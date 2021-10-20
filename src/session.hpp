// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2021 YADRO

#pragma once

#include <unistd.h>

#include <phosphor-logging/elog-errors.hpp>
#include <phosphor-logging/log.hpp>

#include <manager.hpp>
#include <xyz/openbmc_project/Association/Definitions/server.hpp>

namespace obmc
{
namespace session
{

using namespace phosphor::logging;

using SessionItemServerObject =
    sdbusplus::server::object::object<SessionItemServer>;
using AssocDefinitionServerObject = sdbusplus::server::object::object<
    sdbusplus::xyz::openbmc_project::Association::server::Definitions>;

class UnknownUser: public std::logic_error
{
    static constexpr auto errDesc = "Unkown username was given.";

  public:
    UnknownUser(): std::logic_error(errDesc)
    {}
    ~UnknownUser() override = default;
};

class SessionItem :
    public SessionItemServerObject,
    public AssocDefinitionServerObject
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
        AssocDefinitionServerObject(bus, objPath.c_str()), bus(bus),
        path(objPath), managerPtr(managerPtr)
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
                SessionManagerPtr managerPtr, SessionCleanupFn&& cleanupFn) :
        SessionItemServerObject(bus, objPath.c_str()),
        AssocDefinitionServerObject(bus, objPath.c_str()), bus(bus),
        path(objPath), managerPtr(managerPtr), cleanupFn(cleanupFn)
    {
        // Nothing to do here
    }

    ~SessionItem() override
    {
        if (cleanupFn != nullptr)
        {
            SessionIdentifier sessionId =
                SessionManager::parseSessionId(this->sessionID());
            std::invoke(cleanupFn, sessionId);
        }
    }

    /** @brief Close the exist session.
     *
     *  @param[in] handle   - Specifies it is required to post-processing the
     *                        session closing  with the configured handler.
     */
    void close(bool handle) override;

    /** @brief Set Username and Remote IP addres of exist session.
     *
     *  @param[in] username         - Owner username of the session
     *  @param[in] remoteIPAddr     - Remote IP address.
     */
    void setSessionMetadata(std::string username, std::string remoteIPAddr);

    /**
     * @brief Callback function to a session cleanup on the close.
     *
     */
    void resetCleanupFn(SessionCleanupFn&&);

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

    /** 
     * @brief Get the session owner username
     * 
     * @throw logic_error       - the username has not been set.
     * 
     * @return std::string      - the session username
     */ 
    const std::string getOwner() const;

    static const std::string
        retrieveUserFromObjectPath(const std::string& objectPath);

    static SessionIdentifier
        retrieveIdFromObjectPath(const std::string& objectPath);

  private:
    sdbusplus::bus::bus& bus;
    /** @brief Path of the group instance */
    const std::string path;
    SessionManagerPtr managerPtr;
    SessionCleanupFn cleanupFn;
};

} // namespace session
} // namespace obmc
