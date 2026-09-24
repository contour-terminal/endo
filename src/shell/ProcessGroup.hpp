// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <core/platform/Types.hpp>

#include <vector>

namespace endo
{

/// Represents a group of related processes.
///
/// A process group contains a leader process and optionally
/// foreground and background processes.
struct ProcessGroup
{
    core::platform::ProcessId leader = core::platform::InvalidProcessId;     ///< Leader process ID
    core::platform::ProcessId foreground = core::platform::InvalidProcessId; ///< Foreground process ID
    std::vector<core::platform::ProcessId> background;                       ///< Background process IDs
};

} // namespace endo
