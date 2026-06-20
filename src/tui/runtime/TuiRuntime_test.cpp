// SPDX-License-Identifier: Apache-2.0
#include <tui/InputEvent.hpp>
#include <tui/runtime/TuiRuntime.hpp>
#include <tui/runtime/testing/MockEventSource.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
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
Task<int> awaitOneKeyCodepoint(TuiRuntime& runtime)
{
    auto const event = co_await runtime.nextEvent();
    co_return static_cast<int>(std::get<KeyEvent>(event).codepoint);
}

/// Collects the codepoints of @p count key events in arrival order.
Task<int> sumKeyCodepoints(TuiRuntime& runtime, int count)
{
    auto sum = 0;
    for (auto i = 0; i < count; ++i)
    {
        auto const event = co_await runtime.nextEvent();
        sum += static_cast<int>(std::get<KeyEvent>(event).codepoint);
    }
    co_return sum;
}

/// Returns true once the agent-ready wakeup is observed.
Task<bool> awaitAgentReady(TuiRuntime& runtime)
{
    co_await runtime.nextAgentReady();
    co_return true;
}

/// Awaits input; returns a sentinel if cancelled instead.
Task<int> awaitKeyOrCancel(TuiRuntime& runtime, int cancelSentinel)
{
    try
    {
        auto const event = co_await runtime.nextEvent();
        co_return static_cast<int>(std::get<KeyEvent>(event).codepoint);
    }
    catch (OperationCancelled const&)
    {
        co_return cancelSentinel;
    }
}

/// Resumes immediately when the delay has already elapsed (the ready path).
Task<int> awaitZeroDelay(TuiRuntime& runtime)
{
    co_await runtime.delay(std::chrono::milliseconds { 0 });
    co_return 7;
}

/// Parks on a long delay; returns a sentinel when cancelled.
Task<int> awaitDelayOrCancel(TuiRuntime& runtime, int cancelSentinel)
{
    try
    {
        co_await runtime.delay(std::chrono::milliseconds { 500 });
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

} // namespace

TEST_CASE("Runtime delivers a scripted input event to a waiting flow", "[TuiRuntime]")
{
    auto source = MockEventSource {};
    source.pushEvents({ InputEvent { keyOf(U'x') } });
    auto runtime = TuiRuntime { source };

    auto const result = runtime.blockOn(awaitOneKeyCodepoint(runtime));

    REQUIRE(result == static_cast<int>(U'x'));
}

TEST_CASE("Runtime delivers buffered events in arrival order", "[TuiRuntime]")
{
    auto source = MockEventSource {};
    // Two events arriving in one wait are buffered and consumed in order.
    source.pushEvents({ InputEvent { keyOf(U'a') }, InputEvent { keyOf(U'b') } });
    auto runtime = TuiRuntime { source };

    auto const result = runtime.blockOn(sumKeyCodepoints(runtime, 2));

    REQUIRE(result == static_cast<int>(U'a') + static_cast<int>(U'b'));
}

TEST_CASE("Runtime resumes a flow when the agent wakeup fires", "[TuiRuntime]")
{
    auto source = MockEventSource {};
    source.pushAgentReady();
    auto runtime = TuiRuntime { source };

    auto const ready = runtime.blockOn(awaitAgentReady(runtime));

    REQUIRE(ready);
}

TEST_CASE("Protocol reports never surface as input events", "[TuiRuntime]")
{
    auto source = MockEventSource {};
    // A focus report (internal) followed by a real key: the flow must see only the key.
    source.pushEvents({ InputEvent { tui::FocusEvent { .focused = true } }, InputEvent { keyOf(U'z') } });
    auto runtime = TuiRuntime { source };

    auto const result = runtime.blockOn(awaitOneKeyCodepoint(runtime));

    REQUIRE(result == static_cast<int>(U'z'));
}

TEST_CASE("Interrupt cancels a flow parked on input", "[TuiRuntime]")
{
    auto source = MockEventSource {};
    source.pushInterrupt();
    auto runtime = TuiRuntime { source };

    constexpr auto sentinel = -99;
    auto const result = runtime.blockOn(awaitKeyOrCancel(runtime, sentinel));

    REQUIRE(result == sentinel);
}

TEST_CASE("delay(0) resumes without waiting", "[TuiRuntime]")
{
    auto source = MockEventSource {};
    auto runtime = TuiRuntime { source };

    auto const result = runtime.blockOn(awaitZeroDelay(runtime));

    REQUIRE(result == 7);
    REQUIRE(source.waitCount() == 0); // ready path never blocks
}

TEST_CASE("A pending delay bounds the wait timeout", "[TuiRuntime]")
{
    auto source = MockEventSource {};
    source.pushInterrupt(); // end the test before the 500ms elapses
    auto runtime = TuiRuntime { source };

    constexpr auto sentinel = -7;
    auto const result = runtime.blockOn(awaitDelayOrCancel(runtime, sentinel));

    REQUIRE(result == sentinel);
    REQUIRE(source.waitCount() >= 1);
    // The first wait is bounded by the pending timer, not an indefinite (-1) block.
    auto const firstTimeout = source.recordedTimeouts().front();
    REQUIRE(firstTimeout > 0);
    REQUIRE(firstTimeout <= 500);
}
