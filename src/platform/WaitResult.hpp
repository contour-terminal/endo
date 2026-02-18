// SPDX-License-Identifier: Apache-2.0
#pragma once

namespace endo::platform
{

/// Result of waiting for a process to complete.
struct WaitResult
{
    int exitCode = 0;      ///< Exit code of the process
    bool signaled = false; ///< Whether the process was terminated by a signal
    bool stopped = false;  ///< Whether the process was stopped (SIGTSTP/SIGSTOP)
    int signal = 0;        ///< Signal number if signaled or stopped is true
};

} // namespace endo::platform

// Backward-compatible alias in the endo namespace
namespace endo
{
using endo::platform::WaitResult;
} // namespace endo
