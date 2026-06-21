// SPDX-License-Identifier: Apache-2.0
#include <tui/runtime/TuiRuntime.hpp>

#include <algorithm>
#include <limits>
#include <type_traits>
#include <utility>
#include <variant>

namespace tui::runtime
{

namespace
{
    /// @return True if @p event is an internal protocol report rather than an
    /// application input event.
    [[nodiscard]] bool isProtocolReport(InputEvent const& event) noexcept
    {
        return std::visit(
            [](auto const& concrete) {
                using T = std::decay_t<decltype(concrete)>;
                return std::is_same_v<T, ColorSchemeReport> || std::is_same_v<T, CellSizeReport>
                       || std::is_same_v<T, CursorPositionReport> || std::is_same_v<T, DecModeReport>
                       || std::is_same_v<T, FocusEvent> || std::is_same_v<T, DcsResponse>;
            },
            event);
    }
} // namespace

TuiRuntime::TuiRuntime(EventSource& source) noexcept: _source(source)
{
}

void TuiRuntime::spawn(endo::coro::Task<void> task)
{
    task.handle().promise().setStopToken(_rootStop.get_token());
    _ready.push_back(task.handle());
    _roots.push_back(std::move(task));
}

InputEvent TuiRuntime::popBufferedInput()
{
    auto event = std::move(_inputBuffer.front());
    _inputBuffer.pop_front();
    return event;
}

void TuiRuntime::scheduleTimer(std::chrono::steady_clock::time_point deadline, std::coroutine_handle<> waiter)
{
    _timers.push_back(TimerEntry { .deadline = deadline, .handle = waiter });
    std::ranges::push_heap(_timers, soonestFirst);
}

void TuiRuntime::drainReadyQueue()
{
    while (!_ready.empty())
    {
        auto const handle = _ready.front();
        _ready.pop_front();
        if (handle && !handle.done())
            handle.resume();
    }
}

int TuiRuntime::computeTimeoutMs() const
{
    // The soonest of the next timer and a timed input waiter's deadline.
    auto soonest = std::optional<std::chrono::steady_clock::time_point> {};
    if (!_timers.empty())
        soonest = _timers.front().deadline;
    if (_inputDeadline && (!soonest || *_inputDeadline < *soonest))
        soonest = _inputDeadline;

    if (!soonest)
        return -1; // Block indefinitely until a source becomes ready.

    auto const now = std::chrono::steady_clock::now();
    if (*soonest <= now)
        return 0;

    auto const ms = std::chrono::duration_cast<std::chrono::milliseconds>(*soonest - now).count();
    return static_cast<int>(std::min<long long>(ms, std::numeric_limits<int>::max()));
}

void TuiRuntime::routeDecodedEvent(InputEvent&& event)
{
    // Defensive net: the production EventSource already consumes color-scheme /
    // focus / cursor / cell reports (Terminal::consumeProtocolReports), but a
    // source is only contracted to deliver decoded events, not to pre-filter —
    // and DecModeReport / DcsResponse are not stripped upstream. Drop any report
    // here so it never surfaces as application input regardless of the source.
    // TODO(#19): route reports to one-shot Terminal query awaiters when those land.
    if (isProtocolReport(event))
        return;
    _inputBuffer.push_back(std::move(event));
}

void TuiRuntime::fireExpiredTimers()
{
    auto const now = std::chrono::steady_clock::now();
    while (!_timers.empty() && _timers.front().deadline <= now)
    {
        std::ranges::pop_heap(_timers, soonestFirst);
        auto const entry = _timers.back();
        _timers.pop_back();
        if (entry.handle && !entry.handle.done())
            _ready.push_back(entry.handle);
    }
}

void TuiRuntime::wakeWaiter(std::coroutine_handle<>& waiter)
{
    if (!waiter)
        return;
    if (&waiter == &_inputWaiter)
    {
        _inputDeadline.reset();
        _inputWaiterWantsAgent = false;
    }
    auto const handle = std::exchange(waiter, {});
    if (!handle.done())
        _ready.push_back(handle);
}

void TuiRuntime::wakeAllWaiters()
{
    wakeWaiter(_inputWaiter);
    wakeWaiter(_agentWaiter);
    for (auto const& entry: _timers)
        if (entry.handle && !entry.handle.done())
            _ready.push_back(entry.handle);
    _timers.clear();
}

void TuiRuntime::pumpOnce()
{
    drainReadyQueue();

    // Nothing is parked on a source: a well-formed root flow either completed
    // (the caller's loop will observe `done()`) or is awaiting a child task that
    // will itself park. Returning avoids a wait with no one to wake.
    auto const hasParked = _inputWaiter || _agentWaiter || !_timers.empty();
    if (!hasParked)
        return;

    auto outcome = _source.wait(computeTimeoutMs());

    if (outcome.interrupted)
    {
        if (_onInterrupt)
            _onInterrupt();
        else
            _rootStop.request_stop();
        if (_rootStop.stop_requested())
            wakeAllWaiters();
    }

    for (auto& event: outcome.events)
        routeDecodedEvent(std::move(event));

    if (outcome.agentReady)
        _agentPending = true;

    // Wake the input waiter when an event is ready, its timed wait elapsed
    // (nextEventFor/nextActivity resume so the caller can run idle ticks), or — for
    // nextActivity, which also services the agent worker — when a message is pending.
    if (_inputWaiter
        && (hasBufferedInput() || inputDeadlinePassed() || (_inputWaiterWantsAgent && _agentPending)))
        wakeWaiter(_inputWaiter);

    // Wake a dedicated nextAgentReady() waiter while a message is pending.
    if (_agentWaiter && _agentPending)
        wakeWaiter(_agentWaiter);

    fireExpiredTimers();

    // Resume coroutines woken during this iteration so an event is delivered in
    // the same pump it arrived, rather than on the next one.
    drainReadyQueue();
}

NextInputEventAwaiter TuiRuntime::nextEvent() noexcept
{
    return NextInputEventAwaiter { *this };
}

NextEventForAwaiter TuiRuntime::nextEventFor(std::chrono::milliseconds timeout) noexcept
{
    return NextEventForAwaiter { *this, std::chrono::steady_clock::now() + timeout };
}

NextActivityAwaiter TuiRuntime::nextActivity(std::chrono::milliseconds timeout) noexcept
{
    return NextActivityAwaiter { *this, std::chrono::steady_clock::now() + timeout };
}

DelayAwaiter TuiRuntime::delay(std::chrono::milliseconds duration) noexcept
{
    return DelayAwaiter { *this, std::chrono::steady_clock::now() + duration };
}

NextAgentReadyAwaiter TuiRuntime::nextAgentReady() noexcept
{
    return NextAgentReadyAwaiter { *this };
}

} // namespace tui::runtime
