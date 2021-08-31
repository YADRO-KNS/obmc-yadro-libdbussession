// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2021 YADRO

#include <libobmcsession/manager.hpp>
#include <sdbusplus/server/object.hpp>
#include <src/session.hpp>
#include <xyz/openbmc_project/Object/Delete/client.hpp>
#include <xyz/openbmc_project/Session/Item/client.hpp>

#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace obmc
{
namespace session
{

constexpr const char* sessionManagerObjectPath =
    "/xyz/openbmc_project/session_manager/";

SessionManager::SessionIdentifier
    SessionManager::create(const std::string& userName,
                           const uint32_t remoteAddress)
{
    auto sessionId = generateSessionId();
    auto sessionObjectPath = getSessionObjectPath(sessionId);
    auto session =
        std::make_unique<SessionItem>(bus, sessionObjectPath, weak_from_this());

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

void SessionManager::setSessionMetadata(SessionIdentifier sessionId,
                                        const std::string& userName,
                                        const uint32_t remoteAddress)
{
    auto objects = findSessionItemObjects();
    auto hexSessionId = this->hexSessionId(sessionId);
    for (const auto& [sessionObjectPath, objectMetaDict] : objects)
    {
        if (objectMetaDict.empty())
        {
            continue;
        }
        const auto& serviceName = objectMetaDict.begin()->first;
        const auto details = getSessionDetails(serviceName, sessionObjectPath);
        for (const auto& [propertyName, propertyValue] : details)
        {
            if (propertyName == "SessionID")
            {
                const std::string* sessionIdStr =
                    std::get_if<std::string>(&propertyValue);
                if (sessionIdStr != nullptr && hexSessionId == *sessionIdStr)
                {
                    auto callMethod = bus.new_method_call(
                        serviceName.c_str(), sessionObjectPath.c_str(),
                        sdbusplus::xyz::openbmc_project::Session::client::Item::
                            interface,
                        "SetSessionMetadata");
                    callMethod.append(userName, remoteAddress);
                    bus.call_noreply(callMethod);
                    return;
                }
            }
        }
    }
}

bool SessionManager::remove(SessionIdentifier sessionId)
{
    auto sessionIt = sessionItems.find(sessionId);
    if (sessionItems.end() == sessionIt)
    {
        return false;
    }
    sessionItems.erase(sessionIt);
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
    return sessionManagerObjectPath + slug;
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
        "xyz.openbmc_project.Session.Item"};

    DBusSubTreeOut getSessionItemObjects;
    auto callMethod =
        bus.new_method_call("xyz.openbmc_project.ObjectMapper",
                            "/xyz/openbmc_project/object_mapper",
                            "xyz.openbmc_project.ObjectMapper", "GetSubTree");
    callMethod.append(sessionManagerObjectPath, 0U, sessionItemObjectIfaces);
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
    constexpr const std::array sessionItemObjectIfaces = {
        "xyz.openbmc_project.Session.Item"};

    SessionManager::DBusSessionDetailsMap sessionDetails;

    auto callMethod =
        bus.new_method_call(serviceName.c_str(), objectPath.c_str(),
                            "org.freedesktop.DBus.Properties", "GetAll");
    callMethod.append(sessionItemObjectIfaces);
    bus.call(callMethod).read(sessionDetails);

    return std::forward<SessionManager::DBusSessionDetailsMap>(sessionDetails);
}

} // namespace session
} // namespace obmc
