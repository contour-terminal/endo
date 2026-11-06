// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <mutex>

namespace endo::platform
{

/// @brief The lock that keeps one child's inheritable handles away from another child.
///
/// A handle created inheritable for one child is inherited by EVERY process this process starts
/// with `bInheritHandles` set until it is closed, and `WindowsProcessManager::spawn` starts jobs that
/// way. So `captureOutput`, which runs on the prompt's worker thread, holds this from creating its
/// child's handles until it has closed its copies of them, and `spawn` holds it around
/// `CreateProcessW`; a job started at that moment would otherwise inherit the capture's pipe and keep
/// its reader from ever seeing the end.
///
/// Process-wide rather than injected, deliberately: the handle table it guards is the process's own,
/// and there is one of it whatever the object graph looks like.
/// @return The one lock, for the life of the process.
[[nodiscard]] std::mutex& handleInheritanceMutex() noexcept;

} // namespace endo::platform
