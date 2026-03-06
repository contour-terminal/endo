// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file Wakeup.hpp
/// @brief Cross-platform wakeup primitive for inter-thread signaling.

#include <platform/Types.hpp>

namespace endo::platform
{

/// Platform-independent wakeup primitive for cross-thread signaling.
///
/// On Linux: wraps an eventfd (optimal, single fd).
/// On other UNIX: wraps a self-pipe pair.
/// On Windows: wraps a manual-reset Event (HANDLE).
///
/// Usage: one thread calls signal() to wake another thread that is
/// blocked in TerminalInput::poll() or WaitForMultipleObjects().
class Wakeup
{
  public:
    Wakeup();
    ~Wakeup();

    Wakeup(Wakeup const&) = delete;
    Wakeup& operator=(Wakeup const&) = delete;
    Wakeup(Wakeup&&) = delete;
    Wakeup& operator=(Wakeup&&) = delete;

    /// @brief Signals the wakeup, unblocking any thread waiting on it.
    void signal() const;

    /// @brief Resets the signaled state (called after draining).
    void reset() const;

    /// @brief Returns the platform-native handle for poll()/WaitForMultipleObjects().
    [[nodiscard]] auto nativeHandle() const noexcept -> NativeHandle;

  private:
    NativeHandle _handle = InvalidHandle;
#if !defined(_WIN32) && !defined(__linux__)
    NativeHandle _writeFd = InvalidHandle; ///< Write end for self-pipe (non-Linux UNIX).
#endif
};

} // namespace endo::platform
