// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file
/// A headless @c EventSource that multiplexes only user-registered file
/// descriptors — no terminal, no agent/interrupt wakeups, no signal fd.
///
/// This is the reactor substrate for non-interactive coroutine work driven by a
/// @c TuiRuntime: an async socket server (the `httpServe` builtin) or any flow
/// that only needs to `co_await runtime.waitReadable(fd)` / `waitWritable(fd)` and
/// timers. Where @c TerminalEventSource couples the wait to a TTY, this source
/// blocks purely on the attached fds (and the runtime's timer-derived timeout), so
/// the same @c TuiRuntime scheduler drives a server with no terminal attached.

#include <tui/runtime/EventSource.hpp>

#include <cstdint>
#include <vector>

#include <platform/Types.hpp>

namespace tui::runtime
{

/// An @c EventSource whose wait set is exactly the user-registered fds.
///
/// Cross-platform: POSIX uses `poll(2)`, Windows uses `WaitForMultipleObjects`.
/// When nothing is registered, a positive timeout sleeps and a negative timeout is
/// clamped to a finite wait so the loop cannot block forever with no wakeable
/// source (the runtime only calls `wait()` while something is parked, and a flow
/// parked solely on a timer supplies a finite timeout).
class PollEventSource: public EventSource
{
  public:
    PollEventSource() = default;

    [[nodiscard]] WaitOutcome wait(int timeoutMs) override;

    [[nodiscard]] FdToken attach(endo::platform::NativeHandle fd, FdInterest interest) override
    {
        return _registry.attach(fd, interest);
    }

    void detach(FdToken token) override { _registry.detach(token); }

    /// @return The number of fds currently attached.
    [[nodiscard]] std::size_t attachedCount() const noexcept { return _registry.size(); }

  private:
    FdRegistry _registry; ///< Watched fds, in registration order.
};

} // namespace tui::runtime
