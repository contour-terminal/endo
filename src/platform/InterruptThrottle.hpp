// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>

#include <platform/SignalHandler.hpp>

namespace endo::platform
{

/// Throttled, cooperative interrupt poll for long-running builtin loops.
///
/// Ctrl+C handling is cooperative: a loop must periodically drain pending OS
/// signals (so the Linux signalfd flag is updated; a no-op where the Windows
/// console handler sets the flag directly) and check whether a SIGINT/Ctrl+C is
/// pending. This helper centralizes that idiom — previously copy-pasted across
/// many builtins — and amortizes the (cheap but non-free) signal drain by only
/// performing it every @c interval calls.
///
/// Usage:
/// @code
/// auto throttle = InterruptThrottle{};
/// for (auto const& item: lazySequence)
/// {
///     if (throttle.pending())
///         return 130; // 128 + SIGINT
///     // ... process item ...
/// }
/// @endcode
class InterruptThrottle
{
  public:
    /// Default number of calls between actual signal polls. Tuned for tight,
    /// cheap loops (directory scans, line matching) where draining the signalfd
    /// every iteration would add measurable overhead.
    static constexpr std::size_t defaultInterval = 256;

    /// Constructs a throttle.
    /// @param interval Number of @ref pending calls between real polls. Pass 1
    ///                 for loops whose per-iteration body already performs real
    ///                 I/O (file copy/remove), where polling every time is free
    ///                 relative to the work and maximizes responsiveness.
    explicit InterruptThrottle(std::size_t interval = defaultInterval) noexcept: _interval(interval) {}

    /// Polls for a pending interrupt at the throttled cadence and consumes it.
    ///
    /// Polls on the very first call and then every @c interval calls (the counter
    /// is post-incremented from 0, so 0 % interval == 0 fires immediately). This
    /// matters for short loops: with a pre-increment a loop running fewer than
    /// @c interval iterations would never poll at all and could ignore Ctrl+C
    /// entirely. On the calls where it actually polls, it drains pending signals
    /// and, if a SIGINT/Ctrl+C is pending, consumes it (clears the flag so the
    /// next REPL prompt is not spuriously interrupted) and returns true. The
    /// caller should then abort its loop and return exit code 130.
    ///
    /// @return true if an interrupt was pending (and has been consumed).
    [[nodiscard]] bool pending() noexcept
    {
        if (!polledAndPending())
            return false;
        SignalHandler::clearPendingSigint();
        return true;
    }

    /// Polls for a pending interrupt at the throttled cadence WITHOUT consuming it.
    ///
    /// Same throttled drain-and-check as @ref pending, but the SIGINT flag is left
    /// set. Use this from library code that polls deep inside a call tree and
    /// returns partial results: the flag stays set so the driver can observe it
    /// after the helper returns, consume it once (via
    /// @ref SignalHandler::clearPendingSigint), and exit with code 130.
    ///
    /// @return true if an interrupt is pending (and has been left pending).
    [[nodiscard]] bool peek() noexcept { return polledAndPending(); }

  private:
    /// Drains pending signals at the throttled cadence and reports whether a
    /// SIGINT/Ctrl+C is pending, without clearing it. Shared by @ref pending and
    /// @ref peek so both honor the same cadence and first-call semantics.
    [[nodiscard]] bool polledAndPending() noexcept
    {
        if (_counter++ % _interval != 0)
            return false;
        SignalHandler::processSignalFd();
        return SignalHandler::hasPendingSigint();
    }

    std::size_t _interval;
    std::size_t _counter = 0;
};

} // namespace endo::platform

namespace endo
{
using endo::platform::InterruptThrottle;
} // namespace endo
