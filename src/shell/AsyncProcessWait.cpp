// SPDX-License-Identifier: Apache-2.0
#include <shell/AsyncProcessWait.hpp>

#include <tui/runtime/TuiRuntime.hpp>

namespace endo::shell
{

namespace
{
    /// True if @p r is the `WaitFlag::NoHang` "no state change yet" sentinel
    /// (`PosixProcessManager::wait` returns `exitCode == -1` with neither signaled
    /// nor stopped set when the child is still running). A genuine termination
    /// always has a non-negative exit code, or the signaled/stopped flag set, so
    /// this cannot alias a real result.
    [[nodiscard]] bool isNoChange(platform::WaitResult const& r) noexcept
    {
        return r.exitCode == -1 && !r.signaled && !r.stopped;
    }
} // namespace

coro::Task<platform::WaitResult> waitProcessAsync(tui::runtime::TuiRuntime* runtime,
                                                  platform::ProcessManager* pm,
                                                  platform::ProcessId pid,
                                                  std::chrono::milliseconds pollInterval)
{
    while (true)
    {
        auto const result =
            pm->wait(pid, platform::WaitFlags { platform::WaitFlag::NoHang, platform::WaitFlag::Untraced });
        if (!result)
        {
            // The child could not be waited for (e.g. it was already reaped
            // elsewhere, ECHILD). "Gone" is exactly the terminal state we want, and
            // its real status is unknowable here, so resolve to a benign "terminated"
            // result (exit code 128, the conventional "killed/unknown" base) rather
            // than propagating the error.
            co_return platform::WaitResult { .exitCode = 128 };
        }
        if (!isNoChange(*result))
            co_return *result; // exited, signalled, or stopped

        // Still running: suspend on the reactor's timer, then poll again. A cancelled
        // delay throws OperationCancelled out of this loop, unwinding the wait (the
        // right behaviour for a losing whenAny/withTimeout arm).
        co_await runtime->delay(pollInterval);
    }
}

} // namespace endo::shell
