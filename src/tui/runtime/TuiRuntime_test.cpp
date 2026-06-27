// SPDX-License-Identifier: Apache-2.0
#include <tui/InputEvent.hpp>
#include <tui/runtime/TuiRuntime.hpp>
#include <tui/runtime/testing/MockEventSource.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <ranges>
#include <vector>

#include <coro/Cancellation.hpp>
#include <coro/Task.hpp>

using endo::coro::OperationCancelled;
using endo::coro::Task;
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

TEST_CASE("A non-input activity wake resumes a timed input waiter with no event", "[TuiRuntime]")
{
    auto source = MockEventSource {};
    source.pushActivity(); // focus change / finished job — no input event
    auto runtime = TuiRuntime { source };

    // awaitEventForResult returns 0 when the wait resumes without an event.
    auto const result = runtime.blockOn(awaitEventForResult(&runtime, 500));

    REQUIRE(result == 0);
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
