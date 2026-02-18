// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <vector>

#include <platform/Types.hpp>

namespace endo
{

/// Represents a group of related processes.
///
/// A process group contains a leader process and optionally
/// foreground and background processes.
struct ProcessGroup
{
    ProcessId leader = InvalidProcessId;     ///< Leader process ID
    ProcessId foreground = InvalidProcessId; ///< Foreground process ID
    std::vector<ProcessId> background;       ///< Background process IDs
};

} // namespace endo
