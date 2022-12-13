// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <vector>

#include <unistd.h>

struct ProcessGroup
{
    pid_t leader = -1;
    pid_t foreground = -1;
    std::vector<pid_t> background;
};
