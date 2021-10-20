/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2021 YADRO.
 */

#pragma once

#include <dbus.hpp>
#include <xyz/openbmc_project/Session/Item/server.hpp>
#include <xyz/openbmc_project/Session/Build/server.hpp>

#include <libobmcsession/obmcsession_proto.hpp>

#include <atomic>
#include <condition_variable>
#include <thread>
#include <chrono>

namespace obmc
{
namespace session
{

using SessionItemServer =
    sdbusplus::xyz::openbmc_project::Session::server::Item;
using SessionBuildServer =
    sdbusplus::xyz::openbmc_project::Session::server::Build;

class SessionManager;
class SessionItem;

using SessionItemPtr = std::shared_ptr<SessionItem>;
using SessionManagerPtr = std::shared_ptr<SessionManager>;
using SessionManagerWeakPtr = std::weak_ptr<SessionManager>;

using namespace obmc::dbus;

class SessionManager final :
    public SessionBuildServer,
    public std::enable_shared_from_this<SessionManager>
{
    static constexpr const char* serviceNameStartSegment =
        "xyz.openbmc_project.Session.";
    static constexpr const char* sessionManagerObjectPath =
        "/xyz/openbmc_project/session_manager";

    struct NullDeleter{
        void operator()(sdbusplus::bus::bus*){}
    };
  public:
    using SessionType = SessionItemServer::Type;

    struct InternalSessionInfo 
    {
        SessionIdentifier id;
        std::string username;
        std::string remoteAddress;
        SessionType type;
        std::string serviceName;
        std::string objectPath;
        bool isOwn;
    };
    using InternalSessionInfoList =
        std::map<SessionIdentifier, InternalSessionInfo>;

    SessionManager() = delete;
    ~SessionManager()
    {
        // The stored pointer must be released without deleter because we store
        // reference as a pointer.
        bus.release();
    }
    SessionManager(const SessionManager&) = delete;
    SessionManager& operator=(const SessionManager&) = delete;
    SessionManager(SessionManager&&) = delete;
    SessionManager& operator=(SessionManager&&) = delete;

    /** @brief Constructs session manager
     *
     * @param[in] bus     - Handle to system dbus
     * @param[in] slug    - The Dbus service slug uniquely identifying the source
     *                      of a session to create the appropriate items from.
     *                      Service name template:
     *                      'xyz.openbmc_project.Session.${slug}'.
     * @param[in] type    - Type of all session items that will be created by
     *                      the current instance.
     */
    SessionManager(sdbusplus::bus::bus& bus, const std::string& slug,
                   const SessionType type);

    /** @brief Constructs session manager
     *
     * @param[in] bus     - Handle to system dbus
     * @param[in] slug    - The Dbus service slug uniquely identifying the source
     *                      of a session to create the appropriate items from.
     *                      Service name template:
     *                      'xyz.openbmc_project.Session.${slug}'.
     * @param[in] type    - Type of all session items that will be created by
     *                      the current instance.
     */
    SessionManager(sdbusplus::bus::bus&& bus, const std::string& slug,
                   const SessionType type);

    /**
     * @brief Create a session and publish into the dbus.
     *
     * @param userName              - the owner user name
     * @param remoteAddress         - the IP address of the session initiator.
     *
     * @throw logic_error           - Build new session is locked.
     * @return SessionIdentifier    - unique session ID
     */
    SessionIdentifier create(const std::string& userName,
                             const std::string& remoteAddress);

    /**
     * @brief Create a session and publish into the dbus without appropriate
     *        session payload
     *
     * @note The session is incomplete. Metadata of session must be set for 10
     *       seconds. Otherwise, the current session will be closed. All other
     *       new session creates requests will be rejected until the current
     *       created session accepts metadata or is removed by timeout.
     *
     * @throw logic_error           - Build new session is locked.
     *
     * @return SessionIdentifier    - unique session ID
     */
    SessionIdentifier startTransaction();

    /**
     * @brief Create a session and publish into the dbus without appropriate
     *        session payload
     *
     * @param cleanupFn             - the callback to cleanup session on
     *                                destroy.
     *
     * @note The session is incomplete. Metadata of session must be set for 10
     *       seconds. Otherwise, the current session will be closed. All other
     *       new session creates requests will be rejected until the current
     *       created session accepts metadata or is removed by timeout.
     *
     * @throw logic_error           - Build new session is locked.
     *
     * @return SessionIdentifier    - unique session ID
     */
    SessionIdentifier startTransaction(SessionCleanupFn&& cleanupFn);

    /**
     * @brief Create a session and publish into the dbus with cleanup callback
     *        on the session destroy.
     *
     * @param userName              - the owner user name
     * @param remoteAddress         - the IP address of the session initiator.
     * @param cleanupFn             - the callback to cleanup session on destroy.
     *
     * @throw logic_error           - Build new session is locked.
     *
     * @return SessionIdentifier    - unique session ID
     */
    SessionIdentifier create(const std::string& userName,
                             const std::string& remoteAddress,
                             SessionCleanupFn&& cleanupFn);

    // /**
    //  * @brief  Start the session build transaction.
    //  *
    //  * @note No concurent transaction available, any new starting transaction
    //  *       reqiests will be rejected with `NotAllowed` error.
    //  *       When transaction is started the target session service will wait
    //  *       for commit for 20 seconds.
    //  *
    //  *  @param[in] username         - Owner username of the session
    //  *  @param[in] remoteIPAddr     - Remote IP address.
    //  */
    // void startTransaction(const std::string username,
    //                       const std::string remoteIPAddr) override;

    // /** 
    //  * @brief Finalize building a new session with predefined metadata.
    //  */
    // void commit() override;

    /** @brief Commit pending session build.
     *
     *  @param[in] username     - the owner username of the session.
     *  @param[in] remoteIPAddr - the IP address of the session initiator.
     */
    void commitSessionBuild(std::string username,
                            std::string remoteIPAddr) override;

    /** @brief Commit pending session build from remote service.
     *
     *  @param[in] bus         - handle to system dbus
     *  @param[in] slug         - the slug of service that is managing target
     *                            session.
     *  @param[in] username     - the owner username of the session.
     *  @param[in] remoteIPAddr - the IP address of the session initiator.
     */
    static void commitSessionBuild(sdbusplus::bus::bus& bus, std::string slug,
                                   std::string username, std::string remoteIPAddr);

    /**
     * @brief Remove a dbus session object from storage and unpublish it from
     *        dbus.
     *
     * @param sessionId     - unique session ID to remove from storage
     * @param withCleanup   - specifies whether to call configured cleanup
     *                        routine on the session removing.
     * @param localLookup   - specifies whether search session object only at
     *                        current session manager or globally.
     *
     * @return true         - success
     * @return false        - fail
     */
    bool remove(SessionIdentifier sessionId, bool withCleanup = true,
                bool localLookup = false);

    /**
     * @brief Remove all sessions associated with the specified user.
     *
     * @param userName      - username to close appropriate sessions
     *
     * @throw std::runtime_error not implemented
     *
     * @return std::size_t  - count of closed sessions
     */
    std::size_t removeAll(const std::string& userName);

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
    std::size_t removeAllByRemoteAddress(const std::string& remoteAddress);

    /**
     * @brief Remove all sessions of specified type.
     *
     * @param type         - the type of session to close.
     *
     * @throw std::runtime_error not implemented
     *
     * @return std::size_t - count of closed sessions
     */
    std::size_t removeAll(SessionType type);

    /**
     * @brief Unconditional removes all opened sessions.
     *
     * @throw std::runtime_error not implemented
     *
     * @return std::size_t - count of closed sessions
     */
    std::size_t removeAll();

    /**
     * @brief Get transaction status of session building
     *
     * @return true - transaction is process
     * @return false - transaction not started
     */
    bool isSessionBuildPending() const;

    /**
     * @brief Reset the active session build transaction
     *
     */
    void resetPendginSessionBuild();

    /**
     * @brief Get list of all session with detailed information.
     *
     * @throw std::exception    - failure to retrieve sessions list
     */
    void getAllSessions(InternalSessionInfoList& sessionsList) const;

    /**
     * @brief Get session details by session identifier.
     *
     * @param[in] sessionId         - session identifier
     * @param[out] sessionInfo      - the internal session info
     *
     * @throw std::invalid_argument - session not found
     * @throw std::runtime_error    - failure to retrieve session by specified ID.
     */
    void getSessionInfo(SessionIdentifier id, InternalSessionInfo& sessionInfo) const;

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
     * @brief Get the Session Object Path object
     *
     * @return const std::string - object path of session for specified
     *         identifier
     */
    const std::string getSessionObjectPath(SessionIdentifier) const;
  protected:
    friend class SessionItem;

    /**
     * @brief Generate new session depened on current session manager slug and
     *        timestamp.
     *
     * @return SessionIdentifier - a new session identifier
     */
    SessionIdentifier generateSessionId() const;

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
     * @brief Find external session objects
     *
     * @throw std::exception failure on search item object
     *
     * @return const InternalSessionInfoList - session objects dictionary with
     *         appropriate internal list of session info.
     */
    const DBusSubTreeOut findSessionItemObjects() const;

    /**
     * @brief Close session by specified object path
     *
     * @param serviceName   - session object service name
     * @param objectPath    - session object path to close
     * @param withCleanup   - Specifies whether to call configured cleanup
     *                        routine on the session removing.
     *
     * @throw std::exception failure on deleting item object
     */
    void callCloseSession(const std::string& serviceName,
                          const std::string& objectPath,
                          bool withCleanup = true) const;

    /**
     * @brief Retrieve sessions list with detailed information
     *
     * @param serviceName   - session object service name
     * @param objectPath    - session object path
     *
     * @throw std::exception failure on retrieving session details
     */
    const DBusSessionDetailsMap
        getSessionsProperties(const std::string& serviceName,
                          const std::string& objectPath) const;

    /**
     * @brief Start timer to observe timeout of finilize session build
     *        transaction.
     *
     * @param sessionId
     */
    void sessionBuildTimerStart(SessionIdentifier sessionId);

    /**
     * @brief Finalize session build transaction.
     *
     */
    void sessionBuildSucess();

    /**
     * @brief Get the list of all Session in the InternalSessionInfo view
     *
     * @param[in] sessionSubTree     - the DBus subtree of all session
     *                                 objects
     * @param[out] sessionsList      - found sessions
     * @param[in] listSearchSessions - (optional) the list session identifiers
     *                                 that the search is handled for.
     *
     * @throw std::exception         - failure to retrieve the sessions info
     *                                 list
     */
    void getSessionsInfo(const DBusSubTreeOut& sessionSubTree,
                         InternalSessionInfoList& sessionsList,
                         std::optional<std::vector<SessionIdentifier>>
                             listSearchingSessions = std::nullopt) const;
  private:
    std::unique_ptr<sdbusplus::bus::bus> bus;
    std::unique_ptr<sdbusplus::server::manager::manager> dbusManager;
    const std::string slug;
    const std::string serviceName;
    const SessionType type;

    std::thread timerSessionComplete;
    std::atomic<bool> pendingSessionBuild;
    SessionIdentifier pendingSessionId;
    
    std::condition_variable cvTransaction;
    std::mutex cvmTransaction;

    using SessionItemDict = std::map<SessionIdentifier, SessionItemPtr>;
    SessionItemDict sessionItems;
};
} // namespace session
} // namespace obmc
