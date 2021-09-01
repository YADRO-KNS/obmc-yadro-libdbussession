// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2021 YADRO

#pragma once

#include <unistd.h>

#include <libobmcsession/manager.hpp>
#include <xyz/openbmc_project/Association/Definitions/server.hpp>
#include <xyz/openbmc_project/Common/error.hpp>
#include <xyz/openbmc_project/Object/Delete/server.hpp>

namespace obmc
{
namespace session
{

using SessionItemServerObject =
    sdbusplus::server::object::object<SessionItemServer>;
using AssocDefinitionServerObject = sdbusplus::server::object::object<
    sdbusplus::xyz::openbmc_project::Association::server::Definitions>;

using DeleteServerObject = sdbusplus::server::object::object<
    sdbusplus::xyz::openbmc_project::Object::server::Delete>;
using InternalFailure =
    sdbusplus::xyz::openbmc_project::Common::Error::InternalFailure;

class SessionItem :
    public SessionItemServerObject,
    public AssocDefinitionServerObject,
    public DeleteServerObject
{
  public:
    SessionItem() = delete;
    SessionItem(const SessionItem&) = delete;
    SessionItem& operator=(const SessionItem&) = delete;
    SessionItem(SessionItem&&) = default;
    SessionItem& operator=(SessionItem&&) = default;

    /** @brief Constructs dbus-object-server of Session Item.
     *
     * @param[in] bus               - Handle to system dbus
     * @param[in] objPath           - The Dbus path that hosts Session Item.
     * @param[in] managerPtr        - The pointer of manager.
     */
    SessionItem(sdbusplus::bus::bus& bus, const std::string& objPath,
                SessionManagerPtr managerPtr) :
        SessionItemServerObject(bus, objPath.c_str()),
        AssocDefinitionServerObject(bus, objPath.c_str()),
        DeleteServerObject(bus, objPath.c_str()), bus(bus), path(objPath),
        managerPtr(managerPtr)
    {
        // Nothing to do here
    }

    /** @brief Constructs dbus-object-server of session item.
     *
     * @param[in] bus           - Handle to system dbus
     * @param[in] objPath       - The Dbus path that hosts Session Item
     * @param[in] managerPtr        - The pointer of manager.
     * @param[in] cleanupFn     - The callback will be handling a customized
     *                            cleanup of the session on the session-item
     *                            removal.
     */
    SessionItem(sdbusplus::bus::bus& bus, const std::string& objPath,
                SessionManagerPtr managerPtr,
                SessionManager::SessionCleanupFn&& cleanupFn) :
        SessionItemServerObject(bus, objPath.c_str()),
        AssocDefinitionServerObject(bus, objPath.c_str()),
        DeleteServerObject(bus, objPath.c_str()), bus(bus), path(objPath),
        managerPtr(managerPtr), cleanupFn(cleanupFn)
    {
        // Nothing to do here
    }

    ~SessionItem() override
    {
        if (cleanupFn != nullptr)
        {
            std::invoke(cleanupFn, identifier);
        }
    }

    /**
     * @brief callback to delete object of the
     *        `xyz.openbmc_project.Object.Delete` dbus interface method
     *
     */
    void delete_() override;

    /** @brief Implementation for SetSessionMetadata
     *         Set Username and Remote IP addres of exist session.
     *
     *  @param[in] username         - Owner username of the session
     *  @param[in] remoteIPAddr     - Remote IP address.
     */
    void setSessionMetadata(std::string username,
                            uint32_t remoteIPAddr) override;

    /**
     * @brief Callback function to a session cleanup on the close.
     *
     */
    void resetCleanupFn(SessionManager::SessionCleanupFn&&);

    /**
     * @brief Associate user of specified username with the current session.
     *
     * @param userName          - the user name to associate with the
     *                            current session.
     *
     * @throw std::exception    failure on set user object relation to
     *                          the current session
     */
    void adjustSessionOwner(const std::string& userName);

  private:
    SessionManager::SessionIdentifier identifier;
    sdbusplus::bus::bus& bus;
    /** @brief Path of the group instance */
    const std::string path;
    SessionManagerPtr managerPtr;
    SessionManager::SessionCleanupFn cleanupFn;
};

} // namespace session
} // namespace obmc
