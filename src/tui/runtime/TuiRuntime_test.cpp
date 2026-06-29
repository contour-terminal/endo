// SPDX-License-Identifier: Apache-2.0
#include <tui/InputEvent.hpp>
#include <tui/runtime/PollEventSource.hpp>
#include <tui/runtime/TuiRuntime.hpp>
#include <tui/runtime/testing/MockEventSource.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <ranges>
#include <vector>

#include <coro/Cancellation.hpp>
#include <coro/Task.hpp>
#include <platform/Clock.hpp>
#include <platform/SystemPipe.hpp>

using endo::coro::OperationCancelled;
using endo::coro::Task;
using endo::platform::ManualClock;
using tui::InputEvent;
using tui::KeyEvent;
using tui::runtime::TuiRuntime;
using tui::runtime::testing::MockEventSource;

namespace
{

/// Returns the codepoint of the next key event the runtime delivers.
Task<char32_t> awaitOneKeyCodepoint(TuiRuntime* runtime)
{
    auto const event = co_await runtime->nextEvent();
    co_return std::get<KeyEvent>(event).codepoint;
}

/// Collects the codepoints of @p count key events in arrival order.
Task<int> sumKeyCodepoints(TuiRuntime* runtime, int count)
{
    auto sum = 0;
    for ([[maybe_unused]] auto const index: std::views::iota(0, count))
    {
        auto const event = co_await runtime->nextEvent();
        sum += static_cast<int>(std::get<KeyEvent>(event).codepoint);
    }
    co_return sum;
}

/// Returns 1 if nextEventFor yielded an event, 0 if it timed out.
Task<int> awaitEventForResult(TuiRuntime* runtime, int timeoutMs)
{
    auto const event = co_await runtime->nextEventFor(std::chrono::milliseconds { timeoutMs });
    co_return event.has_value() ? 1 : 0;
}

/// Returns the kind of the first activity nextActivity observes (as its enum value).
Task<int> awaitActivityKind(TuiRuntime* runtime, int timeoutMs)
{
    auto const activity = co_await runtime->nextActivity(std::chrono::milliseconds { timeoutMs });
    co_return static_cast<int>(activity.kind);
}

/// Returns true once the agent-ready wakeup is observed.
Task<bool> awaitAgentReady(TuiRuntime* runtime)
{
    co_await runtime->nextAgentReady();
    co_return true;
}

/// Awaits input; returns a sentinel if cancelled instead.
Task<int> awaitKeyOrCancel(TuiRuntime* runtime, int cancelSentinel)
{
    try
    {
        auto const event = co_await runtime->nextEvent();
        co_return static_cast<int>(std::get<KeyEvent>(event).codepoint);
    }
    catch (OperationCancelled const&)
    {
        co_return cancelSentinel;
    }
}

/// Resumes immediately when the delay has already elapsed (the ready path).
Task<int> awaitZeroDelay(TuiRuntime* runtime)
{
    co_await runtime->delay(std::chrono::milliseconds { 0 });
    co_return 7;
}

/// Parks on a long delay; returns a sentinel when cancelled.
Task<int> awaitDelayOrCancel(TuiRuntime* runtime, int cancelSentinel)
{
    try
    {
        co_await runtime->delay(std::chrono::milliseconds { 500 });
        co_return 0;
    }
    catch (OperationCancelled const&)
    {
        co_return cancelSentinel;
    }
}

/// Parks on a delay of @p delayMs, then sets *fired and returns it. Used with a
/// ManualClock to prove the timer fires only once the clock crosses the deadline.
Task<int> awaitDelayThenFire(TuiRuntime* runtime, int delayMs, bool* fired)
{
    co_await runtime->delay(std::chrono::milliseconds { delayMs });
    *fired = true;
    co_return delayMs;
}

/// Parks on a delay of @p delayMs; sets *fired if it elapses, or returns
/// @p cancelSentinel if cancelled while parked. Lets a ManualClock test assert
/// the delay did NOT fire when the clock never advanced past the deadline.
Task<int> awaitCustomDelayOrCancel(TuiRuntime* runtime, int delayMs, int cancelSentinel, bool* fired)
{
    try
    {
        co_await runtime->delay(std::chrono::milliseconds { delayMs });
        *fired = true;
        co_return 0;
    }
    catch (OperationCancelled const&)
    {
        co_return cancelSentinel;
    }
}

/// Waits for @p fd to become readable; returns 1 on readiness or @p cancelSentinel
/// if cancelled while parked.
Task<int> awaitReadableOrCancel(TuiRuntime* runtime, endo::platform::NativeHandle fd, int cancelSentinel)
{
    try
    {
        co_await runtime->waitReadable(fd);
        co_return 1;
    }
    catch (OperationCancelled const&)
    {
        co_return cancelSentinel;
    }
}

KeyEvent keyOf(char32_t codepoint)
{
    return KeyEvent { .codepoint = codepoint };
}

/// Sets *destroyed = true when its frame unwinds (RAII), so a test can prove a
/// parked flow was cancelled-and-unwound rather than raw-destroyed.
struct UnwindFlag
{
    bool* destroyed;

    ~UnwindFlag() { *destroyed = true; }
};

/// A spawned flow that takes an RAII guard and then parks forever on input. If
/// the runtime is destroyed while it is parked, the guard must still run.
Task<void> parkForeverWithGuard(TuiRuntime* runtime, bool* destroyed)
{
    *destroyed = false; // explicit write so the pointee is observably non-const
    auto guard = UnwindFlag { destroyed };
    try
    {
        auto const event = co_await runtime->nextEvent();
        static_cast<void>(event);
    }
    catch (OperationCancelled const&)
    {
        // Expected on runtime teardown: the frame unwinds and `guard` destructs,
        // which is exactly what this flow exists to demonstrate.
        static_cast<void>(destroyed);
    }
}

/// Parks on waitReadable with an RAII guard; proves the frame unwinds (guard runs)
/// if the runtime is torn down while the fd wait is parked.
Task<void> waitReadableWithGuard(TuiRuntime* runtime, endo::platform::NativeHandle fd, bool* destroyed)
{
    *destroyed = false;
    auto guard = UnwindFlag { destroyed };
    try
    {
        co_await runtime->waitReadable(fd);
    }
    catch (OperationCancelled const&)
    {
        static_cast<void>(destroyed);
    }
}

} // namespace

TEST_CASE("Runtime delivers a scripted input event to a waiting flow", "[TuiRuntime]")
{
    auto source = MockEventSource {};
    source.pushEvents({ InputEvent { keyOf(U'x') } });
    auto runtime = TuiRuntime { source };

    auto const result = runtime.blockOn(awaitOneKeyCodepoint(&runtime));

    REQUIRE(result == U'x');
}

TEST_CASE("Runtime delivers buffered events in arrival order", "[TuiRuntime]")
{
    auto source = MockEventSource {};
    // Two events arriving in one wait are buffered and consumed in order.
    source.pushEvents({ InputEvent { keyOf(U'a') }, InputEvent { keyOf(U'b') } });
    auto runtime = TuiRuntime { source };

    auto const result = runtime.blockOn(sumKeyCodepoints(&runtime, 2));

    REQUIRE(result == static_cast<int>(U'a') + static_cast<int>(U'b'));
}

TEST_CASE("Runtime resumes a flow when the agent wakeup fires", "[TuiRuntime]")
{
    auto source = MockEventSource {};
    source.pushAgentReady();
    auto runtime = TuiRuntime { source };

    auto const ready = runtime.blockOn(awaitAgentReady(&runtime));

    REQUIRE(ready);
}

TEST_CASE("Protocol reports never surface as input events", "[TuiRuntime]")
{
    auto source = MockEventSource {};
    // A focus report (internal) followed by a real key: the flow must see only the key.
    source.pushEvents({ InputEvent { tui::FocusEvent { .focused = true } }, InputEvent { keyOf(U'z') } });
    auto runtime = TuiRuntime { source };

    auto const result = runtime.blockOn(awaitOneKeyCodepoint(&runtime));

    REQUIRE(result == U'z');
}

TEST_CASE("nextEventFor yields an event that arrives before the timeout", "[TuiRuntime]")
{
    auto source = MockEventSource {};
    source.pushEvents({ InputEvent { keyOf(U'e') } });
    auto runtime = TuiRuntime { source };

    auto const result = runtime.blockOn(awaitEventForResult(&runtime, 500));

    REQUIRE(result == 1);
}

TEST_CASE("nextEventFor returns nullopt when its timeout elapses", "[TuiRuntime]")
{
    auto source = MockEventSource {};
    source.pushTimeout(); // wait returns with no event; the 0ms deadline has passed
    auto runtime = TuiRuntime { source };

    auto const result = runtime.blockOn(awaitEventForResult(&runtime, 0));

    REQUIRE(result == 0);
}

TEST_CASE("nextActivity reports an input event, an agent message, or a timeout", "[TuiRuntime]")
{
    using tui::runtime::ActivityKind;

    SECTION("input event")
    {
        auto source = MockEventSource {};
        source.pushEvents({ InputEvent { keyOf(U'a') } });
        auto runtime = TuiRuntime { source };
        REQUIRE(runtime.blockOn(awaitActivityKind(&runtime, 500)) == static_cast<int>(ActivityKind::Event));
    }
    SECTION("agent message")
    {
        auto source = MockEventSource {};
        source.pushAgentReady();
        auto runtime = TuiRuntime { source };
        REQUIRE(runtime.blockOn(awaitActivityKind(&runtime, 500))
                == static_cast<int>(ActivityKind::AgentReady));
    }
    SECTION("timeout")
    {
        auto source = MockEventSource {};
        source.pushTimeout();
        auto runtime = TuiRuntime { source };
        REQUIRE(runtime.blockOn(awaitActivityKind(&runtime, 0)) == static_cast<int>(ActivityKind::Timeout));
    }
}

TEST_CASE("Interrupt cancels a flow parked on input", "[TuiRuntime]")
{
    auto source = MockEventSource {};
    source.pushInterrupt();
    auto runtime = TuiRuntime { source };

    constexpr auto Sentinel = -99;
    auto const result = runtime.blockOn(awaitKeyOrCancel(&runtime, Sentinel));

    REQUIRE(result == Sentinel);
}

TEST_CASE("delay(0) resumes without waiting", "[TuiRuntime]")
{
    auto source = MockEventSource {};
    auto runtime = TuiRuntime { source };

    auto const result = runtime.blockOn(awaitZeroDelay(&runtime));

    REQUIRE(result == 7);
    REQUIRE(source.waitCount() == 0); // ready path never blocks
}

TEST_CASE("A pending delay bounds the wait timeout", "[TuiRuntime]")
{
    auto source = MockEventSource {};
    source.pushInterrupt(); // end the test before the 500ms elapses
    auto runtime = TuiRuntime { source };

    constexpr auto Sentinel = -7;
    auto const result = runtime.blockOn(awaitDelayOrCancel(&runtime, Sentinel));

    REQUIRE(result == Sentinel);
    REQUIRE(source.waitCount() >= 1);
    // The first wait is bounded by the pending timer, not an indefinite (-1) block.
    auto const firstTimeout = source.recordedTimeouts().front();
    REQUIRE(firstTimeout > 0);
    REQUIRE(firstTimeout <= 500);
}

TEST_CASE("An injected ManualClock makes a delay's computed timeout exact", "[TuiRuntime][clock]")
{
    // With time frozen, computeTimeoutMs has no real-clock jitter: a delay(100ms)
    // must yield exactly 100 as the bounding timeout on the first wait.
    auto source = MockEventSource {};
    source.pushInterrupt(); // unblock the test (the timer never fires; clock is frozen)
    auto clock = ManualClock {};
    auto runtime = TuiRuntime { source, clock };

    auto fired = false;
    constexpr auto Sentinel = -3;
    // The delay parks; the interrupt then cancels it (delay throws OperationCancelled).
    auto const result = runtime.blockOn(awaitCustomDelayOrCancel(&runtime, 100, Sentinel, &fired));

    REQUIRE(result == Sentinel);
    REQUIRE_FALSE(fired); // clock never advanced, so the timer never elapsed
    REQUIRE(source.recordedTimeouts().front() == 100);
}

namespace
{

/// A MockEventSource that advances an injected ManualClock by a fixed step on
/// every wait(). This models the passage of time deterministically: the runtime
/// schedules a delay against the clock, and each blocking wait "elapses" exactly
/// `step` of clock time, so a delay fires after a known number of waits — with no
/// real sleeping.
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

} // namespace

TEST_CASE("A delay fires deterministically once the ManualClock crosses its deadline", "[TuiRuntime][clock]")
{
    // The runtime fires expired timers using the injected clock. Each scripted wait
    // advances the ManualClock by 40ms, so a 100ms delay must still be pending after
    // the first two waits (80ms) and fire on the third (120ms) — no real time passes.
    auto clock = ManualClock {};
    auto source = ClockAdvancingSource { clock, std::chrono::milliseconds { 40 } };
    source.pushTimeout(); // 40ms elapsed: still pending
    source.pushTimeout(); // 80ms elapsed: still pending
    source.pushTimeout(); // 120ms elapsed: timer is now due → flow resumes
    auto runtime = TuiRuntime { source, clock };

    auto fired = false;
    auto const result = runtime.blockOn(awaitDelayThenFire(&runtime, 100, &fired));

    REQUIRE(fired);
    REQUIRE(result == 100);
    // The flow resumed on the third wait, not before: time only moved via the clock.
    REQUIRE(source.waitCount() == 3);
}

TEST_CASE("A non-input activity wake resumes a timed input waiter with no event", "[TuiRuntime]")
{
    auto source = MockEventSource {};
    source.pushActivity(); // focus change / finished job — no input event
    auto runtime = TuiRuntime { source };

    // awaitEventForResult returns 0 when the wait resumes without an event.
    auto const result = runtime.blockOn(awaitEventForResult(&runtime, 500));

    REQUIRE(result == 0);
}

TEST_CASE("EventSource fd registry hands out distinct tokens and reports readiness", "[EventSource]")
{
    auto source = MockEventSource {};
    auto const a = source.attach(7, tui::runtime::FdInterest::Read);
    auto const b = source.attach(8, tui::runtime::FdInterest::Write);

    REQUIRE(static_cast<bool>(a));
    REQUIRE(static_cast<bool>(b));
    REQUIRE_FALSE(a == b);
    REQUIRE(source.attachedCount() == 2);

    source.pushReadable(a);
    source.pushWritable(b);

    auto const first = source.wait(0);
    REQUIRE(first.readyRead.size() == 1);
    REQUIRE(first.readyRead.front() == a);

    auto const second = source.wait(0);
    REQUIRE(second.readyWrite.size() == 1);
    REQUIRE(second.readyWrite.front() == b);

    source.detach(a);
    source.detach(b);
    REQUIRE(source.attachedCount() == 0);
}

TEST_CASE("An invalid FdToken is falsy and equals the invalid sentinel", "[EventSource]")
{
    auto const invalid = tui::runtime::FdToken::invalid();
    REQUIRE_FALSE(static_cast<bool>(invalid));
    REQUIRE(invalid == tui::runtime::FdToken {});
}

TEST_CASE("waitReadable resumes when the registered fd becomes readable", "[TuiRuntime][fd]")
{
    auto source = MockEventSource {};
    // The awaiter attaches the fd during await_suspend (the first wait()), which the
    // mock tokenises as FdToken{1}; script that token readable so the first wait
    // resolves the park.
    source.pushReadable(tui::runtime::FdToken { 1 });
    auto runtime = TuiRuntime { source };

    constexpr auto Cancelled = -1;
    auto const result = runtime.blockOn(awaitReadableOrCancel(&runtime, /*fd=*/42, Cancelled));

    REQUIRE(result == 1);
}

TEST_CASE("waitReadable on an interrupt cancels the parked flow", "[TuiRuntime][fd]")
{
    auto source = MockEventSource {};
    source.pushInterrupt(); // SIGINT while parked → root stop → OperationCancelled
    auto runtime = TuiRuntime { source };

    constexpr auto Cancelled = -7;
    auto const result = runtime.blockOn(awaitReadableOrCancel(&runtime, /*fd=*/42, Cancelled));

    REQUIRE(result == Cancelled);
}

TEST_CASE("waitReadable on an invalid fd resolves immediately as cancelled", "[TuiRuntime][fd]")
{
    auto source = MockEventSource {};
    auto runtime = TuiRuntime { source };

    constexpr auto Cancelled = -3;
    auto const result =
        runtime.blockOn(awaitReadableOrCancel(&runtime, endo::platform::InvalidHandle, Cancelled));

    REQUIRE(result == Cancelled);
    REQUIRE(source.waitCount() == 0); // never blocked: an unwaitable fd resolves inline
}

TEST_CASE("waitReadable resolves over a real SystemPipe via PollEventSource", "[TuiRuntime][fd][poll]")
{
    // End-to-end through the real OS readiness path (poll(2) / WaitForMultipleObjects),
    // not the scripted mock: a SystemPipe whose write end already holds a byte is
    // readable, so a flow parked on waitReadable resolves on the first real wait and
    // reads the byte back. SystemPipe gives a reactor-waitable read end on every
    // platform, so this test runs identically on Linux, macOS, and Windows.
    auto pipe = endo::platform::createSystemPipe();
    REQUIRE(pipe.has_value());

    char const payload = 'Z';
    REQUIRE((*pipe)->write(&payload, 1).has_value());

    auto source = tui::runtime::PollEventSource {};
    auto runtime = TuiRuntime { source };

    auto readByte = [](TuiRuntime* rt, endo::platform::SystemPipe* p) -> Task<char> {
        co_await rt->waitReadable(p->waitHandle());
        char buf = 0;
        auto const got = p->read(&buf, 1);
        co_return (got.has_value() && *got == 1) ? buf : '\0';
    };

    auto const result = runtime.blockOn(readByte(&runtime, pipe->get()));
    REQUIRE(result == 'Z');
    REQUIRE(source.attachedCount() == 0); // the awaiter detached on resume
}

TEST_CASE("Destroying the runtime unwinds a flow parked on waitReadable", "[TuiRuntime][fd]")
{
    auto source = MockEventSource {};
    source.pushTimeout(); // benign wait for the root's post-completion pump
    auto destroyed = false;
    {
        auto runtime = TuiRuntime { source };
        runtime.spawn(waitReadableWithGuard(&runtime, /*fd=*/42, &destroyed));
        runtime.blockOn(awaitZeroDelay(&runtime)); // drive the spawned flow to its park
        REQUIRE_FALSE(destroyed);
    } // ~TuiRuntime: stop + flush fd waiters + drain → the frame unwinds, guard runs

    REQUIRE(destroyed);
}

TEST_CASE("Destroying the runtime unwinds a parked spawned flow via RAII", "[TuiRuntime]")
{
    auto source = MockEventSource {};
    // One benign timeout for the wait that the root's blockOn issues after the root
    // completes; without it the mock's empty-script backstop returns an interrupt
    // that would cancel the spawned flow during blockOn instead of at teardown.
    source.pushTimeout();
    auto destroyed = false;
    {
        auto runtime = TuiRuntime { source };
        runtime.spawn(parkForeverWithGuard(&runtime, &destroyed));
        // Run a trivial root to drive the pump so the spawned flow reaches its first
        // suspension (parked on input) and stays there.
        runtime.blockOn(awaitZeroDelay(&runtime));
        REQUIRE_FALSE(destroyed); // still parked
    } // ~TuiRuntime: request_stop + wake + drain → the flow unwinds, guard runs

    REQUIRE(destroyed);
}
