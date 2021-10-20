
/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2021 YADRO.
 */

#pragma once

#include <libobmcsession/obmcsession.h>

#include <libobmcsession/obmcsession_proto.hpp>
#include <sdbusplus/bus.hpp>

/** @brief Constructs session manager
 *
 * @param[in] bus           - Handle to system dbus
 * @param[in] slug          - The Dbus service slug uniquely identifying the
 *                            source of a session to create the appropriate
 *                            items from. Service name template:
 *                            'xyz.openbmc_project.Session.${slug}'.
 * @param[in] type          - Type of all session items that will be created
 * by the current instance.
 * @return int              - session manager initialization status
 */
extern int obmcsesManagerInitAsio(sdbusplus::bus::bus& bus,
                                  const std::string& slug, obmcsessType type);

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
extern int obmcsesCreateTransactionWithFCleanup(SessionCleanupFn&& cleanupFn,
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
extern int obmcsesCreateWithFCleanup(const std::string& userName,
                                     const std::string& remoteAddress,
                                     SessionCleanupFn&& cleanupFn,
                                     obmcsesSessionId* sessionId);

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
extern int obmcsesCommitSessionRemoteAsio(sdbusplus::bus::bus& bus,
                                          const std::string& slug,
                                          const std::string& username,
                                          const std::string& remoteIPAddr);
