// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2021 YADRO

#include <dbus.hpp>
#include <session.hpp>

#include <iostream>

#include <phosphor-logging/elog-errors.hpp>
#include <phosphor-logging/log.hpp>

namespace obmc
{
namespace session
{

using namespace phosphor::logging;

void SessionItem::close(bool handle)
{
    SessionIdentifier sessionId =
        SessionManager::parseSessionId(this->sessionID());

    log<level::DEBUG>("SessionItem::close()", entry("SESSIONID=%d", sessionId),
                      entry("ISCLEANUP=%d", handle));

    auto cleanupFnPtr = cleanupFn;
    if (!handle && cleanupFn)
    {
        resetCleanupFn(nullptr);
    }
    if (!managerPtr->remove(sessionId, handle, true))
    {
        // Restore cleanup function if something fail.
        resetCleanupFn(std::forward<SessionCleanupFn>(cleanupFnPtr));
        throw InternalFailure();
    }
}

void SessionItem::setSessionMetadata(std::string username,
                                     std::string remoteIPAddr)
{
    this->adjustSessionOwner(username);
    if (remoteIPAddr.empty())
    {
        throw InvalidArgument();
    }
    this->remoteIPAddr(remoteIPAddr);
}

void SessionItem::resetCleanupFn(SessionCleanupFn&& cleanup)
{
    this->cleanupFn = cleanup;
}

void SessionItem::adjustSessionOwner(const std::string& userName)
{
    using DBusGetObjectOut = std::map<std::string, std::vector<std::string>>;

    constexpr const std::array userObjectIfaces = {
        "xyz.openbmc_project.User.Attributes"};
    auto userObjectPath = "/xyz/openbmc_project/user/" + userName;

    DBusGetObjectOut getUserObject;
    auto callMethod =
        bus.new_method_call("xyz.openbmc_project.ObjectMapper",
                            "/xyz/openbmc_project/object_mapper",
                            "xyz.openbmc_project.ObjectMapper", "GetObject");
    callMethod.append(userObjectPath.c_str(), userObjectIfaces);
    bus.call(callMethod).read(getUserObject);

    if (getUserObject.empty())
    {
        throw UnknownUser();
    }

    this->associations({
        make_tuple("user", "session", userObjectPath),
    });
}

const std::string SessionItem::getOwner() const
{
    for (const auto assocTuple : associations())
    {
        const std::string assocType = std::get<0>(assocTuple);
        if (assocType != std::string("user"))
        {
            continue;
        }
        const std::string userObjectPath = std::get<2>(assocTuple);

        return retrieveUserFromObjectPath(userObjectPath);
    }

    throw std::logic_error("The username has not been set.");
}

const std::string
    SessionItem::retrieveUserFromObjectPath(const std::string& objectPath)
{
    return dbus::utils::getLastSegmentFromObjectPath(objectPath);
}

SessionIdentifier
    SessionItem::retrieveIdFromObjectPath(const std::string& objectPath)
{
    return SessionManager::parseSessionId(
        dbus::utils::getLastSegmentFromObjectPath(objectPath));
}

} // namespace session
} // namespace obmc
