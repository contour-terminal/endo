// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file
/// A scripted @c EventSource for deterministic @c TuiRuntime unit tests.

#include <tui/runtime/EventSource.hpp>

#include <deque>
#include <vector>

namespace tui::runtime::testing
{

/// An @c EventSource that returns a pre-scripted sequence of wait outcomes.
///
/// Each `wait()` pops and returns the next scripted outcome and records the
/// timeout it was called with. When the script is exhausted it returns an
/// interrupt outcome, so a runtime driving a never-completing flow terminates
/// instead of hanging the test.
class MockEventSource: public EventSource
{
  public:
    /// Appends an outcome carrying the given input events.
    /// @param events The events the next wait should surface.
    void pushEvents(std::vector<InputEvent> events)
    {
        _scripted.push_back(WaitOutcome { .events = std::move(events) });
    }

    /// Appends an outcome signalling the agent-message wakeup fired.
    void pushAgentReady() { _scripted.push_back(WaitOutcome { .agentReady = true }); }

    /// Appends an outcome signalling an interrupt (SIGINT / Ctrl+C).
    void pushInterrupt() { _scripted.push_back(WaitOutcome { .interrupted = true }); }

    /// Appends a bare timeout outcome (nothing happened).
    void pushTimeout() { _scripted.push_back(WaitOutcome {}); }

    /// Appends an outcome signalling a non-input activity wake (focus change /
    /// finished job) with no input event.
    void pushActivity() { _scripted.push_back(WaitOutcome { .activity = true }); }

    /// @return The timeouts passed to each `wait()` call, in order.
    [[nodiscard]] std::vector<int> const& recordedTimeouts() const noexcept { return _timeouts; }

    /// @return How many times `wait()` was invoked.
    [[nodiscard]] std::size_t waitCount() const noexcept { return _timeouts.size(); }

    WaitOutcome wait(int timeoutMs) override
    {
        _timeouts.push_back(timeoutMs);
        if (_scripted.empty())
            return WaitOutcome { .interrupted = true }; // Backstop so tests cannot hang.
        auto outcome = std::move(_scripted.front());
        _scripted.pop_front();
        return outcome;
    }

  private:
    std::deque<WaitOutcome> _scripted;
    std::vector<int> _timeouts;
};

} // namespace tui::runtime::testing
