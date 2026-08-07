// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file
/// `withTimeout` — bound a task by a deadline on the runtime's clock.
///
/// Races the work against `runtime.delay(timeout)` via @c whenAny: if the work
/// finishes first it returns its value; if the timeout fires first the work is
/// cancelled (it unwinds via @c OperationCancelled, like every runtime awaitable)
/// and the result is @c std::nullopt. Built entirely on @c whenAny + the runtime
/// timer, so it is deterministic under an injected @c ManualClock.
///
/// The vendored @c net/WithTimeout.hpp is the same construction over
/// @c net::EventLoop. The two are deliberately kept apart rather than
/// deduplicated: they are parameterized on different runtimes, and this one's
/// @c TuiRuntime carries terminal input, resize and interrupt wakeups that
/// @c net::EventLoop (a pure fd reactor) does not model. Unifying them would mean
/// either editing the vendored header — which every fetch overwrites — or
/// templating over the runtime, which buys little for two call sites.

#include <tui/runtime/TuiRuntime.hpp>

#include <chrono>
#include <memory>
#include <optional>
#include <vector>

#include <coro/Task.hpp>
#include <coro/WhenAny.hpp>

namespace tui::runtime
{

namespace detail
{

    /// Wraps a value-producing task so it can participate in a `whenAny` (which
    /// races `Task<void>`): awaits the work and stores its result in @p slot.
    /// @tparam T The work's result type.
    /// @param work The task to run.
    /// @param slot Shared storage receiving the result if @p work completes.
    template <typename T>
    coro::Task<void> captureResult(coro::Task<T> work, std::shared_ptr<std::optional<T>> slot)
    {
        *slot = co_await std::move(work);
    }

    /// The timer arm of a `withTimeout`: simply sleeps to the deadline.
    /// @param runtime The runtime providing the timer.
    /// @param timeout The duration after which the wait elapses.
    inline coro::Task<void> timeoutArm(TuiRuntime* runtime, std::chrono::milliseconds timeout)
    {
        co_await runtime->delay(timeout);
    }

} // namespace detail

/// Runs @p work with a deadline. If @p work finishes first, returns its value; if
/// @p timeout elapses first, cancels @p work and returns @c std::nullopt.
/// @tparam T The work's result type (must be non-void; use the void overload for
///         value-less work).
/// @param runtime The runtime whose clock bounds the wait (not owned; outlives the
///        coroutine — a pointer, since coroutine reference parameters can dangle).
/// @param work The task to bound (moved in). Must unwind cleanly on cancellation.
/// @param timeout The maximum duration to allow.
/// @return The work's result, or @c std::nullopt on timeout.
template <typename T>
    requires(!std::is_void_v<T>)
coro::Task<std::optional<T>> withTimeout(TuiRuntime* runtime,
                                         coro::Task<T> work,
                                         std::chrono::milliseconds timeout)
{
    auto slot = std::make_shared<std::optional<T>>();
    auto tasks = std::vector<coro::Task<void>> {};
    tasks.reserve(2);
    tasks.push_back(detail::captureResult(std::move(work), slot));
    tasks.push_back(detail::timeoutArm(runtime, timeout));

    auto const winner = co_await coro::whenAny(std::move(tasks));
    if (winner == 0)
        co_return std::move(*slot);
    co_return std::nullopt; // the timeout arm won; the work was cancelled
}

/// Value-less overload: runs @p work with a deadline.
/// @param runtime The runtime whose clock bounds the wait (not owned; a pointer for
///        the same reason as above).
/// @param work The void task to bound (moved in).
/// @param timeout The maximum duration to allow.
/// @return True if @p work completed before the timeout, false if it timed out.
inline coro::Task<bool> withTimeout(TuiRuntime* runtime,
                                    coro::Task<void> work,
                                    std::chrono::milliseconds timeout)
{
    auto tasks = std::vector<coro::Task<void>> {};
    tasks.reserve(2);
    tasks.push_back(std::move(work));
    tasks.push_back(detail::timeoutArm(runtime, timeout));

    auto const winner = co_await coro::whenAny(std::move(tasks));
    co_return winner == 0;
}

} // namespace tui::runtime
