// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2021 YADRO

#include <errno.h>
#include <stdio.h>

#include <phosphor-logging/elog-errors.hpp>
#include <phosphor-logging/log.hpp>

#include <libobmcsession/obmcsession.hpp>
#include <manager.hpp>
#include <session.hpp>

#include <iostream>

using namespace obmc::session;
using namespace phosphor::logging;

static SessionManagerPtr managerPtr;

/** The session info public struct descriptor */
typedef struct
{
    obmcsesSessionId id;
    char username[64];
    char address[24];
    obmcsessType type;
} __attribute__((aligned(1))) SessionInfo;

/**
 * @brief Copy data from internal session info to session info descriptor.
 *
 * @param internalSessionInfo   - the internal session info
 * @param sessionInfo           - the session info public descriptor
 *
 * @return int                  - errno status
 */
inline int copySessionInfo(
    const SessionManager::InternalSessionInfo& internalSessionInfo,
    SessionInfo* sessionInfo);

int obmcsesManagerInit(sd_bus* bus, const char* slug, obmcsessType type)
{
    if (managerPtr)
    {
        return EEXIST;
    }
    if (!bus)
    {
        return EINVAL;
    }
    try
    {
        managerPtr = std::make_shared<SessionManager>(
            sdbusplus::bus::bus(bus), slug,
            static_cast<SessionManager::SessionType>(type));
    }
    catch (...)
    {
        return ENOMEM;
    }
    return 0;
}

int obmcsesManagerInitAsio(sdbusplus::bus::bus& bus, const std::string& slug,
                           obmcsessType type)
{
    try
    {
        managerPtr = std::make_shared<SessionManager>(
            bus, slug, static_cast<SessionManager::SessionType>(type));
    }
    catch (...)
    {
        return ENOMEM;
    }
    return 0;
}

void obmcsesManagerClose()
{
    if (managerPtr)
    {
        managerPtr.reset();
    }
}

int obmcsesCreate(const char* userName, const char* remoteAddress,
                  obmcsesSessionId* sessionId)
{
    if (!userName || !sessionId)
    {
        return EINVAL;
    }
    if (!managerPtr)
    {
        return ENOENT;
    }
    try
    {
        *sessionId = managerPtr->create(userName, remoteAddress);
    }
    catch (...)
    {
        return EPERM;
    }
    return 0;
}

int obmcsesCreateTransaction(obmcsesSessionId* sessionId)
{
    if (!sessionId)
    {
        return EINVAL;
    }
    if (!managerPtr)
    {
        return ENOENT;
    }
    try
    {
        *sessionId = managerPtr->startTransaction();
    }
    catch (...)
    {
        managerPtr->resetPendginSessionBuild();
        return EPERM;
    }
    return 0;
}

int obmcsesCreateTransactionWithCleanup(obmcsesCleanupFn cleanupFn,
                                        obmcsesSessionId* sessionId)
{
    log<level::DEBUG>(
        "Call to start a new session build transaction (c-proxy)");

    if (!sessionId || !cleanupFn)
    {
        log<level::ERR>("Fail to start a new session build transaction "
                        "(c-proxy): invalid argument");
        return EINVAL;
    }
    if (!managerPtr)
    {
        log<level::ERR>(
            "Fail to start a new session build transaction (c-proxy): "
            "manager not initialized");
        return ENOENT;
    }
    try
    {
        *sessionId = managerPtr->startTransaction(
            [cleanupFn](SessionIdentifier id) -> bool {
                return cleanupFn(id);
            });
    }
    catch (const std::exception& ex)
    {
        log<level::ERR>(
            "Fail to start a new session build transaction (c-proxy)",
            entry("ERROR=%s", ex.what()));
        managerPtr->resetPendginSessionBuild();
        return EPERM;
    }

    log<level::INFO>(
        "Start a new session build transaction (c-proxy): success");
    return 0;
}

int obmcsesCreateTransactionWithFCleanup(SessionCleanupFn&& cleanupFn,
                                         obmcsesSessionId* sessionId)
{

    log<level::DEBUG>(
        "Call to start a new session build transaction");
    if (!sessionId || !cleanupFn)
    {
        log<level::ERR>(
            "Fail to start a new session build transaction: invalid argument");
        return EINVAL;
    }
    if (!managerPtr)
    {
        log<level::ERR>("Fail to start a new session build transaction: "
                        "manager not initialized");
        return ENOENT;
    }
    try
    {
        *sessionId = managerPtr->startTransaction(
            std::forward<SessionCleanupFn>(cleanupFn));
    }
    catch (std::exception& ex)
    {
        log<level::ERR>("Fail to start a new session build transaction",
                        entry("ERROR=%s", ex.what()));
        managerPtr->resetPendginSessionBuild();
        return EPERM;
    }

    log<level::INFO>("Start a new session build transaction: success");
    return 0;
}

int obmcsesCreateWithCleanup(const char* userName, const char* remoteAddress,
                             obmcsesCleanupFn cleanupFn,
                             obmcsesSessionId* sessionId)
{
    if (!sessionId || !userName || !cleanupFn)
    {
        return EINVAL;
    }
    if (!managerPtr)
    {
        return ENOENT;
    }
    try
    {
        *sessionId = managerPtr->create(
            userName, remoteAddress, [cleanupFn](SessionIdentifier id) -> bool {
                return cleanupFn(id);
            });
    }
    catch (...)
    {
        return EPERM;
    }

    return 0;
}

int obmcsesCreateWithFCleanup(const std::string& userName,
                              const std::string& remoteAddress,
                              SessionCleanupFn&& cleanupFn,
                              obmcsesSessionId* sessionId)
{
    if (!cleanupFn)
    {
        return EINVAL;
    }
    if (!managerPtr)
    {
        return ENOENT;
    }
    try
    {
        *sessionId = managerPtr->create(
            userName, remoteAddress, std::forward<SessionCleanupFn>(cleanupFn));
    }
    catch (...)
    {
        return EPERM;
    }

    return 0;
}

int obmcsesCommitSessionBuild(const char* username, const char* remoteIPAddr)
{
    log<level::DEBUG>("Finalize a new session build transaction.");
    if (!username || !remoteIPAddr)
    {
        log<level::ERR>("Fail to commit a session build transaction. An "
                        "invalid argument has been specified.",
                        entry("ARGSPTR=(%p, %p, %p)", username));
        return EINVAL;
    }
    if (!managerPtr)
    {
        log<level::ERR>("Fail to commit a new session build transaction: "
                        "manager not initialized");
        return ENOENT;
    }
    try
    {
        managerPtr->commitSessionBuild(username, remoteIPAddr);
    }
    catch (const std::exception& ex)
    {
        managerPtr->resetPendginSessionBuild();
        log<level::ERR>(
            "Fail to commit a session build transaction (remotelly)",
            entry("ERRMSG=%s", ex.what()));
        return EPERM;
    }

    log<level::INFO>(
        "Successful to finalize a new session build transaction.",
        entry("USERNAME=%s", username), entry("REMOTEIP=%s", remoteIPAddr));

    return 0;
}

int obmcsesCommitSessionBuildRemote(sd_bus* bus, const char* slug,
                                    const char* username,
                                    const char* remoteIPAddr)
{
    log<level::DEBUG>(
        "Finalize a new session build transaction (remotelly, c-proxy).");
    if (!bus || !slug || !username || !remoteIPAddr)
    {
        log<level::ERR>("Fail to commit a session build transaction "
                        "(remotelly, c-proxy). Specified an invalid argument.",
                        entry("ARGSPTR=(%p, %p, %p, %p)", bus, slug, username,
                              remoteIPAddr));
        return EINVAL;
    }

    try
    {
        sdbusplus::bus::bus localDbus(bus);
        SessionManager::commitSessionBuild(localDbus, slug, username,
                                           remoteIPAddr);
        localDbus.release();
    }
    catch (const std::exception& ex)
    {
        log<level::ERR>(
            "Fail to commit a session build transaction (remotelly, c-proxy)",
            entry("ERRMSG=%s", ex.what()));
        return EPERM;
    }

    log<level::INFO>(
        "Successfully created new session via remote commit of session build "
        "transaction (remotelly, c-proxy).",
        entry("SVCSLUG=%s", slug), entry("USERNAME=%s", username),
        entry("REMOTEIP=%s", remoteIPAddr));
    return 0;
}

int obmcsesCommitSessionRemoteAsio(sdbusplus::bus::bus& bus,
                                   const std::string& slug,
                                   const std::string& username,
                                   const std::string& remoteIPAddr)
{
    log<level::DEBUG>("Finalize a new session build transaction (remotelly).");
    try
    {
        SessionManager::commitSessionBuild(bus, slug, username, remoteIPAddr);
    }
    catch (const std::exception& ex)
    {
        log<level::ERR>(
            "Fail to commit a session build transaction (remotelly, c-proxy)",
            entry("ERRMSG=%s", ex.what()));
        return EPERM;
    }

    log<level::INFO>(
        "Successfully created new session via remote commit of session build "
        "transaction (remotelly).",
        entry("SVCSLUG=%s", slug.c_str()), entry("USERNAME=%s", username.c_str()),
        entry("REMOTEIP=%s", remoteIPAddr.c_str()));
    return 0;
}

int obmcsesGetPtrToHandle(const sessObmcInfoHandle sessionInfoHandle,
                          size_t index, sessObmcInfoHandle* pSessionInfoHandle)
{
    if (!sessionInfoHandle || !pSessionInfoHandle)
    {
        return EINVAL;
    }

    auto* sessionList = reinterpret_cast<SessionInfo*>(sessionInfoHandle);
    if (!sessionList)
    {
        return EINVAL;
    }

    *pSessionInfoHandle =
        reinterpret_cast<sessObmcInfoHandle>(&(sessionList[index]));

    return 0;
}

int obmcsesGetSessionDetails(const sessObmcInfoHandle handle,
                             obmcsesSessionId* sessionId, const char** username,
                             const char** address, obmcsessType* type)
{
    if (!handle)
    {
        return EINVAL;
    }
    if (!managerPtr)
    {
        return ENOENT;
    }
    try
    {
        const auto* info = reinterpret_cast<const SessionInfo*>(handle);

        if (!info)
        {
            return EINVAL;
        }
        if (sessionId)
        {
            *sessionId = info->id;
        }
        if (username)
        {
            *username = info->username;
        }
        if (address)
        {
            *address = info->address;
        }
        if (type)
        {
            *type = static_cast<obmcsessType>(info->type);
        }
    }
    catch (...)
    {
        return EPERM;
    }
    return 0;
}

inline int copySessionInfo(
    const SessionManager::InternalSessionInfo& internalSessionInfo,
    SessionInfo* sessionInfo)
{
    if (!sessionInfo)
    {
        return EINVAL;
    }
    sessionInfo->id = internalSessionInfo.id;
    sessionInfo->type = static_cast<obmcsessType>(internalSessionInfo.type);
    strcpy(sessionInfo->username, internalSessionInfo.username.c_str());
    strcpy(sessionInfo->address, internalSessionInfo.remoteAddress.c_str());
    return 0;
}

int obmcsesGetSessionInfo(obmcsesSessionId sessionId,
                          sessObmcInfoHandle* pSessionInfo)
{
    if (!pSessionInfo)
    {
        return EINVAL;
    }
    if (!managerPtr)
    {
        return ENOENT;
    }
    try
    {
        SessionManager::InternalSessionInfo internalSessionInfo;
        SessionInfo* sessionInfo;
        managerPtr->getSessionInfo(sessionId, internalSessionInfo);
        sessionInfo =
            reinterpret_cast<SessionInfo*>(malloc(sizeof(SessionInfo)));
        memset(sessionInfo, 0, sizeof(sessionInfo));
        if (!sessionInfo ||
            (0 != copySessionInfo(internalSessionInfo, sessionInfo)))
        {
            return ENOMEM;
        }

        *pSessionInfo = sessionInfo;
    }
    catch (...)
    {
        return EPERM;
    }
    return 0;
}

int obmcsesGetSessionsList(sessObmcInfoHandle* sessionInfoList, size_t* count)
{
    if (!sessionInfoList || !count)
    {
        return EINVAL;
    }
    if (!managerPtr)
    {
        return ENOENT;
    }
    try
    {
        SessionManager::InternalSessionInfoList sessionsList;
        managerPtr->getAllSessions(sessionsList);
        *count = sessionsList.size();
        auto* internalList = reinterpret_cast<SessionInfo*>(
            malloc((*count) * sizeof(SessionInfo)));
        memset(internalList, 0, (*count) * sizeof(SessionInfo));
        if (!internalList)
        {
            return ENOMEM;
        }
        size_t index = 0;
        for (auto it : sessionsList)
        {
            auto* internalInfo = internalList + index;
            if (0 != copySessionInfo(it.second, &(*internalInfo)))
            {
                continue;
            }
            index++;
        }
        *sessionInfoList = reinterpret_cast<sessObmcInfoHandle>(internalList);
    }
    catch (std::exception& e)
    {
        log<level::ERR>("Fail to obtaining an sessions list.",
                        entry("ERROR=%s", e.what()));
        return EPERM;
    }
    return 0;
}

int obmcsesReleaseSessionHandle(sessObmcInfoHandle sessionInfoHandle)
{
    if (!sessionInfoHandle)
    {
        return EINVAL;
    }

    free(sessionInfoHandle);

    return 0;
}

OBMCBool obmcsesRemove(obmcsesSessionId sessionId)
{
    if (!managerPtr)
    {
        return false;
    }

    try
    {
        return managerPtr->remove(sessionId);
    }
    catch (const std::exception&)
    {}

    return false;
}

OBMCBool obmcsesRemoveWithoutCleanup(obmcsesSessionId sessionId)
{
    if (!managerPtr)
    {
        return false;
    }

    try
    {
        return managerPtr->remove(sessionId, false);
    }
    catch (const std::exception&)
    {}

    return false;
}

unsigned int obmcsesRemoveAllByUser(const char* userName)
{
    if (!managerPtr)
    {
        return 0U;
    }
    try
    {
        return managerPtr->removeAll(userName);
    }
    catch (const std::exception&)
    {}
    return 0U;
}

unsigned int obmcsesRemoveAllByAddress(const char* remoteAddress)
{
    if (!managerPtr)
    {
        return 0U;
    }
    try
    {
        return managerPtr->removeAllByRemoteAddress(remoteAddress);
    }
    catch (const std::exception&)
    {}
    return 0U;
}

unsigned int obmcsesRemoveAllByType(obmcsessType type)
{
    if (!managerPtr)
    {
        return 0U;
    }
    try
    {
        return managerPtr->removeAll(
            static_cast<SessionManager::SessionType>(type));
    }
    catch (const std::exception&)
    {}
    return 0U;
}

unsigned int obmcsesRemoveAll()
{
    if (!managerPtr)
    {
        return 0U;
    }
    try
    {
        return managerPtr->removeAll();
    }
    catch (const std::exception&)
    {}
    return 0U;
}

OBMCBool obmcsesIsSessionBuildPending()
{
    if (!managerPtr)
    {
        return false;
    }
    try
    {
        return managerPtr->isSessionBuildPending();
    }
    catch (const std::exception&)
    {}
    return false;
}

void obmcsesResetPendginSessionBuild()
{
    if (!managerPtr)
    {
        return;
    }
    try
    {
        managerPtr->resetPendginSessionBuild();
    }
    catch (const std::exception&)
    {}
}

obmcsesSessionId obmcsesSessionIdFromString(const char* sessionId)
{
    try
    {
        return SessionManager::parseSessionId(sessionId);
    }
    catch (...)
    {
        return 0;
    }
}
