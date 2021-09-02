
/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2021 YADRO.
 */
#pragma once

#include <functional>
#include <libobmcsession/obmcsession_proto.h>

using SessionIdentifier = std::size_t;
using SessionCleanupFn = std::function<bool(SessionIdentifier)>;
