// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cassert>
#include <cstdlib>
#include <string>

#define COREVM_ASSERT(cond, msg)                           \
    if (!(cond))                                           \
    {                                                      \
        fprintf(stderr, "%s\n", std::string(msg).c_str()); \
        abort();                                           \
    }
