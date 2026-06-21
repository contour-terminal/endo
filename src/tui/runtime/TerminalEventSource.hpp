// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file
/// The production @c EventSource backing the TUI runtime: it multiplexes
/// terminal input, resize, the agent-message wakeup, the interrupt wakeup, and
/// (on POSIX) the signal fd into one blocking wait.

#include <tui/Terminal.hpp>
#include <tui/runtime/EventSource.hpp>

#include <utility>

#include <platform/SignalHandler.hpp>
#include <platform/Types.hpp>
#include <platform/Wakeup.hpp>

namespace tui::runtime
{

/// Multiplexes every real input/wakeup source the runtime cares about.
///
/// Waiting includes the agent and interrupt wakeups in the OS wait set, so an
/// agent message or Ctrl+C wakes the pump immediately rather than at the next
/// timeout — closing the Windows gap where @c TerminalInput::poll() never
/// waited on the agent wakeup. SIGINT detection is unified through
/// @c SignalHandler so the same interrupt path works on every platform.
///
/// Protocol-response events (color-scheme / focus / cursor-position / cell-size)
/// are consumed here exactly as @c Terminal::poll() does, so they never surface
/// as application input and the terminal's focus/color-scheme handlers keep
/// firing.
class TerminalEventSource: public EventSource
{
  public:
    /// @param terminal The terminal (provides input handles, decode, and report handlers; not owned).
    /// @param agentWakeup Wakeup the agent worker signals on a new message, or nullptr.
    /// @param interruptWakeup Wakeup signalled by @c SignalHandler on SIGINT, or nullptr.
    /// @param signalFd POSIX signal fd to also wait on, or @c InvalidHandle.
    TerminalEventSource(Terminal& terminal,
                        endo::platform::Wakeup* agentWakeup = nullptr,
                        endo::platform::Wakeup* interruptWakeup = nullptr,
                        endo::platform::NativeHandle signalFd = endo::platform::InvalidHandle) noexcept:
        _terminal(terminal), _agentWakeup(agentWakeup), _interruptWakeup(interruptWakeup), _signalFd(signalFd)
    {
    }

    [[nodiscard]] WaitOutcome wait(int timeoutMs) override;

  private:
    /// Consumes protocol-response events (via @c Terminal::consumeProtocolReports
    /// so the policy lives in one place) and folds a pending SIGINT into the
    /// outcome, clearing the flag so the runtime observes each interrupt once. A
    /// dispatched focus change is reported as @c activity so an idle waiter wakes
    /// and can redraw (e.g. the prompt's focus-dim chrome).
    /// @param outcome The outcome assembled from the wait so far.
    /// @return @p outcome with reports stripped, @c interrupted set if a SIGINT
    ///         is pending, and @c activity set if focus changed.
    [[nodiscard]] WaitOutcome finalize(WaitOutcome outcome)
    {
        if (_terminal.consumeProtocolReports(outcome.events))
            outcome.activity = true;
        if (endo::platform::SignalHandler::hasPendingSigint())
        {
            endo::platform::SignalHandler::clearPendingSigint();
            outcome.interrupted = true;
        }
        return outcome;
    }

    Terminal& _terminal;                      ///< Provides input handles, decode, report handlers.
    endo::platform::Wakeup* _agentWakeup;     ///< Agent-message wakeup, or nullptr.
    endo::platform::Wakeup* _interruptWakeup; ///< Interrupt wakeup, or nullptr.
    endo::platform::NativeHandle _signalFd;   ///< POSIX signal fd, or InvalidHandle.
};

} // namespace tui::runtime
