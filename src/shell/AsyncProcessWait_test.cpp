// SPDX-License-Identifier: Apache-2.0
#include <shell/AsyncProcessWait.hpp>

#include <tui/runtime/PollEventSource.hpp>
#include <tui/runtime/TuiRuntime.hpp>
#include <tui/runtime/WithTimeout.hpp>
#include <tui/runtime/testing/MockEventSource.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>

#include <coro/Cancellation.hpp>
#include <coro/Task.hpp>
#include <coro/WhenAny.hpp>
#include <platform/Clock.hpp>
#include <platform/testing/MockProcessManager.hpp>

using namespace std::chrono_literals;

using endo::platform::ManualClock;
using endo::platform::WaitResult;
using endo::platform::testing::MockProcessManager;
using endo::shell::waitProcessAsync;
using tui::runtime::TuiRuntime;
using tui::runtime::testing::MockEventSource;

namespace
{

/// The `WaitFlag::NoHang` "still running" sentinel returned by
/// `PosixProcessManager::wait` (and mimicked here): exit code -1, not signalled,
/// not stopped.
constexpr WaitResult StillRunning { .exitCode = -1 };

/// A @c MockEventSource that advances an injected @c ManualClock by a fixed step on
/// every wait(), so a `delay` scheduled against that clock elapses after a known
/// number of waits with no real sleeping. Mirrors the helper in TuiRuntime_test.
class ClockAdvancingSource: public MockEventSource
{
  public:
    ClockAdvancingSource(ManualClock& clock, std::chrono::milliseconds step) noexcept:
        _clock(clock), _step(step)
    {
    }

    tui::runtime::WaitOutcome wait(int timeoutMs) override
    {
        _clock.advance(_step);
        return MockEventSource::wait(timeoutMs);
    }

  private:
    ManualClock& _clock;
    std::chrono::milliseconds _step;
};

/// Awaits @c waitProcessAsync with a 20ms poll interval.
endo::coro::Task<WaitResult> runWait(TuiRuntime* runtime,
                                     MockProcessManager* pm,
                                     endo::platform::ProcessId pid)
{
    co_return co_await waitProcessAsync(runtime, pm, pid, 20ms);
}

/// A whenAny arm that never finishes on its own (the child never exits), so it
/// only completes by being cancelled as a losing sibling.
endo::coro::Task<void> neverEndingArm(TuiRuntime* runtime, MockProcessManager* pm)
{
    (void) co_await waitProcessAsync(runtime, pm, 1, 20ms);
}

/// A whenAny arm that completes immediately.
endo::coro::Task<void> immediateArm()
{
    co_return;
}

/// Races @c neverEndingArm against @c immediateArm and returns the winner index.
/// A named (non-capturing) coroutine so the closure lifetime is not a concern.
endo::coro::Task<std::size_t> raceNeverEndingVsImmediate(TuiRuntime* runtime, MockProcessManager* pm)
{
    co_return co_await endo::coro::whenAny(neverEndingArm(runtime, pm), immediateArm());
}

} // namespace

TEST_CASE("waitProcessAsync polls until the child exits", "[AsyncProcessWait]")
{
    // The mock reports "still running" for the first two polls, then a real exit.
    // The delay between polls is driven by the ManualClock so no real time passes:
    // each scripted wait advances the clock past the 20ms poll interval, resuming
    // the parked delay.
    auto clock = ManualClock {};
    auto source = ClockAdvancingSource { clock, 25ms };
    source.pushTimeout(); // resumes the 1st inter-poll delay
    source.pushTimeout(); // resumes the 2nd inter-poll delay
    auto runtime = TuiRuntime { source, clock };

    auto pm = MockProcessManager {};
    int pollCount = 0;
    pm.onWait([&](endo::platform::ProcessId,
                  endo::platform::WaitFlags) -> std::expected<WaitResult, endo::platform::PlatformError> {
        ++pollCount;
        if (pollCount < 3)
            return StillRunning;
        return WaitResult { .exitCode = 7 };
    });

    auto const result = runtime.blockOn(runWait(&runtime, &pm, 1234));

    CHECK(result.exitCode == 7);
    CHECK(pollCount == 3); // two "still running" polls + the exit
}

TEST_CASE("waitProcessAsync returns immediately when the child has already exited", "[AsyncProcessWait]")
{
    auto source = MockEventSource {};
    auto runtime = TuiRuntime { source };

    auto pm = MockProcessManager {};
    pm.onWait([](endo::platform::ProcessId, endo::platform::WaitFlags) {
        return std::expected<WaitResult, endo::platform::PlatformError> { WaitResult { .exitCode = 0 } };
    });

    auto const result = runtime.blockOn(runWait(&runtime, &pm, 1));
    CHECK(result.exitCode == 0);
}

TEST_CASE("waitProcessAsync treats a wait error as the child being gone", "[AsyncProcessWait]")
{
    auto source = MockEventSource {};
    auto runtime = TuiRuntime { source };

    auto pm = MockProcessManager {};
    pm.onWait([](endo::platform::ProcessId, endo::platform::WaitFlags) {
        return std::expected<WaitResult, endo::platform::PlatformError> {
            std::unexpect, endo::platform::PlatformError::WaitFailed
        };
    });

    // ECHILD-style failure resolves to a benign terminated result, not an error.
    auto const result = runtime.blockOn(runWait(&runtime, &pm, 1));
    CHECK(result.exitCode == 128);
}

TEST_CASE("withTimeout cancels a never-exiting waitProcessAsync", "[AsyncProcessWait][timeout]")
{
    // The child never exits (always "still running"), so only the timeout arm can
    // win. A real PollEventSource + short real timeout drives the runtime's timer
    // (matching the reference withTimeout test); the parked poll-delay is cancelled
    // via the stop-callback and unwinds, so withTimeout yields nullopt.
    auto source = tui::runtime::PollEventSource {};
    auto runtime = TuiRuntime { source };

    auto pm = MockProcessManager {};
    pm.onWait([](endo::platform::ProcessId, endo::platform::WaitFlags) {
        return std::expected<WaitResult, endo::platform::PlatformError> { StillRunning };
    });

    auto const finished =
        runtime.blockOn(tui::runtime::withTimeout(&runtime, waitProcessAsync(&runtime, &pm, 1, 5ms), 20ms));

    CHECK_FALSE(finished.has_value()); // timed out → work cancelled
}

TEST_CASE("waitProcessAsync loses a whenAny race and unwinds", "[AsyncProcessWait][cancel]")
{
    // Race the never-exiting wait against a task that completes immediately; the
    // immediate task wins, the wait is cancelled, and whenAny reports index 1.
    auto source = MockEventSource {};
    auto runtime = TuiRuntime { source };

    auto pm = MockProcessManager {};
    pm.onWait([](endo::platform::ProcessId, endo::platform::WaitFlags) {
        return std::expected<WaitResult, endo::platform::PlatformError> { StillRunning };
    });

    auto const winner = runtime.blockOn(raceNeverEndingVsImmediate(&runtime, &pm));

    CHECK(winner == 1); // the immediate arm won; the wait unwound as a loser
}
