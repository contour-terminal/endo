// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file
/// Async, cancellable child-process wait for the coroutine reactor.
///
/// The blocking `ProcessManager::wait` (a bare `waitpid`) cannot be interrupted,
/// so a slow or hung child freezes whatever thread awaits it. @ref waitProcessAsync
/// instead polls the child non-blockingly (`WaitFlag::NoHang`) and suspends on the
/// runtime's timer between polls, so a `whenAny`/`withTimeout` sibling (a timeout
/// arm or an abort-key watcher) can cancel the wait — the parked @c delay throws
/// @c OperationCancelled, which unwinds the coroutine cleanly.
///
/// It reaps the pid directly, so it must be driven on a reactor where no other
/// reaper is active for that child (e.g. a nested @c PollEventSource reactor, whose
/// enclosing main loop is not pumping and therefore not running the SIGCHLD-driven
/// `Shell::onSigchld`). See `awaitSubstitutionPipeline`.

#include <chrono>

#include <coro/Task.hpp>
#include <platform/Process.hpp>
#include <platform/WaitResult.hpp>

namespace tui::runtime
{
class TuiRuntime;
}

namespace endo::shell
{

/// Waits for @p pid to change state (exit, be signalled, or stop) without blocking
/// the reactor: polls `pm->wait(pid, NoHang | Untraced)` and, while the child is
/// still running, suspends for @p pollInterval on @p runtime before retrying.
///
/// Reaps @p pid directly on a state change, so it must only be used where no other
/// reaper handles this child concurrently.
///
/// @param runtime The reactor providing the timer to suspend on between polls (a
///        pointer, not a reference: coroutine reference parameters can dangle
///        across a suspend point, so the caller guarantees the pointee outlives the
///        wait — same convention as @c tui::runtime::withTimeout).
/// @param pm The process manager to poll (injectable for tests; a pointer for the
///        same reason).
/// @param pid The child process id to wait for.
/// @param pollInterval How long to suspend between non-blocking polls.
/// @return The child's terminal @c WaitResult. A wait error (e.g. the child was
///         already reaped, ECHILD) resolves to a benign signalled result rather
///         than propagating, since "the child is gone" is the outcome we want.
/// @throws endo::coro::OperationCancelled if the flow is cancelled while parked
///         (a losing @c whenAny arm), so callers unwind via ordinary RAII.
[[nodiscard]] coro::Task<platform::WaitResult> waitProcessAsync(
    tui::runtime::TuiRuntime* runtime,
    platform::ProcessManager* pm,
    platform::ProcessId pid,
    std::chrono::milliseconds pollInterval = std::chrono::milliseconds { 20 });

} // namespace endo::shell
