// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file
/// `TuiRuntime` — the single-threaded coroutine driver for the TUI.
///
/// The runtime owns the one blocking primitive (an injected @c EventSource) and
/// multiplexes terminal input, the agent-message wakeup, the interrupt wakeup,
/// and timers over it. Flows (`endo::coro::Task`s) suspend on the awaitables the
/// runtime hands out — `nextEvent()`, `delay()`, `nextAgentReady()` — and the
/// pump loop resumes them when their source is ready. This replaces the many
/// hand-rolled `terminal.poll(timeout)` loops with one driver.

#include <tui/InputEvent.hpp>
#include <tui/runtime/EventSource.hpp>

#include <cassert>
#include <chrono>
#include <coroutine>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <unordered_map>
#include <vector>

#include <coro/Cancellation.hpp>
#include <coro/Task.hpp>
#include <platform/Clock.hpp>

namespace tui::runtime
{

class NextInputEventAwaiter;
class NextEventForAwaiter;
class NextActivityAwaiter;
class DelayAwaiter;
class NextAgentReadyAwaiter;
class WaitFdAwaiter;

/// What a `nextActivity()` wait observed first.
enum class ActivityKind : std::uint8_t
{
    Event,      ///< An input event is available.
    AgentReady, ///< The agent-message wakeup fired; drain the agent channel.
    Timeout,    ///< The wait elapsed with nothing to do (run idle ticks).
};

/// The result of a `nextActivity()` wait: the kind plus the event (if Event).
struct Activity
{
    ActivityKind kind = ActivityKind::Timeout; ///< Which source resolved the wait.
    std::optional<InputEvent> event;           ///< The input event, set iff kind == Event.
};

/// Single-threaded cooperative scheduler driving TUI coroutine flows.
///
/// Construct with an @c EventSource, `spawn` background flows and/or `blockOn`
/// a root flow; the pump runs on the calling (UI) thread until the root flow
/// completes. All scheduler state is touched only on that thread — the sole
/// cross-thread surfaces are the wakeups inside the @c EventSource.
class TuiRuntime
{
  public:
    /// @param source The multiplexed wait the pump drives (not owned; outlives the runtime).
    /// @param clock The monotonic time source for timers, timed waits, and delays
    ///        (not owned; outlives the runtime). Defaults to the process steady
    ///        clock; tests inject a @c ManualClock for deterministic timing.
    explicit TuiRuntime(EventSource& source,
                        endo::platform::IClock& clock = endo::platform::defaultSteadyClock()) noexcept;

    TuiRuntime(TuiRuntime const&) = delete;
    TuiRuntime& operator=(TuiRuntime const&) = delete;
    TuiRuntime(TuiRuntime&&) = delete;
    TuiRuntime& operator=(TuiRuntime&&) = delete;

    /// Cancels and unwinds any still-parked spawned flows before their frames are
    /// destroyed: requests stop, wakes every waiter, and drains the ready queue so
    /// parked awaiters resume, observe cancellation (OperationCancelled), and run
    /// their RAII cleanup. Members (including the spawned-flow storage) destruct
    /// afterward. A no-op when nothing is parked (the common case).
    ~TuiRuntime();

    /// Drives the pump until @p task completes, then returns its result.
    /// @param task The root flow to run (its frame is kept alive for the call).
    /// @return The value produced by @p task (or void).
    template <typename T>
    T blockOn(endo::coro::Task<T> task)
    {
        task.handle().promise().setStopToken(_rootStop.get_token());
        _ready.push_back(task.handle());
        while (!task.done())
            pumpOnce();
        return task.result();
    }

    /// Starts a background flow that runs alongside the root flow.
    /// @param task The flow to run (its frame is kept alive by the runtime).
    void spawn(endo::coro::Task<void> task);

    /// @return The root cancellation source; `request_stop()` cancels every flow.
    [[nodiscard]] endo::coro::StopSource& rootStopSource() noexcept { return _rootStop; }

    /// @return The monotonic clock backing all timers, timed waits, and delays.
    ///         Awaiters read deadlines through this so tests can drive time
    ///         deterministically via an injected @c ManualClock.
    [[nodiscard]] endo::platform::IClock& clock() const noexcept { return _clock; }

    /// Sets the handler invoked when the @c EventSource reports an interrupt
    /// (SIGINT / Ctrl+C). The default requests cancellation of the root source.
    /// A consumer overrides this to scope cancellation (e.g. cancel only the open
    /// modal, or forward a cancel to the agent worker).
    /// @param handler The interrupt policy, or `nullptr` to restore the default.
    void setInterruptHandler(std::function<void()> handler) { _onInterrupt = std::move(handler); }

    /// @return An awaitable yielding the next input event.
    [[nodiscard]] NextInputEventAwaiter nextEvent() noexcept;

    /// @param timeout How long to wait for an event before giving up.
    /// @return An awaitable yielding the next input event, or std::nullopt if
    ///         @p timeout elapses first (so the caller can run idle ticks).
    [[nodiscard]] NextEventForAwaiter nextEventFor(std::chrono::milliseconds timeout) noexcept;

    /// Waits for input, an agent message, or a timeout — whichever happens first.
    /// Used by the agent-mode loop, which must service both input and the agent
    /// worker in one wait.
    /// @param timeout How long to wait before reporting ActivityKind::Timeout.
    /// @return An awaitable yielding the first activity observed.
    [[nodiscard]] NextActivityAwaiter nextActivity(std::chrono::milliseconds timeout) noexcept;

    /// @param duration How long to suspend.
    /// @return An awaitable that resumes after @p duration elapses.
    [[nodiscard]] DelayAwaiter delay(std::chrono::milliseconds duration) noexcept;

    /// @param deadline The absolute instant (on this runtime's clock) to resume at.
    /// @return An awaitable that resumes once the clock reaches @p deadline.
    [[nodiscard]] DelayAwaiter sleepUntil(std::chrono::steady_clock::time_point deadline) noexcept;

    /// Suspends until @p fd is readable (data, EOF, or HUP/ERR), without consuming
    /// any bytes — the caller then performs a non-blocking read. The terminal input
    /// fd is owned by `nextEvent()`; use this for other fds (pipes, PTYs, sockets).
    /// @param fd The native handle to wait on (must outlive the await).
    /// @return An awaitable resolving when @p fd is readable; throws
    ///         @c OperationCancelled if the flow is cancelled while parked.
    [[nodiscard]] WaitFdAwaiter waitReadable(endo::platform::NativeHandle fd) noexcept;

    /// Suspends until @p fd is writable (space available in the send buffer).
    /// @param fd The native handle to wait on (must outlive the await).
    /// @return An awaitable resolving when @p fd is writable; throws
    ///         @c OperationCancelled if the flow is cancelled while parked.
    [[nodiscard]] WaitFdAwaiter waitWritable(endo::platform::NativeHandle fd) noexcept;

    /// @return An awaitable that resumes when the agent-message wakeup fires.
    [[nodiscard]] NextAgentReadyAwaiter nextAgentReady() noexcept;

    /// @name Awaiter-facing scheduler primitives (internal)
    /// Called by the runtime awaitables; not part of the consumer API.
    /// @{
    [[nodiscard]] bool hasBufferedInput() const noexcept { return !_inputBuffer.empty(); }

    [[nodiscard]] InputEvent popBufferedInput();

    /// Parks @p waiter for the next input event, optionally with a deadline after
    /// which it is resumed with no event (a timeout).
    /// @param waiter The coroutine to resume.
    /// @param deadline Resume-by time for a timed wait, or std::nullopt to block.
    /// @pre No input waiter is currently parked. The runtime drives one
    ///      input-awaiting flow at a time; two flows parked on input concurrently
    ///      would clobber this slot and strand the first. (Modal nesting suspends
    ///      the parent, so its awaiter is not parked while the child awaits.)
    void registerInputWaiter(std::coroutine_handle<> waiter,
                             std::optional<std::chrono::steady_clock::time_point> deadline = std::nullopt,
                             bool wantsAgent = false) noexcept
    {
        assert(!_inputWaiter && "registerInputWaiter: an input waiter is already parked");
        _inputWaiter = waiter;
        _inputDeadline = deadline;
        _inputWaiterWantsAgent = wantsAgent;
    }

    /// @return True if the parked input waiter's deadline has elapsed.
    [[nodiscard]] bool inputDeadlinePassed() const noexcept
    {
        return _inputDeadline.has_value() && *_inputDeadline <= _clock.now();
    }

    void scheduleTimer(std::chrono::steady_clock::time_point deadline, std::coroutine_handle<> waiter);

    /// @return Whether an agent wakeup is pending. Only meaningful to an
    ///         agent-interested waiter (nextActivity with wantsAgent, or
    ///         nextAgentReady); a plain nextEvent waiter never consumes it.
    [[nodiscard]] bool agentPending() const noexcept { return _agentPending; }

    /// Clears the pending agent wakeup. Called only by an agent-interested waiter.
    void consumeAgentPending() noexcept { _agentPending = false; }

    /// Parks @p waiter for the next agent wakeup.
    /// @param waiter The coroutine to resume.
    /// @pre No agent waiter is currently parked (single-waiter invariant).
    void registerAgentWaiter(std::coroutine_handle<> waiter) noexcept
    {
        assert(!_agentWaiter && "registerAgentWaiter: an agent waiter is already parked");
        _agentWaiter = waiter;
    }

    /// Attaches @p fd to the event source for @p interest and parks @p waiter until
    /// it becomes ready. Unlike the single input/agent waiter slots, several fd
    /// waiters may be parked concurrently (one per distinct fd), so they live in a
    /// map keyed by registration token.
    /// @param fd The native handle to wait on.
    /// @param interest The readiness to wait for (Read or Write).
    /// @param waiter The coroutine to resume on readiness or cancellation.
    /// @return The registration token (to detach on resume/cancel), or
    ///         @c FdToken::invalid() if the attach failed.
    [[nodiscard]] FdToken registerFdWaiter(endo::platform::NativeHandle fd,
                                           FdInterest interest,
                                           std::coroutine_handle<> waiter);

    /// Detaches @p token from the event source and drops its parked waiter, if any.
    /// Idempotent. Called by the awaiter on resume (ready or cancelled).
    /// @param token The registration to remove.
    void unregisterFdWaiter(FdToken token) noexcept;

    /// Re-queues @p waiter for resumption because its cancellation token fired while
    /// it was parked on a timer or fd. Used by the timed/fd awaiters' stop-callbacks
    /// so a `whenAny`/`withTimeout` loser parked on `delay`/`waitReadable` unwinds
    /// promptly instead of only when its deadline/fd eventually fires. The awaiter
    /// then observes stop_requested() in await_resume and throws OperationCancelled;
    /// its stale timer entry / fd registration is skipped (handle already done) or
    /// detached by the awaiter. Safe to call once per parked waiter.
    /// @param waiter The parked coroutine to resume for cancellation.
    void requeueForCancellation(std::coroutine_handle<> waiter);

    /// @}

  private:
    /// One scheduled timer: a deadline and the coroutine to resume at it.
    struct TimerEntry
    {
        std::chrono::steady_clock::time_point deadline;
        std::coroutine_handle<> handle;
    };

    /// Heap comparator placing the soonest deadline at the heap root (a min-heap
    /// over the standard max-heap, by reversing the comparison).
    /// @return True if @p a is later than @p b.
    [[nodiscard]] static bool soonestFirst(TimerEntry const& a, TimerEntry const& b) noexcept
    {
        return a.deadline > b.deadline;
    }

    /// Runs one iteration: resume ready coroutines, then wait and route results.
    void pumpOnce();

    /// Resumes every coroutine currently in the ready queue.
    void drainReadyQueue();

    /// @return The timeout (ms) for the next wait: the soonest timer, or -1 if none.
    [[nodiscard]] int computeTimeoutMs() const;

    /// Routes one decoded event to the input buffer (dropping protocol reports,
    /// which gain dedicated handling with the Terminal query awaitables).
    /// @param event The event to route (consumed).
    void routeDecodedEvent(InputEvent&& event);

    /// Moves expired timers' coroutines into the ready queue.
    void fireExpiredTimers();

    /// Moves a parked waiter (if any) into the ready queue and clears the slot.
    /// @param waiter The waiter slot to wake.
    void wakeWaiter(std::coroutine_handle<>& waiter);

    /// Resumes the coroutines parked on the fds reported ready by a wait, detaching
    /// each from the event source. Idempotent per token.
    /// @param tokens The ready fd tokens (readyRead or readyWrite from the outcome).
    void wakeFdWaiters(std::vector<FdToken> const& tokens);

    /// Wakes every parked flow so cancelled awaitables can unwind.
    void wakeAllWaiters();

    EventSource& _source;                       ///< The injected multiplexed wait.
    endo::platform::IClock& _clock;             ///< The injected monotonic time source.
    std::deque<std::coroutine_handle<>> _ready; ///< Coroutines ready to resume now.
    std::vector<TimerEntry> _timers;            ///< Min-heap by deadline (soonest at front).
    std::deque<InputEvent> _inputBuffer;        ///< Decoded input awaiting a consumer.
    std::coroutine_handle<> _inputWaiter;       ///< Flow parked in nextEvent()/nextEventFor()/nextActivity().
    std::optional<std::chrono::steady_clock::time_point> _inputDeadline; ///< Timeout for a timed input wait.
    bool _inputWaiterWantsAgent = false;  ///< Input waiter also wakes on an agent message (nextActivity).
    std::coroutine_handle<> _agentWaiter; ///< Flow parked in nextAgentReady(), if any.
    bool _agentPending = false; ///< Agent wakeup fired; consumed only by an agent-interested waiter.
    std::unordered_map<FdToken, std::coroutine_handle<>>
        _fdWaiters;                             ///< Flows parked on a generic fd, by token.
    std::vector<endo::coro::Task<void>> _roots; ///< Keeps spawned background flows alive.
    endo::coro::StopSource _rootStop;           ///< Root cancellation source.
    std::function<void()> _onInterrupt;         ///< Interrupt policy; default cancels the root.
};

/// Awaitable yielding the next input event, or throwing @c OperationCancelled if
/// the awaiting flow is cancelled while parked.
class NextInputEventAwaiter
{
  public:
    explicit NextInputEventAwaiter(TuiRuntime& runtime) noexcept: _runtime(runtime) {}

    [[nodiscard]] bool await_ready() const noexcept { return _runtime.hasBufferedInput(); }

    /// @param awaiting The coroutine performing the `co_await`.
    /// @return False (resume now) if already cancelled, true to park as the input waiter.
    template <typename Promise>
    [[nodiscard]] bool await_suspend(std::coroutine_handle<Promise> awaiting) noexcept
    {
        if constexpr (requires { awaiting.promise().stopToken(); })
            _token = awaiting.promise().stopToken();
        if (_token.stop_requested())
            return false;
        _runtime.registerInputWaiter(awaiting);
        return true;
    }

    /// @return The next buffered input event.
    /// @throws OperationCancelled if the flow was cancelled while parked.
    [[nodiscard]] InputEvent await_resume()
    {
        if (_token.stop_requested() || !_runtime.hasBufferedInput())
            throw endo::coro::OperationCancelled {};
        return _runtime.popBufferedInput();
    }

  private:
    TuiRuntime& _runtime;
    endo::coro::StopToken _token;
};

/// Awaitable yielding the next input event, or std::nullopt if a timeout elapses
/// first. Throws @c OperationCancelled if the flow is cancelled while parked.
class NextEventForAwaiter
{
  public:
    NextEventForAwaiter(TuiRuntime& runtime, std::chrono::steady_clock::time_point deadline) noexcept:
        _runtime(runtime), _deadline(deadline)
    {
    }

    [[nodiscard]] bool await_ready() const noexcept { return _runtime.hasBufferedInput(); }

    /// @param awaiting The coroutine performing the `co_await`.
    /// @return False (resume now) if already cancelled, true to park with a deadline.
    template <typename Promise>
    [[nodiscard]] bool await_suspend(std::coroutine_handle<Promise> awaiting) noexcept
    {
        if constexpr (requires { awaiting.promise().stopToken(); })
            _token = awaiting.promise().stopToken();
        if (_token.stop_requested())
            return false;
        _runtime.registerInputWaiter(awaiting, _deadline);
        return true;
    }

    /// @return The next buffered input event, or std::nullopt on timeout.
    /// @throws OperationCancelled if the flow was cancelled while parked.
    [[nodiscard]] std::optional<InputEvent> await_resume()
    {
        if (_token.stop_requested())
            throw endo::coro::OperationCancelled {};
        if (_runtime.hasBufferedInput())
            return _runtime.popBufferedInput();
        return std::nullopt; // timed out
    }

  private:
    TuiRuntime& _runtime;
    std::chrono::steady_clock::time_point _deadline;
    endo::coro::StopToken _token;
};

/// Awaitable that resumes on the first of: an input event, an agent message, or
/// a timeout. Throws @c OperationCancelled if cancelled while parked.
class NextActivityAwaiter
{
  public:
    NextActivityAwaiter(TuiRuntime& runtime, std::chrono::steady_clock::time_point deadline) noexcept:
        _runtime(runtime), _deadline(deadline)
    {
    }

    [[nodiscard]] bool await_ready() const noexcept
    {
        return _runtime.hasBufferedInput() || _runtime.agentPending();
    }

    /// @param awaiting The coroutine performing the `co_await`.
    /// @return False (resume now) if already cancelled, true to park (input + agent + deadline).
    template <typename Promise>
    [[nodiscard]] bool await_suspend(std::coroutine_handle<Promise> awaiting) noexcept
    {
        if constexpr (requires { awaiting.promise().stopToken(); })
            _token = awaiting.promise().stopToken();
        if (_token.stop_requested())
            return false;
        _runtime.registerInputWaiter(awaiting, _deadline, /*wantsAgent=*/true);
        return true;
    }

    /// @return The first activity observed (event, agent-ready, or timeout).
    /// @throws OperationCancelled if the flow was cancelled while parked.
    [[nodiscard]] Activity await_resume()
    {
        if (_token.stop_requested())
            throw endo::coro::OperationCancelled {};
        if (_runtime.hasBufferedInput())
            return Activity { .kind = ActivityKind::Event, .event = _runtime.popBufferedInput() };
        if (_runtime.agentPending())
        {
            _runtime.consumeAgentPending();
            return Activity { .kind = ActivityKind::AgentReady, .event = std::nullopt };
        }
        return Activity { .kind = ActivityKind::Timeout, .event = std::nullopt };
    }

  private:
    TuiRuntime& _runtime;
    std::chrono::steady_clock::time_point _deadline;
    endo::coro::StopToken _token;
};

/// Awaitable that resumes after a delay (or throws on cancellation).
///
/// While parked it registers a stop-callback so that if its cancellation token is
/// stopped before the deadline (e.g. a `whenAny`/`withTimeout` sibling won), the
/// parked coroutine is re-queued promptly and unwinds via @c OperationCancelled,
/// rather than lingering until the deadline elapses. The stale timer entry is
/// skipped when it later fires (the handle is already done).
class DelayAwaiter
{
  public:
    DelayAwaiter(TuiRuntime& runtime, std::chrono::steady_clock::time_point deadline) noexcept:
        _runtime(runtime), _deadline(deadline)
    {
    }

    [[nodiscard]] bool await_ready() const noexcept { return _deadline <= _runtime.clock().now(); }

    template <typename Promise>
    [[nodiscard]] bool await_suspend(std::coroutine_handle<Promise> awaiting)
    {
        if constexpr (requires { awaiting.promise().stopToken(); })
            _token = awaiting.promise().stopToken();
        if (_token.stop_requested())
            return false;
        _runtime.scheduleTimer(_deadline, awaiting);
        _cancelReg.emplace(_token,
                           [&runtime = _runtime, awaiting] { runtime.requeueForCancellation(awaiting); });
        return true;
    }

    /// @throws OperationCancelled if the flow was cancelled while parked.
    void await_resume()
    {
        _cancelReg.reset();
        if (_token.stop_requested())
            throw endo::coro::OperationCancelled {};
    }

  private:
    std::optional<endo::coro::StopCallback<std::function<void()>>> _cancelReg;
    TuiRuntime& _runtime;
    std::chrono::steady_clock::time_point _deadline;
    endo::coro::StopToken _token;
};

/// Awaitable that resumes when the agent-message wakeup fires; the consumer then
/// drains its own typed message queue.
class NextAgentReadyAwaiter
{
  public:
    explicit NextAgentReadyAwaiter(TuiRuntime& runtime) noexcept: _runtime(runtime) {}

    [[nodiscard]] bool await_ready() const noexcept { return _runtime.agentPending(); }

    template <typename Promise>
    [[nodiscard]] bool await_suspend(std::coroutine_handle<Promise> awaiting) noexcept
    {
        if constexpr (requires { awaiting.promise().stopToken(); })
            _token = awaiting.promise().stopToken();
        if (_token.stop_requested())
            return false;
        _runtime.registerAgentWaiter(awaiting);
        return true;
    }

    /// @throws OperationCancelled if the flow was cancelled while parked.
    void await_resume()
    {
        if (_token.stop_requested())
            throw endo::coro::OperationCancelled {};
        _runtime.consumeAgentPending();
    }

  private:
    TuiRuntime& _runtime;
    endo::coro::StopToken _token;
};

/// Awaitable that resumes when a registered fd reaches a given readiness (Read or
/// Write), or throws @c OperationCancelled if the awaiting flow is cancelled while
/// parked. Returned by @c TuiRuntime::waitReadable / @c waitWritable.
///
/// Readiness is observed via the OS wait, so the awaiter is never ready before it
/// suspends: it always parks (after attaching the fd to the event source), and the
/// runtime resumes it when the wait reports the fd ready. On resume — whether ready
/// or cancelled — it detaches the fd so the registration never outlives the await.
class WaitFdAwaiter
{
  public:
    /// @param runtime The runtime whose event source the fd is registered with.
    /// @param fd The native handle to wait on.
    /// @param interest The readiness to wait for (Read or Write).
    WaitFdAwaiter(TuiRuntime& runtime, endo::platform::NativeHandle fd, FdInterest interest) noexcept:
        _runtime(runtime), _fd(fd), _interest(interest)
    {
    }

    /// Readiness is only known after the OS wait, so a valid fd never reports ready
    /// before suspending. An invalid fd resolves immediately (await_resume then
    /// reports cancellation), avoiding a pointless park on a handle that can never
    /// signal.
    [[nodiscard]] bool await_ready() const noexcept { return _fd == endo::platform::InvalidHandle; }

    /// Captures the cancellation token, then (unless already cancelled) attaches the
    /// fd and parks. Checks cancellation BEFORE registering so a cancelled flow
    /// resumes immediately without leaving a dangling registration.
    /// @param awaiting The coroutine performing the `co_await`.
    /// @return False (resume now) if already cancelled or the attach failed; true to park.
    template <typename Promise>
    [[nodiscard]] bool await_suspend(std::coroutine_handle<Promise> awaiting)
    {
        if constexpr (requires { awaiting.promise().stopToken(); })
            _token = awaiting.promise().stopToken();
        if (_token.stop_requested())
            return false;
        _registration = _runtime.registerFdWaiter(_fd, _interest, awaiting);
        if (!_registration)
            return false; // attach failed: resume and surface the failure in await_resume
        // If the token is stopped while parked (a whenAny/withTimeout sibling won),
        // re-queue this coroutine promptly so it unwinds instead of waiting for the
        // fd to become ready (which may never happen).
        _cancelReg.emplace(_token,
                           [&runtime = _runtime, awaiting] { runtime.requeueForCancellation(awaiting); });
        return true;
    }

    /// Detaches the fd and, if the flow was cancelled while parked or the attach
    /// failed, reports cancellation.
    /// @throws OperationCancelled if cancelled while parked or the fd could not be attached.
    void await_resume()
    {
        _cancelReg.reset();
        if (_registration)
            _runtime.unregisterFdWaiter(_registration);
        if (_token.stop_requested() || !_registration)
            throw endo::coro::OperationCancelled {};
    }

  private:
    std::optional<endo::coro::StopCallback<std::function<void()>>> _cancelReg;
    TuiRuntime& _runtime;
    endo::platform::NativeHandle _fd;
    FdInterest _interest;
    FdToken _registration {};
    endo::coro::StopToken _token;
};

} // namespace tui::runtime
