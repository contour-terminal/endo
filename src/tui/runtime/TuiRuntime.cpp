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
    std::push_heap(_timers.begin(), _timers.end(), soonestFirst);
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
    if (_timers.empty())
        return -1; // Block indefinitely until a source becomes ready.

    auto const now = std::chrono::steady_clock::now();
    auto const deadline = _timers.front().deadline;
    if (deadline <= now)
        return 0;

    auto const ms = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
    return static_cast<int>(std::min<long long>(ms, std::numeric_limits<int>::max()));
}

void TuiRuntime::routeDecodedEvent(InputEvent&& event)
{
    // TODO(#19): route protocol reports to one-shot Terminal query awaiters and
    // the Terminal's color-scheme/focus handlers. Until then they are consumed
    // here so they never surface as application input.
    if (isProtocolReport(event))
        return;
    _inputBuffer.push_back(std::move(event));
}

void TuiRuntime::fireExpiredTimers()
{
    auto const now = std::chrono::steady_clock::now();
    while (!_timers.empty() && _timers.front().deadline <= now)
    {
        std::pop_heap(_timers.begin(), _timers.end(), soonestFirst);
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

    if (_inputWaiter && hasBufferedInput())
        wakeWaiter(_inputWaiter);

    if (outcome.agentReady)
    {
        _agentPending = true;
        wakeWaiter(_agentWaiter);
    }

    fireExpiredTimers();

    // Resume coroutines woken during this iteration so an event is delivered in
    // the same pump it arrived, rather than on the next one.
    drainReadyQueue();
}

NextInputEventAwaiter TuiRuntime::nextEvent() noexcept
{
    return NextInputEventAwaiter { *this };
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
