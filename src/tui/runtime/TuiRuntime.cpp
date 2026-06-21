// SPDX-License-Identifier: Apache-2.0
#include <tui/runtime/TuiRuntime.hpp>

#include <algorithm>
#include <cassert>
#include <limits>
#include <utility>

namespace tui::runtime
{

TuiRuntime::TuiRuntime(EventSource& source) noexcept: _source(source)
{
}

TuiRuntime::~TuiRuntime()
{
    // Cancel every spawned flow and let parked awaiters unwind via RAII before
    // their frames are destroyed. Order matters: request_stop() first so that when
    // wakeAllWaiters() requeues parked handles, drainReadyQueue() resumes them into
    // await_resume(), which sees stop_requested() and throws OperationCancelled —
    // unwinding the frame's locals. The frames then complete (done() == true), so
    // the spawned-flow Tasks destroy already-finished frames. Cancelled re-awaits
    // resume synchronously (await_suspend returns false when stop is requested), so
    // a single drain converges. No-op and effectively free when nothing is parked.
    _rootStop.request_stop();
    wakeAllWaiters();
    drainReadyQueue();
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
    // A positive remainder under 1ms truncates to 0; clamp to 1 so the next wait
    // actually blocks instead of spinning on wait(0) until the deadline crosses now.
    return static_cast<int>(std::clamp<long long>(ms, 1, std::numeric_limits<int>::max()));
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

    // Clear transient wait state so a reused runtime (the prompt runtime persists
    // across REPL iterations) cannot carry a stale deadline or pending flag into the
    // next blockOn. wakeWaiter already resets these when _inputWaiter was set; this
    // also covers the case where the slot was already empty.
    _inputDeadline.reset();
    _inputWaiterWantsAgent = false;
    _agentPending = false;
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
    {
        // Agent wakeups are only meaningful to an agent-interested waiter
        // (nextActivity with wantsAgent, or nextAgentReady). The invariant is that
        // exactly one such consumer exists while the agent worker runs; a plain
        // nextEvent waiter must never coexist with a live agent channel, else this
        // flag would go unconsumed and a later await_ready() would resume on a stale
        // wakeup. Assert documents and enforces that in debug builds.
        assert((!_inputWaiter || _inputWaiterWantsAgent || _agentWaiter)
               && "agentReady fired with no agent-interested waiter parked");
        _agentPending = true;
    }

    // Wake the input waiter when an event is ready, its timed wait elapsed
    // (nextEventFor/nextActivity resume so the caller can run idle ticks), a
    // non-input activity occurred (focus change / finished job — resume so the
    // caller can redraw or report), or — for nextActivity, which also services the
    // agent worker — when a message is pending.
    if (_inputWaiter
        && (hasBufferedInput() || inputDeadlinePassed() || outcome.activity
            || (_inputWaiterWantsAgent && _agentPending)))
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
