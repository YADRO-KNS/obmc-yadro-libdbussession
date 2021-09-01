// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2021 YADRO

#include <libobmcsession/session.hpp>
#include <iostream>

namespace obmc
{
namespace session
{
void SessionItem::delete_()
{
    SessionManager::SessionIdentifier sessionId =
        SessionManager::parseSessionId(this->sessionID());
    if (!managerPtr->remove(sessionId))
    {
        throw InternalFailure();
    }
}

void SessionItem::setSessionMetadata(std::string username,
                                     uint32_t remoteIPAddr)
{
    this->adjustSessionOwner(username);
    this->remoteIPAddr(remoteIPAddr);
}

void SessionItem::resetCleanupFn(SessionManager::SessionCleanupFn&& cleanup)
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
        throw std::runtime_error("The username '" + userName +
                                 "' is not found");
    }

    this->associations({
        make_tuple("user", "session", userObjectPath),
    });
}

} // namespace session
} // namespace obmc
