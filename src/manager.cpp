// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2021 YADRO

#include <libobmcsession/manager.hpp>
#include <libobmcsession/session.hpp>
#include <sdbusplus/server/object.hpp>
#include <xyz/openbmc_project/Object/Delete/client.hpp>
#include <xyz/openbmc_project/Session/Item/client.hpp>
#include <xyz/openbmc_project/Session/Build/client.hpp>

#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace obmc
{
namespace session
{

SessionManager::SessionManager(sdbusplus::bus::bus& bus,
                               const std::string& slug,
                               const SessionType type) :
    SessionBuildServer(bus, sessionManagerObjectPath),
    bus(bus), slug(slug), serviceName(serviceNameStartSegment + slug),
    type(type), pendingSessionBuild(false)
{
    dbusManager = std::make_unique<sdbusplus::server::manager::manager>(
        bus, sessionManagerObjectPath);
    bus.request_name(serviceName.c_str());
}

SessionManager::SessionIdentifier
    SessionManager::create(const std::string& userName,
                           const uint32_t remoteAddress)
{
    if (isSessionBuildPending())
    {
        throw std::logic_error(
            "Pending a session creation finish. Building a new session is locked.");
    }

    auto sessionId = generateSessionId();

    auto sessionObjectPath = getSessionObjectPath(sessionId);
    auto session = std::make_unique<SessionItem>(bus, sessionObjectPath,
                                                 shared_from_this());

    session->sessionID(hexSessionId(sessionId));
    session->sessionType(type);
    session->remoteIPAddr(remoteAddress);

    if (!userName.empty())
    {
        session->adjustSessionOwner(userName);
    }

    sessionItems.insert_or_assign(sessionId, std::move(session));

    return sessionId;
}

SessionManager::SessionIdentifier
    SessionManager::create(const std::string& userName,
                           const uint32_t remoteAddress,
                           SessionCleanupFn&& cleanupFn)
{
    auto sessionId = this->create(userName, remoteAddress);
    sessionItems.at(sessionId)->resetCleanupFn(
        std::forward<SessionCleanupFn>(cleanupFn));
    return sessionId;
}

SessionManager::SessionIdentifier SessionManager::create()
{
    auto sessionId = this->create("", 0);
    sessionBuildTimerStart(sessionId);
    return sessionId;
}

SessionManager::SessionIdentifier
    SessionManager::create(SessionCleanupFn&& cleanupFn)
{
    auto sessionId = this->create();
    sessionItems.at(sessionId)->resetCleanupFn(
        std::forward<SessionCleanupFn>(cleanupFn));
    return sessionId;
}

void SessionManager::commitSessionBuild(std::string username,
                            uint32_t remoteIPAddr)
{
    if (!isSessionBuildPending())
    {
        return;
    }
    SessionItemDict::iterator sessionIt = sessionItems.find(this->pendingSessionId);
    if (sessionIt == sessionItems.end())
    {
        return;
    }

    sessionIt->second->setSessionMetadata(username, remoteIPAddr);
    sessionBuildSucess();
}

void SessionManager::commitSessionBuild(sdbusplus::bus::bus& bus,
                                        std::string slug, std::string username,
                                        uint32_t remoteIPAddr)
{
    auto serviceName = serviceNameStartSegment + slug;
    auto callMethod = bus.new_method_call(
        serviceName.c_str(), sessionManagerObjectPath,
        sdbusplus::xyz::openbmc_project::Session::client::Build::interface,
        "CommitSessionBuild");
    callMethod.append(username, remoteIPAddr);
    bus.call_noreply(callMethod);
}

bool SessionManager::remove(SessionIdentifier sessionId)
{
    auto sessionIt = sessionItems.find(sessionId);
    if (sessionItems.end() == sessionIt)
    {
        return false;
    }
    sessionItems.extract(sessionIt);
    return true;
}

std::size_t SessionManager::removeAll(const std::string& userName) const
{
    auto objects = findSessionItemObjects();
    auto userObjectPath = "/xyz/openbmc_project/user/" + userName;
    size_t handledSessions = 0;
    for (const auto& [sessionObjectPath, objectMetaDict] : objects)
    {
        if (objectMetaDict.empty())
        {
            continue;
        }
        const auto& serviceName = objectMetaDict.begin()->first;
        auto details = getSessionDetails(serviceName, sessionObjectPath);
        for (const auto& [propertyName, propertyValue] : details)
        {
            if (propertyName == "Associations")
            {
                const UserAssociationList* userAssociations =
                    std::get_if<UserAssociationList>(&propertyValue);
                if (userAssociations == nullptr)
                {
                    continue;
                }

                for (const auto& userAssociation : *userAssociations)
                {
                    if (std::get<0>(userAssociation) == "user" &&
                        std::get<2>(userAssociation) == userObjectPath)
                    {
                        try
                        {
                            callCloseSession(serviceName, sessionObjectPath);
                            handledSessions++;
                        }
                        catch (const std::exception&)
                        {}
                    }
                }
            }
        }
    }

    return handledSessions;
}

std::size_t SessionManager::removeAll(uint32_t remoteAddress) const
{
    auto objects = findSessionItemObjects();
    size_t handledSessions = 0;
    for (const auto& [sessionObjectPath, objectMetaDict] : objects)
    {
        if (objectMetaDict.empty())
        {
            continue;
        }
        const auto& serviceName = objectMetaDict.begin()->first;
        auto details = getSessionDetails(serviceName, sessionObjectPath);
        for (const auto& [propertyName, propertyValue] : details)
        {
            if (propertyName == "RemoteIPAddr")
            {
                const uint32_t* remoteAddressValue =
                    std::get_if<uint32_t>(&propertyValue);
                if (remoteAddressValue != nullptr &&
                    *remoteAddressValue == remoteAddress)
                {
                    try
                    {
                        callCloseSession(serviceName, sessionObjectPath);
                        handledSessions++;
                    }
                    catch (const std::exception&)
                    {}
                }
            }
        }
    }

    return handledSessions;
}

std::size_t SessionManager::removeAll(SessionType type) const
{
    auto objects = findSessionItemObjects();
    size_t handledSessions = 0;
    for (const auto& [sessionObjectPath, objectMetaDict] : objects)
    {
        if (objectMetaDict.empty())
        {
            continue;
        }
        const auto& serviceName = objectMetaDict.begin()->first;
        auto details = getSessionDetails(serviceName, sessionObjectPath);
        for (const auto& [propertyName, propertyValue] : details)
        {
            if (propertyName == "SessionType")
            {
                const std::string* sessionTypeValue =
                    std::get_if<std::string>(&propertyValue);
                if (sessionTypeValue != nullptr &&
                    *sessionTypeValue ==
                        sdbusplus::message::details::convert_to_string(type))
                {
                    try
                    {
                        callCloseSession(serviceName, sessionObjectPath);
                        handledSessions++;
                    }
                    catch (const std::exception&)
                    {}
                }
            }
        }
    }

    return handledSessions;
}

std::size_t SessionManager::removeAll() const
{
    auto objects = findSessionItemObjects();
    size_t handledSessions = 0;
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
        catch (const std::exception&)
        {}
    }
    return handledSessions;
}

bool SessionManager::isSessionBuildPending() const
{
    return pendingSessionBuild;
}

void SessionManager::resetPendginSessilBuild()
{
    this->pendingSessionBuild = false;
    this->pendingSessionId = 0;
}

SessionManager::SessionIdentifier SessionManager::generateSessionId() const
{
    auto time = std::chrono::high_resolution_clock::now();
    std::size_t timeHash =
        std::hash<int64_t>{}(time.time_since_epoch().count());
    std::size_t serviceNameHash = std::hash<std::string>{}(serviceName);

    return timeHash ^ (serviceNameHash << 1);
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

SessionManager::SessionIdentifier
    SessionManager::parseSessionId(const std::string hexSessionId)
{
    return std::stoull(hexSessionId, nullptr, 16);
}

const SessionManager::DBusSubTreeOut
    SessionManager::findSessionItemObjects() const
{
    constexpr const std::array sessionItemObjectIfaces = {
        sdbusplus::xyz::openbmc_project::Session::client::Item::interface};

    DBusSubTreeOut getSessionItemObjects;
    auto callMethod =
        bus.new_method_call("xyz.openbmc_project.ObjectMapper",
                            "/xyz/openbmc_project/object_mapper",
                            "xyz.openbmc_project.ObjectMapper", "GetSubTree");
    callMethod.append(sessionManagerObjectPath, static_cast<int32_t>(0),
                      sessionItemObjectIfaces);
    bus.call(callMethod).read(getSessionItemObjects);

    return std::forward<DBusSubTreeOut>(getSessionItemObjects);
}

void SessionManager::callCloseSession(const std::string& serviceName,
                                      const std::string& objectPath) const
{
    std::vector<std::string> getSessionItemObjects;
    auto callMethod = bus.new_method_call(
        serviceName.c_str(), objectPath.c_str(),
        sdbusplus::xyz::openbmc_project::Object::client::Delete::interface,
        "Delete");
    bus.call_noreply(callMethod);
}

const SessionManager::DBusSessionDetailsMap
    SessionManager::getSessionDetails(const std::string& serviceName,
                                      const std::string& objectPath) const
{
    SessionManager::DBusSessionDetailsMap sessionDetails;

    auto callMethod =
        bus.new_method_call(serviceName.c_str(), objectPath.c_str(),
                            "org.freedesktop.DBus.Properties", "GetAll");
    callMethod.append(
        sdbusplus::xyz::openbmc_project::Session::client::Item::interface);
    bus.call(callMethod).read(sessionDetails);
    return std::forward<SessionManager::DBusSessionDetailsMap>(sessionDetails);
}

void SessionManager::sessionBuildTimerStart(SessionIdentifier sessionId)
{
    this->pendingSessionBuild = true;
    this->pendingSessionId = sessionId;
    this->timerSessionComplete =
        std::thread(std::move([manager = shared_from_this()] {
            std::this_thread::sleep_for(std::chrono::seconds(10));
            if (manager->isSessionBuildPending())
            {
                manager->remove(manager->pendingSessionId);
                manager->resetPendginSessilBuild();
            }
            return true;
        }));

    this->timerSessionComplete.detach();
}

void SessionManager::sessionBuildSucess()
{
    resetPendginSessilBuild();
}

} // namespace session
} // namespace obmc
