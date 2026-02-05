// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <atomic>

#include "Platform.hpp"

namespace endo
{

class Shell;

/// Signal handling using signalfd (Linux) or traditional handlers (other POSIX).
///
/// On Linux, we use signalfd to convert SIGCHLD signals to file descriptor events,
/// which eliminates race conditions and allows integration with poll().
///
/// On macOS/BSD, we use traditional signal handlers with atomic flags.
class SignalHandler
{
  public:
    /// Initializes signal handling for the shell.
    ///
    /// @param shell Pointer to the shell instance for callbacks
    /// @return signalfd file descriptor on Linux (for poll), -1 on other platforms
    [[nodiscard]] static int initialize(Shell* shell);

    /// Restores default signal handling.
    static void restore();

    /// Returns the signalfd file descriptor (-1 if not using signalfd).
    [[nodiscard]] static int signalFd() noexcept;

    /// Processes pending signals from signalfd (call when fd is readable).
    ///
    /// @return true if any signals were processed
    static bool processSignalFd();

    /// Processes pending signals for non-signalfd platforms.
    /// Call this periodically in the main loop.
    static void processPendingSignals();

    /// Checks if SIGCHLD was received (for platforms without signalfd).
    [[nodiscard]] static bool hasPendingSigchld() noexcept;

    /// Clears the pending SIGCHLD flag.
    static void clearPendingSigchld() noexcept;

  private:
    static Shell* _shell;
    static int _signalFd;

#if !defined(__linux__)
    /// Traditional signal handler for non-Linux platforms.
    static void sigchldHandler(int sig);
    static std::atomic<bool> _sigchldPending;
#endif
};

} // namespace endo
