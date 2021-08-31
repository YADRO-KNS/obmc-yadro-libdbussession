/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2021 YADRO.
 */

#pragma once

#include <sdbusplus/bus.hpp>
#include <xyz/openbmc_project/Session/Item/server.hpp>

namespace obmc
{
namespace session
{

using SessionItemServer =
    sdbusplus::xyz::openbmc_project::Session::server::Item;

class SessionManager;
class SessionItem;

using SessionItemUni = std::unique_ptr<SessionItem>;
using SessionManagerPtr = std::shared_ptr<SessionManager>;
using SessionManagerWeakPtr = std::weak_ptr<SessionManager>;

class SessionManager final : public std::enable_shared_from_this<SessionManager>
{
    static constexpr const char* serviceNameStartSegment =
        "xyz.openbmc_project.Session.";

  public:
    using SessionIdentifier = std::uint64_t;
    using SessionType = SessionItemServer::Type;
    using SessionCleanupFn = std::function<bool(SessionIdentifier)>;

    SessionManager() = delete;
    ~SessionManager() = default;
    SessionManager(const SessionManager&) = delete;
    SessionManager& operator=(const SessionManager&) = delete;
    SessionManager(SessionManager&&) = delete;
    SessionManager& operator=(SessionManager&&) = delete;

    /** @brief Constructs session manager
     *
     * @param[in] bus     - Handle to system dbus
     * @param[in] objPath - The Dbus service slug uniquely identifying the source
     *                      of a session to create the appropriate items from.
     *                      Service name template:
     *                      'xyz.openbmc_project.Session.${slug}'.
     * @param[in] type    - Type of all session items that will be created by
     *                      the current instance.
     */
    SessionManager(sdbusplus::bus::bus& bus, const std::string& slug,
                   const SessionType type) :
        bus(bus),
        slug(slug), serviceName(serviceNameStartSegment + slug), type(type)
    {}

    /**
     * @brief Create a session and publish into the dbus.
     *
     * @param userName              - the owner user name
     * @param remoteAddress         - the IP address of the session initiator.
     *
     * @return SessionIdentifier    - unique session ID
     */
    SessionIdentifier create(const std::string& userName,
                             const uint32_t remoteAddress);

    /**
     * @brief Create a session and publish into the dbus without appropriate
     *        session payload
     *
     * @return SessionIdentifier    - unique session ID
     */
    SessionIdentifier create();

    /**
     * @brief Create a session and publish into the dbus without appropriate
     *        session payload
     *
     * @param cleanupFn             - the callback to cleanup session on
     *                                destroy.
     *
     * @return SessionIdentifier    - unique session ID
     */
    SessionIdentifier create(SessionCleanupFn&& cleanupFn);

    /**
     * @brief Create a session and publish into the dbus with cleanup callback
     *        on the session destroy.
     *
     * @param userName              - the owner user name
     * @param remoteAddress         - the IP address of the session initiator.
     * @param cleanupFn             - the callback to cleanup session on destroy.
     *
     * @return SessionIdentifier    - unique session ID
     */
    SessionIdentifier create(const std::string& userName,
                             const uint32_t remoteAddress,
                             SessionCleanupFn&& cleanupFn);

    /**
     * @brief Set the Session Metadata object
     *
     * @param sessionId     - session identifier.
     * @param userName      - the owner user name.
     * @param remoteAddress - the IP address of the session initiator.
     *
     */
    void setSessionMetadata(SessionIdentifier sessionId,
                            const std::string& userName,
                            const uint32_t remoteAddress);
    /**
     * @brief Remove a dbus session object from storage and unpublish it from
     *        dbus.
     *
     * @param sessionId     - unique session ID to remove from storage
     *
     * @return true         - success
     * @return false        - fail
     */
    bool remove(SessionIdentifier sessionId);

    /**
     * @brief Remove all sessions associated with the specified user.
     *
     * @param userName      - username to close appropriate sessions
     *
     * @throw std::runtime_error not implemented
     *
     * @return std::size_t  - count of closed sessions
     */
    std::size_t removeAll(const std::string& userName) const;

    /**
     * @brief Remove all sessions which have been opened from the specified IPv4
     *        address.
     *
     * @param remoteAddress - the IP address of the session initiator..
     *
     * @throw std::runtime_error not implemented
     *
     * @return std::size_t  - count of closed sessions
     */
    std::size_t removeAll(uint32_t remoteAddress) const;

    /**
     * @brief Remove all sessions of specified type.
     *
     * @param type         - the type of session to close.
     *
     * @throw std::runtime_error not implemented
     *
     * @return std::size_t - count of closed sessions
     */
    std::size_t removeAll(SessionType type) const;

    /**
     * @brief Unconditional removes all opened sessions.
     *
     * @throw std::runtime_error not implemented
     *
     * @return std::size_t - count of closed sessions
     */
    std::size_t removeAll() const;

  protected:
    friend class SessionItem;
    using DBusSubTreeOut =
        std::map<std::string, std::map<std::string, std::vector<std::string>>>;
    using UserAssociation = std::tuple<std::string, std::string, std::string>;
    using UserAssociationList = std::vector<UserAssociation>;
    using DBusSessionDetailsMap =
        std::map<std::string,
                 std::variant<std::string, uint32_t, UserAssociationList>>;
    /**
     * @brief Generate new session depened on current session manager slug and
     *        timestamp.
     *
     * @return SessionIdentifier - a new session identifier
     */
    SessionIdentifier generateSessionId() const;

    /**
     * @brief Get the Session Object Path object
     *
     * @return const std::string - object path of session for specified
     *         identifier
     */
    const std::string getSessionObjectPath(SessionIdentifier) const;

    /**
     * @brief Get the Session Manager Object Path object
     *
     * @return const std::string - object path of session manager for specified
     *         identifier
     */
    const std::string getSessionManagerObjectPath() const;

    /**
     * @brief Cast session identifier to the hex view.
     *
     * @return const std::string - a hex view of session identifier.
     */
    static const std::string hexSessionId(SessionIdentifier);

    /**
     * @brief Cast session id hex view to the SessionIdentifier.
     *
     * @throw std::invalid_argument exception
     * @throw std::out_of_range exception
     *
     * @return SessionIdentifier
     */
    static SessionIdentifier parseSessionId(const std::string);

    /**
     * @brief Find all session objects
     *
     * @throw std::exception failure on search item object
     *
     * @return const DBusSubTreeOut - session objects dictionary with
     *         appropriate dbus Service, Interfaces.
     */
    const DBusSubTreeOut findSessionItemObjects() const;

    /**
     * @brief Close session by specified object path
     *
     * @param serviceName   - session object service name
     * @param objectPath    - session object path to close
     *
     * @throw std::exception failure on deleting item object
     */
    void callCloseSession(const std::string& serviceName,
                          const std::string& objectPath) const;

    /**
     * @brief Retrieve session details
     *
     * @param serviceName   - session object service name
     * @param objectPath    - session object path
     *
     * @throw std::exception failure on retrieving session details
     */
    const DBusSessionDetailsMap
        getSessionDetails(const std::string& serviceName,
                          const std::string& objectPath) const;

  private:
    sdbusplus::bus::bus& bus;
    const std::string slug;
    const std::string serviceName;
    const SessionType type;

    using SessionItemDict = std::map<SessionIdentifier, SessionItemUni>;
    SessionItemDict sessionItems;
};
} // namespace session
} // namespace obmc
