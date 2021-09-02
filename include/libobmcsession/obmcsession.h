/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2021 YADRO.
 */

#pragma once

#if (defined __cplusplus)
extern "C"
{
#endif

#include <libobmcsession/obmcsession_proto.h>
#include <systemd/sd-bus.h>

/** @brief Constructs session manager
 *
 * @param[in] bus           - Handle to system dbus
 * @param[in] objPath       - The Dbus service slug uniquely identifying the
 *                            source of a session to create the appropriate
 *                            items from. Service name template:
 *                            'xyz.openbmc_project.Session.${slug}'.
 * @param[in] type          - Type of all session items that will be created
 *                            by the current instance.
 * @return int              - session manager initialization status
 */
extern int obmcsesManagerInit(sd_bus* bus, const char* slug,
                                obmcsessType type);

/**
 * @brief Deinitialize session manager, release all accepted resources.
 * 
 * @return int 
 */
extern void obmcsesManagerClose();

/**
 * @brief Create a session and publish into the dbus.
 *
 * @param[in] userName      - the owner user name
 * @param[in] remoteAddress - the IP address of the session initiator.
 * @param[out] sessionId    - unique session ID
 *
 * @return int              - session build status
 */
extern int obmcsesCreate(const char* userName, const char* remoteAddress,
                            obmcsesSessionId* sessionId);

/**
 * @brief Create a session and publish into the dbus without appropriate
 *        session payload
 *
 * @param[out] sessionId    - unique session ID
 *
 * @note The session is incomplete. Metadata of session must be set for 10
 *       seconds. Otherwise, the current session will be closed. All other
 *       new session creates requests will be rejected until the current
 *       created session accepts metadata or is removed by timeout.
 *
 * @return int              - session build status
 */
extern int obmcsesCreateTransaction(obmcsesSessionId* sessionId);

/**
 * @brief Create a session and publish into the dbus without appropriate
 *        session payload
 *
 * @param[in] cleanupFn             - the callback to cleanup session on
 *                                    destroy.
 * @param[out] SessionIdentifier    - unique session ID
 *
 * @note The session is incomplete. Metadata of session must be set for 10
 *       seconds. Otherwise, the current session will be closed. All other
 *       new session creates requests will be rejected until the current
 *       created session accepts metadata or is removed by timeout.
 *
 * @return int                      - session build status
 */
extern int obmcsesCreateTransactionWithCleanup(obmcsesCleanupFn cleanupFn,
                                                obmcsesSessionId* sessionId);

/**
 * @brief Create a session and publish into the dbus with cleanup callback
 *        on the session destroy.
 *
 * @param[in] userName              - the owner user name
 * @param[in] remoteAddress         - the IP address of the session
 *                                    initiator.
 * @param[in] cleanupFn             - the callback to cleanup session on
 *                                    destroy.
 * @param[out] SessionIdentifier    - unique session ID
 *
 * @return int                      - session build status
 */
extern int obmcsesCreateWithCleanup(const char* userName,
                                    const char* remoteAddress,
                                    obmcsesCleanupFn cleanupFn,
                                    obmcsesSessionId* sessionId);

/** @brief Commit the pending session build that is handle by the current
 *         manager.
 *
 *  @param[in] username     - the owner username of the session.
 *  @param[in] remoteIPAddr - the IP address of the session initiator.
 *
 *  @return int             - session build status
 */
extern int obmcsesCommitSessionBuild(const char* username,
                                        const char* remoteIPAddr);

/** @brief Commit pending session build from remote service.
 *
 *  @param[in] bus          - handle to system dbus
 *  @param[in] slug         - the slug of service that is managing target
 *                            session.
 *  @param[in] username     - the owner username of the session.
 *  @param[in] remoteIPAddr - the IP address of the session initiator.
 *
 *  @return int             - session build status
 */
extern int obmcsesCommitSessionBuildRemote(sd_bus* bus, const char* slug,
                                            const char* username,
                                            const char* remoteIPAddr);

/**
 * @brief Get sessions info descriptor
 *
 * @param[in] sessionId     - session identifier
 * @param[out] pSessionInfo - pointer to the session info descriptor.
 *
 * @note The 'pSessionInfo' param memory is allocated internally. The client
 *       takes responsibility for freed the obtained outputs.
 *
 * @return int              - status of retrieving session info descriptor.
 */
extern int obmcsesGetSessionInfo(obmcsesSessionId sessionId,
                                    sessObmcInfoHandle* pSessionInfo);

/**
 * @brief Get sessions info descriptors list
 *
 * @param[out] pSessionInfoList - pointer to array of session info
 *                                descriptors.
 * @param[out] count            - count of retrieving array items
 * @return int                  - status of retrieving descriptors list
 */
extern int obmcsesGetSessionsList(sessObmcInfoHandle* pSessionInfoList,
                                    size_t* count);

/**
 * @brief Release the memory of the sessions info.
 *
 * @param[in] sessionInfoHandle - pointer to session info handle.
 * @param[in] count             - count of array items
 * @return int                  - freeing status of specified handle.
 * 
 */
extern int obmcsesReleaseSessionHandle(sessObmcInfoHandle sessionInfoHandle);

/**
 * @brief Retrieve the session info from descriptor list by specified index
 *
 * @param[in] sessionInfoHandle     - pointer to array of session info
 *                                    descriptors.
 * @param[in] index                 - index if item to retrieve from list
 * @param[out] pSessionInfoHandle   - index if item to retrieve from list
 * @return int                      - errno status of obtaining session info
 */
extern int obmcsesGetPtrToHandle(const sessObmcInfoHandle sessionInfoHandle,
                                 size_t index,
                                 sessObmcInfoHandle* pSessionInfoHandle);

/**
 * @brief Get session details by session info descriptor.
 *
 * @param[in] handle            - session info descriptor
 * @param[out] sessionId        - session identifier
 * @param[out] username         - the owner username of the session.
 * @param[out] address          - the IP address of the session initiator.
 * @param[out] type             - the type of session
 * @return int                  - status of retrieving session details
 *
 * @note Specify the `nullptr` if you don't want to retrieve some output
 *       field.
 */
extern int obmcsesGetSessionDetails(const sessObmcInfoHandle handle,
                                    obmcsesSessionId* sessionId,
                                    char const** username, char const** address,
                                    obmcsessType* type);

/**
 * @brief Remove a dbus session object from storage and unpublish it from
 *        dbus.
 *
 * @param[in] sessionId     - unique session ID to remove from storage
 * 
 * @return true             - success
 * @return false            - fail
 */
extern OBMCBool obmcsesRemove(obmcsesSessionId sessionId);

/**
 * @brief Remove a dbus session object from storage and unpublish it from
 *        dbus call configured cleanup routine 
 *
 * @param[in] sessionId     - unique session ID to remove from storage
 * 
 * @return true             - success
 * @return false            - fail
 */
extern OBMCBool obmcsesRemoveWithoutCleanup(obmcsesSessionId sessionId);

/**
 * @brief Remove all sessions associated with the specified user.
 *
 * @param userName      - username to close appropriate sessions
 *
 * @return std::size_t  - count of closed sessions
 */
extern unsigned int obmcsesRemoveAllByUser(const char* userName);

/**
 * @brief Remove all sessions which have been opened from the specified IPv4
 *        address.
 *
 * @param remoteAddress - the IP address of the session initiator..
 *
 * @return std::size_t  - count of closed sessions
 */
extern unsigned int obmcsesRemoveAllByAddress(const char* remoteAddress);

/**
 * @brief Remove all sessions of specified type.
 *
 * @param type         - the type of session to close.
 *
 * @return std::size_t - count of closed sessions
 */
extern unsigned int obmcsesRemoveAllByType(obmcsessType type);

/**
 * @brief Unconditional removes all opened sessions.
 *
 * @return std::size_t - count of closed sessions
 */
extern unsigned int obmcsesRemoveAll();

/**
 * @brief Get transaction status of session building
 *
 * @return true - transaction is process
 * @return false - transaction not started
 */
extern OBMCBool obmcsesIsSessionBuildPending();

/**
 * @brief Reset the active session build transaction
 *
 */
extern void obmcsesResetPendginSessionBuild();

/**
 * @brief convert session identifier from string to obmcsesSessionId
 *
 * @param sessionId
 * @return obmcsesSessionId
 */
extern obmcsesSessionId obmcsesSessionIdFromString(const char* sessionId);

#if (defined __cplusplus)
}
#endif
