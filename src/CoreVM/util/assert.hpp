// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cassert>
#include <cstdlib>
#include <print>
#include <string>

#define COREVM_ASSERT(cond, msg)                              \
    if (!(cond))                                              \
    {                                                         \
        std::println(stderr, "{}", std::string(msg).c_str()); \
        abort();                                              \
    }
