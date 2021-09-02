
/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2021 YADRO.
 */
#pragma once

#if (defined __cplusplus)
extern "C"
{
#endif

#ifndef bool
#define OBMCBool int
#else
#define OBMCBool bool
#endif

typedef long unsigned int obmcsesSessionId;
typedef OBMCBool (*obmcsesCleanupFn)(obmcsesSessionId id);
typedef void* sessObmcInfoHandle;

/** session types */
typedef enum obmcsessType {
    obmcsessTypeHostConsole,
    obmcsessTypeIPMI,
    obmcsessTypeKVMIP,
    obmcsessTypeManagerConsole,
    obmcsessTypeRedfish,
    obmcsessTypeVirtualMedia,
    obmcsessTypeWebUI,
    obmcsessTypeNBD
} obmcsessType;

#if (defined __cplusplus)
}
#endif
