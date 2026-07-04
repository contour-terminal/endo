// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file
/// A scripted @c EventSource for deterministic @c TuiRuntime unit tests.

#include <tui/runtime/EventSource.hpp>

#include <cstdint>
#include <deque>
#include <vector>

#include <platform/Types.hpp>

namespace tui::runtime::testing
{

/// An @c EventSource that returns a pre-scripted sequence of wait outcomes.
///
/// Each `wait()` pops and returns the next scripted outcome and records the
/// timeout it was called with. When the script is exhausted it returns an
/// interrupt outcome, so a runtime driving a never-completing flow terminates
/// instead of hanging the test.
///
/// The fd registry is modelled too: @c attach hands back synthetic, monotonically
/// increasing tokens (no real fds), so tests can script readiness on a given
/// token via @c pushReadable / @c pushWritable and drive the readiness awaitables
/// deterministically.
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

    /// Appends an outcome marking @p token readable.
    /// @param token A token previously handed out by @c attach.
    void pushReadable(FdToken token) { _scripted.push_back(WaitOutcome { .readyRead = { token } }); }

    /// Appends an outcome marking @p token writable.
    /// @param token A token previously handed out by @c attach.
    void pushWritable(FdToken token) { _scripted.push_back(WaitOutcome { .readyWrite = { token } }); }

    /// @return The timeouts passed to each `wait()` call, in order.
    [[nodiscard]] std::vector<int> const& recordedTimeouts() const noexcept { return _timeouts; }

    /// @return How many times `wait()` was invoked.
    [[nodiscard]] std::size_t waitCount() const noexcept { return _timeouts.size(); }

    /// @return The number of fds currently attached (after attach/detach).
    [[nodiscard]] std::size_t attachedCount() const noexcept { return _attached; }

    WaitOutcome wait(int timeoutMs) override
    {
        _timeouts.push_back(timeoutMs);
        if (_scripted.empty())
            return WaitOutcome { .interrupted = true }; // Backstop so tests cannot hang.
        auto outcome = std::move(_scripted.front());
        _scripted.pop_front();
        return outcome;
    }

    FdToken attach(endo::platform::NativeHandle /*fd*/, FdInterest /*interest*/) override
    {
        ++_attached;
        return FdToken { ++_nextToken };
    }

    void detach(FdToken token) override
    {
        if (token && _attached > 0)
            --_attached;
    }

  private:
    std::deque<WaitOutcome> _scripted;
    std::vector<int> _timeouts;
    std::uint64_t _nextToken = 0; ///< Source of synthetic, never-zero tokens.
    std::size_t _attached = 0;    ///< Live attach()-minus-detach() count.
};

} // namespace tui::runtime::testing
