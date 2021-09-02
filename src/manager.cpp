// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2021 YADRO

#include <dbus.hpp>
#include <manager.hpp>
#include <session.hpp>

#include <sdbusplus/server/object.hpp>
#include <xyz/openbmc_project/Association/client.hpp>
#include <xyz/openbmc_project/Session/Build/client.hpp>
#include <xyz/openbmc_project/Session/Item/client.hpp>

#include <phosphor-logging/elog-errors.hpp>
#include <phosphor-logging/log.hpp>

#include <sdbusplus/asio/connection.hpp>

#include <chrono>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>

namespace obmc
{

namespace session
{

using namespace obmc::dbus;
using namespace phosphor::logging;

SessionManager::SessionManager(sdbusplus::bus::bus& busIn,
                               const std::string& slug,
                               const SessionType type) :
    SessionBuildServer(busIn, sessionManagerObjectPath),
    bus(&busIn), slug(slug), serviceName(serviceNameStartSegment + slug),
    type(type), pendingSessionBuild(false)
{
    dbusManager = std::make_unique<sdbusplus::server::manager::manager>(
        *bus, sessionManagerObjectPath);
    bus->request_name(serviceName.c_str());
}

SessionManager::SessionManager(sdbusplus::bus::bus&& initBus,
                               const std::string& slug,
                               const SessionType type) :
    SessionBuildServer(initBus, sessionManagerObjectPath),
    slug(slug), serviceName(serviceNameStartSegment + slug), type(type),
    pendingSessionBuild(false), bus(std::make_unique<sdbusplus::bus::bus>(
                                    std::forward<sdbusplus::bus::bus>(initBus)))
{
    dbusManager = std::make_unique<sdbusplus::server::manager::manager>(
        initBus, sessionManagerObjectPath);
    bus->request_name(serviceName.c_str());
}

SessionIdentifier SessionManager::create(const std::string& userName,
                                         const std::string& remoteAddress)
{
    if (isSessionBuildPending())
    {
        throw std::logic_error("Pending a session creation finish. Building a "
                               "new session is locked.");
    }

    auto sessionId = generateSessionId();

    auto sessionObjectPath = getSessionObjectPath(sessionId);
    auto session = std::make_shared<SessionItem>(*bus, sessionObjectPath,
                                                 shared_from_this());

    session->sessionID(hexSessionId(sessionId));
    session->sessionType(type);
    session->remoteIPAddr(remoteAddress);

    if (!userName.empty())
    {
        try
        {
            session->adjustSessionOwner(userName);
        }
        catch (const std::exception& ex)
        {
            log<level::DEBUG>(
                "Skip publishing the obmcsess object if user not found",
                entry("USER=%d", userName.c_str()),
                entry("ERROR=%s", ex.what()));
            return 0U;
        }
    }

    sessionItems.emplace(sessionId, session);
    return sessionId;
}

SessionIdentifier SessionManager::create(const std::string& userName,
                                         const std::string& remoteAddress,
                                         SessionCleanupFn&& cleanupFn)
{
    auto sessionId = this->create(userName, remoteAddress);
    if (sessionId == 0U)
    {
        // session not created. Skip.
        return sessionId;
    }
    sessionItems.at(sessionId)->resetCleanupFn(
        std::forward<SessionCleanupFn>(cleanupFn));
    return sessionId;
}

SessionIdentifier SessionManager::startTransaction()
{
    cvTransaction.notify_all();
    auto sessionId = this->create("", "0.0.0.0");
    sessionBuildTimerStart(sessionId);
    return sessionId;
}

SessionIdentifier SessionManager::startTransaction(SessionCleanupFn&& cleanupFn)
{
    auto sessionId = this->startTransaction();
    sessionItems.at(sessionId)->resetCleanupFn(
        std::forward<SessionCleanupFn>(cleanupFn));
    return sessionId;
}

void SessionManager::commitSessionBuild(std::string username,
                                        std::string remoteIPAddr)
{
    if (!isSessionBuildPending())
    {
        log<level::ERR>(
            "Failure to commit sesion build: transaction not started.");
        throw InternalFailure();
    }
    SessionItemDict::iterator sessionIt =
        sessionItems.find(this->pendingSessionId);
    if (sessionIt == sessionItems.end())
    {
        log<level::ERR>(
            "Failure to commit sesion build: session ID not found.",
            entry("SESSIONID=%d", this->pendingSessionId));
        throw InvalidArgument();
    }
    try
    {
        sessionIt->second->setSessionMetadata(username, remoteIPAddr);
    }
    catch (const UnknownUser& ex)
    {
        log<level::INFO>("User is not managed by UserManager service. Skip "
                         "publishing a session.",
                         entry("USER=%s", username.c_str()),
                         entry("ERROR=%s", ex.what()));
        // remove obmcsess object if user not found.
        remove(sessionIt->first, false, true);
    }
    catch (const std::exception& ex)
    {
        // Keep session object to give a chance to commit session with a valid
        // metadata in a next having time.
        log<level::ERR>("Failure to commit sesion build.",
                          entry("ERROR=%s", ex.what()));
        throw InternalFailure();
    }
    sessionBuildSucess();
}

void SessionManager::commitSessionBuild(sdbusplus::bus::bus& bus,
                                        std::string slug, std::string username,
                                        std::string remoteIPAddr)
{
    auto serviceName = serviceNameStartSegment + slug;
    auto callMethod = bus.new_method_call(
        serviceName.c_str(), sessionManagerObjectPath,
        sdbusplus::xyz::openbmc_project::Session::client::Build::interface,
        "CommitSessionBuild");
    callMethod.append(username, remoteIPAddr);
    bus.call_noreply(callMethod);
}

bool SessionManager::remove(SessionIdentifier sessionId, bool withCleanup,
                            bool localLookup)
{
    log<level::DEBUG>("SessionManager::remove()", entry("SESSID=%d", sessionId),
                      entry("CLEANUP=%d", withCleanup),
                      entry("ISLOCAL=%d", localLookup));
    auto sessionIt = sessionItems.find(sessionId);
    if (sessionItems.end() != sessionIt)
    {
        auto sessionItem = sessionIt->second;
        if (!withCleanup)
        {
            sessionItem->resetCleanupFn(nullptr);
        }
        sessionItems.erase(sessionIt);
        return true;
    }
    if (localLookup) 
    {
        // lookup only at the current session manager (local)
        log<level::DEBUG>("[SessionManager::remove] lookup only at current "
                          "session manager (local)");
        return false;
    }
    InternalSessionInfoList sessionList;
    getAllSessions(sessionList);
    for (const auto& [sessionInfoId, sessionInfo] : sessionList)
    {
        if (sessionInfoId == sessionId)
        {
            callCloseSession(sessionInfo.serviceName, sessionInfo.objectPath,
                             withCleanup);
            return true;
        }
    }
    log<level::WARNING>(
        "SessionManager::remove() fail: target session not found",
        entry("SESSID=%s"));
    return false;
}

std::size_t SessionManager::removeAll(const std::string& userName)
{
    std::size_t handledSessions = 0;

    for (const auto [sessionId, sessionItem] : sessionItems)
    {
        if (userName == sessionItem->getOwner())
        {
            sessionItems.erase(sessionId);
            handledSessions++;
        }
    }

    InternalSessionInfoList sessionList;
    getAllSessions(sessionList);
    for (const auto& [sessionInfoId, sessionInfo] : sessionList)
    {
        if (userName == sessionInfo.serviceName)
        {
            callCloseSession(sessionInfo.serviceName, sessionInfo.objectPath);
            handledSessions++;
        }
    }

    return handledSessions;
}

std::size_t
    SessionManager::removeAllByRemoteAddress(const std::string& remoteAddress)
{
    std::size_t handledSessions = 0;

    for (const auto [sessionId, sessionItem] : sessionItems)
    {
        if (remoteAddress == sessionItem->remoteIPAddr())
        {
            sessionItems.erase(sessionId);
            handledSessions++;
        }
    }

    InternalSessionInfoList sessionList;
    getAllSessions(sessionList);
    for (const auto& [sessionInfoId, sessionInfo] : sessionList)
    {
        if (remoteAddress == sessionInfo.remoteAddress)
        {
            callCloseSession(sessionInfo.serviceName, sessionInfo.objectPath);
            handledSessions++;
        }
    }

    return handledSessions;
}

std::size_t SessionManager::removeAll(SessionType type)
{
    std::size_t handledSessions = 0;

    for (const auto [sessionId, sessionItem] : sessionItems)
    {
        if (type == sessionItem->sessionType())
        {
            sessionItems.erase(sessionId);
            handledSessions++;
        }
    }

    InternalSessionInfoList sessionList;
    getAllSessions(sessionList);
    for (const auto& [sessionInfoId, sessionInfo] : sessionList)
    {
        if (type == sessionInfo.type)
        {
            callCloseSession(sessionInfo.serviceName, sessionInfo.objectPath);
            handledSessions++;
        }
    }

    return handledSessions;
}

std::size_t SessionManager::removeAll()
{
    size_t handledSessions = sessionItems.size();
    sessionItems.clear();
    auto objects = findSessionItemObjects();
    for (const auto& [sessionObjectPath, objectMetaDict] : objects)
    {
        if (objectMetaDict.empty())
        {
            continue;
        }
        try
        {
            callCloseSession(objectMetaDict.begin()->first, sessionObjectPath);
            handledSessions++;
        }
        catch (const std::exception& ex)
        {
            log<level::ERR>("Fail to remove session.",
                            entry("OBJPATH=%s", sessionObjectPath.c_str()),
                            entry("ERROR=%s", ex.what()));
        }
    }
    return handledSessions;
}

bool SessionManager::isSessionBuildPending() const
{
    return pendingSessionBuild;
}

void SessionManager::resetPendginSessionBuild()
{
    this->pendingSessionBuild = false;
    this->pendingSessionId = 0;
    cvTransaction.notify_all();
}

SessionIdentifier SessionManager::generateSessionId() const
{
    auto time = std::chrono::high_resolution_clock::now();
    std::size_t timeHash =
        std::hash<int64_t>{}(time.time_since_epoch().count());
    std::size_t serviceNameHash = std::hash<std::string>{}(serviceName);

    // constexpr unsigned int leftShiftSize = 8 * sizeof(std::size_t);
    std::size_t result = timeHash ^ (serviceNameHash << 1);
    if (result == 0U)
    {
        // The hash-collision guard. The Session ID == 0U is reserved and can't
        // be provided as a valid ID.
        return generateSessionId();
    }

    return result;
}

const std::string
    SessionManager::getSessionObjectPath(SessionIdentifier sessionId) const
{
    return getSessionManagerObjectPath() + "/" + hexSessionId(sessionId);
}

const std::string SessionManager::getSessionManagerObjectPath() const
{
    return std::string(sessionManagerObjectPath) + "/" + slug;
}

const std::string SessionManager::hexSessionId(SessionIdentifier sessionId)
{
    std::stringstream stream;
    stream << std::setfill('0') << std::setw(sizeof(sessionId) * 2) << std::hex
           << sessionId;
    return stream.str();
}

SessionIdentifier SessionManager::parseSessionId(const std::string hexSessionId)
{
    return std::stoull(hexSessionId, nullptr, 16);
}

const DBusSubTreeOut SessionManager::findSessionItemObjects() const
{
    using namespace dbus;
    using namespace sdbusplus::xyz::openbmc_project;

    constexpr const std::array searchUserAssocIfaces = {
        client::Association::interface};
    constexpr const std::array searchSessionItemIfaces = {
        Session::client::Item::interface};

    DBusSubTreeOut getSessionItemObjects;

    auto callGetManagedObjects = bus->new_method_call(
        object_mapper::service, "/", freedesktop::objectManagerIface,
        freedesktop::getManagedObjects);

    freedesktop::ManagedObjectType managedObjectsList;
    bus->call(callGetManagedObjects).read(managedObjectsList);

    log<level::DEBUG>("Fill the ManagedObjectsList.",
                      entry("SIZE=%d", managedObjectsList.size()));
    for (const auto& [managedObject, objectIfaceList] : managedObjectsList)
    {
        if (!managedObject.str.starts_with("/xyz/openbmc_project/user") ||
            !managedObject.str.ends_with("/session"))
        {
            // that is not user session object.
            continue;
        }

        log<level::DEBUG>("Found user session object.",
                          entry("OBJPATH=%s", managedObject.str.c_str()));

        auto assocIfaceIt =
            objectIfaceList.find(client::Association::interface);
        if (assocIfaceIt == objectIfaceList.end())
        {
            log<level::DEBUG>("Found user object haven't required interface");
            continue;
        }

        auto endpointsPropertyIt = assocIfaceIt->second.find("endpoints");
        if (endpointsPropertyIt == assocIfaceIt->second.end())
        {
            log<level::DEBUG>(
                "found association haven't the required 'endpoints' property");
            continue;
        }

        auto sessionObjects =
            std::get<std::vector<std::string>>(endpointsPropertyIt->second);
        log<level::DEBUG>("Ack the SessionObjects.",
                          entry("SIZE=%d", sessionObjects.size()));
        for (const auto& sessionObject : sessionObjects)
        {
            SessionIdentifier sessionId;
            try
            {
                sessionId =
                    SessionItem::retrieveIdFromObjectPath(sessionObject);
            }
            catch (const std::exception& ex)
            {
                log<level::WARNING>("ObjectDetails: invalid object path format",
                                    entry("ERROR=%s", ex.what()));
                continue;
            }
            if (this->sessionItems.find(sessionId) != this->sessionItems.end())
            {
                log<level::DEBUG>(
                    "found session is managered by the current DBus serivce.");
                continue;
            }

            log<level::DEBUG>("Try to query session.",
                              entry("OBJPATH=%s", sessionObject.c_str()));
            DBusGetObjectOut getObjectOut;
            auto callGetObject = bus->new_method_call(
                object_mapper::service, object_mapper::object,
                object_mapper::interface, object_mapper::getObject);
            callGetObject.append(sessionObject, searchSessionItemIfaces);
            try {
                bus->call(callGetObject).read(getObjectOut);
                getSessionItemObjects[sessionObject] = getObjectOut;
            }
            catch (std::exception& ex)
            {
                log<level::ERR>("Fail to query session info.",
                                  entry("OBJPATH=%s", sessionObject.c_str()),
                                  entry("ERROR=%s", ex.what()));
            }
        }
    }

    log<level::DEBUG>("Ack SessionItemObjects",
                      entry("SIZE=%d", getSessionItemObjects.size()));
    return std::forward<DBusSubTreeOut>(getSessionItemObjects);
}

void SessionManager::callCloseSession(const std::string& serviceName,
                                      const std::string& objectPath,
                                      bool withCleanup) const
{
    std::vector<std::string> getSessionItemObjects;
    auto callMethod = bus->new_method_call(
        serviceName.c_str(), objectPath.c_str(),
        sdbusplus::xyz::openbmc_project::Session::client::Item::interface,
        "Close");
    callMethod.append(withCleanup);
    bus->call_noreply(callMethod);
}

const DBusSessionDetailsMap
    SessionManager::getSessionsProperties(const std::string& serviceName,
                                          const std::string& objectPath) const
{
    DBusSessionDetailsMap sessionDetails;

    auto callMethod = bus->new_method_call(
        serviceName.c_str(), objectPath.c_str(),
        dbus::freedesktop::propertyIface, dbus::freedesktop::getAll);
    // get properties of any interface
    callMethod.append("");

    bus->call(callMethod).read(sessionDetails);
    return std::forward<DBusSessionDetailsMap>(sessionDetails);
}

void SessionManager::sessionBuildTimerStart(SessionIdentifier sessionId)
{
    using namespace std::chrono_literals;
    using namespace sdbusplus::xyz::openbmc_project;

    constexpr const std::array searchSessionItemIfaces = {
        Session::client::Item::interface};

    this->pendingSessionBuild = true;
    this->pendingSessionId = sessionId;
    this->timerSessionComplete =
        std::thread(std::move([manager = shared_from_this()] {
            std::unique_lock<std::mutex> traksactionLock(
                manager->cvmTransaction);
            auto waitResult = manager->cvTransaction.wait_for(
                traksactionLock, 20s,
                [manager] { return !manager->pendingSessionBuild; });
            if (!waitResult)
            {
                log<level::WARNING>("Timed out. Reset transaction.",
                                    entry("SERVICE=%s", manager->slug.c_str()));
                try
                {
                    manager->resetPendginSessionBuild();
                }
                catch (const std::exception& ex)
                {
                    log<level::DEBUG>("Failure to reset transaction",
                                      entry("ERROR=%s", ex.what()));
                }
            }
        }));

    this->timerSessionComplete.detach();
}

void SessionManager::sessionBuildSucess()
{
    resetPendginSessionBuild();
}

void SessionManager::getSessionInfo(SessionIdentifier id,
                                    InternalSessionInfo& sessionInfo) const
{
    if (id == 0)
    {
        return;
    }

    auto sessIt = this->sessionItems.find(id);
    auto sessionPtr = sessIt->second;
    if (sessIt != this->sessionItems.end())
    {
        sessionInfo.id = id;
        sessionInfo.username = sessionPtr->getOwner();
        sessionInfo.remoteAddress = sessionPtr->remoteIPAddr();
        sessionInfo.type = sessionPtr->sessionType();
        return;
    }

    auto objects = findSessionItemObjects();

    log<level::DEBUG>("Count external session objects",
                      entry("SIZE=%d", objects.size()));
    InternalSessionInfoList sessionsList;
    getSessionsInfo(objects, sessionsList, {{id}});
    if (sessionsList.empty())
    {
        throw std::invalid_argument("Session ID not found");
    }
    auto sessionInfoIt = sessionsList.begin();
    sessionInfo = sessionInfoIt->second;
}

void SessionManager::getAllSessions(InternalSessionInfoList& sessionsList) const
{
    for (const auto [sessionId, session] : sessionItems)
    {
        const auto objectPath = getSessionObjectPath(sessionId);
        InternalSessionInfo sessionInfo{
            sessionId,
            session->getOwner(),
            session->remoteIPAddr(),
            session->sessionType(),
            serviceName,
            objectPath,
            true,
        };
        sessionsList.emplace(sessionId, sessionInfo);
    }

    const auto sessionObjects = findSessionItemObjects();
    getSessionsInfo(sessionObjects, sessionsList);
}

void SessionManager::getSessionsInfo(
    const DBusSubTreeOut& sessionSubTree, InternalSessionInfoList& sessionsList,
    std::optional<std::vector<SessionIdentifier>> listSearchingSessions) const
{
    for (const auto& [sessionObjectPath, objectMetaDict] : sessionSubTree)
    {
        std::function<bool(SessionIdentifier id)> matchSessionId =
            [sessionObjectPath](SessionIdentifier id) -> bool {
            return id > 0 && sessionObjectPath.ends_with(hexSessionId(id));
        };
        if (objectMetaDict.empty() ||
            (listSearchingSessions.has_value() &&
             !std::any_of(listSearchingSessions->begin(),
                          listSearchingSessions->end(), matchSessionId)))
        {
            log<level::DEBUG>("Skip loop objects",
                              entry("OBJPATH=%s", sessionObjectPath.c_str()));
            continue;
        }
        const auto& svcName = objectMetaDict.begin()->first;
        log<level::DEBUG>("Examinate object to obtain session info",
                          entry("OBJPATH=%s", sessionObjectPath.c_str()),
                          entry("SERVICE=%s", svcName.c_str()));
        const auto details = getSessionsProperties(svcName, sessionObjectPath);
        log<level::DEBUG>("Count properties of ObjectDetails",
                          entry("COUNT=%d", details.size()));
        SessionIdentifier sessionId;
        try
        {
            sessionId =
                SessionItem::retrieveIdFromObjectPath(sessionObjectPath);
        }
        catch (const std::exception& ex)
        {
            log<level::WARNING>("ObjectDetails: invalid object path format",
                                entry("ERROR=%s", ex.what()));
            continue;
        }
        InternalSessionInfo sessionInfo{sessionId};
        sessionInfo.serviceName = svcName;
        sessionInfo.objectPath = sessionObjectPath;
        sessionInfo.isOwn = false;
        for (const auto& [propertyName, propertyValue] : details)
        {
            if (propertyName == "Associations")
            {
                log<level::DEBUG>("Found Session item associations");
                const UserAssociationList* userAssociations =
                    std::get_if<UserAssociationList>(&propertyValue);
                if (userAssociations == nullptr)
                {
                    log<level::WARNING>("Bad association: nullptr. skip");
                    continue;
                }

                for (const auto& userAssociation : *userAssociations)
                {
                    const std::string assocType = std::get<0>(userAssociation);
                    if (assocType == std::string("user"))
                    {
                        log<level::DEBUG>("User association: OK");
                        try
                        {
                            const std::string userObjectPath =
                                std::get<2>(userAssociation);
                            sessionInfo.username =
                                SessionItem::retrieveUserFromObjectPath(
                                    userObjectPath);
                            break;
                        }
                        catch (const std::exception& ex)
                        {
                            log<level::WARNING>(
                                "ObjectDetails: Failure to get User field",
                                entry("ERROR=%s", ex.what()),
                                entry("FIELD=%s", "UserObjectPath"));
                        }
                    }
                }
            }
            else if (propertyName == "RemoteIPAddr")
            {
                const std::string* remoteAddressValue =
                    std::get_if<std::string>(&propertyValue);
                if (remoteAddressValue != nullptr)
                {
                    sessionInfo.remoteAddress = *remoteAddressValue;
                }
            }
            else if (propertyName == "SessionType")
            {
                const std::string* sessionTypeValue =
                    std::get_if<std::string>(&propertyValue);
                if (sessionTypeValue != nullptr)
                {
                    sessionInfo.type =
                        sdbusplus::xyz::openbmc_project::Session::server::Item::
                            convertTypeFromString(*sessionTypeValue);
                }
            }
        }
        sessionsList.emplace(sessionId, sessionInfo);
    }
}

} // namespace session
} // namespace obmc
