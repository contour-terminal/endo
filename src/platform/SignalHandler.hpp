// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <atomic>

#include <platform/Types.hpp>

namespace endo::platform
{

/// Abstract callback interface for signal notifications.
///
/// Implement this interface to receive signal callbacks from SignalHandler.
/// The shell implements this to handle job control signals.
class SignalCallback
{
  public:
    virtual ~SignalCallback() = default;

    /// Called when SIGCHLD is received (child process state change).
    virtual void onSigchld() = 0;

    /// Called when SIGTSTP is received (terminal stop, e.g. Ctrl+Z).
    virtual void onSigtstp() = 0;

    /// Called when SIGCONT is received (continue after stop).
    virtual void onSigcont() = 0;
};

/// Signal handling using signalfd (Linux) or traditional handlers (other POSIX).
///
/// On Linux, we use signalfd to convert SIGCHLD, SIGTSTP, and SIGCONT signals
/// to file descriptor events, which eliminates race conditions and allows
/// integration with poll().
///
/// On macOS/BSD, we use traditional signal handlers with atomic flags.
///
/// Supported signals:
/// - SIGCHLD: Child process state change (exit, stop, continue)
/// - SIGTSTP: Terminal stop signal (Ctrl+Z from parent shell or kill -TSTP)
/// - SIGCONT: Continue after stop (when resumed via fg or kill -CONT)
class SignalHandler
{
  public:
    /// Initializes signal handling.
    ///
    /// @param callback Pointer to the callback receiver for signal notifications
    /// @return signalfd file descriptor on Linux (for poll), -1 on other platforms
    [[nodiscard]] static int initialize(SignalCallback* callback);

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

    /// Checks if SIGTSTP was received (for platforms without signalfd).
    [[nodiscard]] static bool hasPendingSigtstp() noexcept;

    /// Clears the pending SIGTSTP flag.
    static void clearPendingSigtstp() noexcept;

    /// Checks if SIGCONT was received (for platforms without signalfd).
    [[nodiscard]] static bool hasPendingSigcont() noexcept;

    /// Clears the pending SIGCONT flag.
    static void clearPendingSigcont() noexcept;

    /// Temporarily restores default SIGTSTP handling and re-raises the signal.
    ///
    /// This is called during shell suspend to actually stop the process.
    /// After the process is resumed (SIGCONT), the custom handler is reinstalled.
    static void suspendSelf();

  private:
    static SignalCallback* _callback;
    static int _signalFd;

#if !defined(__linux__)
    /// Traditional signal handlers for non-Linux platforms.
    static void sigchldHandler(int sig);
    static void sigtstpHandler(int sig);
    static void sigcontHandler(int sig);
    static std::atomic<bool> _sigchldPending;
    static std::atomic<bool> _sigtstpPending;
    static std::atomic<bool> _sigcontPending;
#endif
};

} // namespace endo::platform

// Backward-compatible aliases
namespace endo
{
using endo::platform::SignalCallback;
using endo::platform::SignalHandler;
} // namespace endo
