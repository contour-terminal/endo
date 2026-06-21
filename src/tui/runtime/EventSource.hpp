// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file
/// The blocking multiplexed wait the TUI runtime drives, abstracted behind an
/// interface so the runtime can be unit-tested with a scripted source instead of
/// a real terminal.

#include <tui/InputEvent.hpp>

#include <vector>

namespace tui::runtime
{

/// What a single multiplexed wait observed: any decoded input events plus flags
/// for the cross-thread / interrupt sources the runtime also selects on.
struct WaitOutcome
{
    std::vector<InputEvent> events; ///< Input/resize/report events decoded this wait (may be empty).
    bool agentReady = false;        ///< The agent message wakeup fired; drain the agent channel.
    bool interrupted = false;       ///< An interrupt (SIGINT / Ctrl+C) was observed.
    bool activity = false; ///< A non-input wake (focus change, finished job) occurred; resume idle waiters.
};

/// Abstraction over "block until something happens, with a timeout".
///
/// The real implementation (@c TerminalEventSource) multiplexes terminal input,
/// the resize notification, the agent-message wakeup, the interrupt wakeup, and
/// (on POSIX) the signal fd. Tests inject a scripted source. This is the single
/// dependency-injection seam between the runtime and the OS: keeping it an
/// interface lets `TuiRuntime` be exercised deterministically.
class EventSource
{
  public:
    EventSource() = default;
    virtual ~EventSource() = default;

    EventSource(EventSource const&) = delete;
    EventSource& operator=(EventSource const&) = delete;
    EventSource(EventSource&&) = delete;
    EventSource& operator=(EventSource&&) = delete;

    /// Blocks until input/resize/agent/interrupt activity or the timeout elapses.
    /// @param timeoutMs -1 to block indefinitely, 0 to poll, >0 to wait that many ms.
    /// @return What was observed during the wait.
    [[nodiscard]] virtual WaitOutcome wait(int timeoutMs) = 0;
};

} // namespace tui::runtime
