// SPDX-License-Identifier: Apache-2.0
#include <shell/Shell.hpp>

#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>

#include <platform/Pipe.hpp>
#include <platform/Process.hpp>
#include <platform/Types.hpp>

#if !defined(_WIN32)
    #include <shell/AsyncProcessWait.hpp>

    #include <endo-language/LogCategories.hpp>

    #include <tui/runtime/PollEventSource.hpp>
    #include <tui/runtime/TuiRuntime.hpp>
    #include <tui/runtime/WithTimeout.hpp>

    #include <chrono>
    #include <csignal>
    #include <cstdint>
    #include <format>
    #include <ranges>
    #include <vector>

    #include <sys/stat.h>

    #include <fcntl.h>
    #include <unistd.h>

    #include <coro/Task.hpp>
    #include <coro/WhenAny.hpp>
#endif

namespace endo
{

void Shell::builtinSubstStart(CoreVM::Params&)
{
    // Push a new capture. A stack (rather than a single slot) keeps nested
    // substitutions like `$(echo $(rpm -qa))` correct: the inner capture is
    // popped on completion, restoring the outer capture untouched. The abort flag
    // lives on the capture, so it is per-substitution by construction.
    //
    // The capture stays on the stack even if sink creation below fails: the IR emits
    // subst_start / command / subst_end unconditionally, so a start that pop_back()'d
    // itself on failure would leave the matching subst_end operating on the OUTER
    // capture — restoring the outer's saved stdout early and popping it. Instead we leave
    // an un-redirected capture (fd/pipe unset) that subst_end reads as empty and pops as
    // its own, keeping every start balanced with its end.
    auto& capture = _substitutionCaptures.emplace_back();

    // Capture into an anonymous temp file rather than a pipe: a regular file has
    // no fixed kernel buffer, so the captured command(s) never block on write no
    // matter how much they emit. A pipe-backed capture deadlocks at ~64KB because
    // the writer blocks while the shell has not yet drained the reader.
#if !defined(_WIN32)
    // Resolve the temp directory through the injected environment provider (not raw
    // getenv), so it honors a test's TestEnvironmentProvider and the shell's own env.
    auto const tmpDir = [this]() -> std::string {
        if (auto const t = _env.get("TMPDIR"); t && !t->empty())
            return *t;
        return "/tmp";
    }();
    auto templ = tmpDir + "/endo-subst-XXXXXX";
    auto const fd = ::mkstemp(templ.data());
    if (fd < 0)
    {
        // No writable temp dir (read-only/full /tmp, restrictive sandbox): leave the
        // capture un-redirected. The command runs writing to the real stdout and the
        // substitution yields empty, rather than corrupting the outer capture.
        error("Failed to create temporary file for command substitution: {}", std::strerror(errno));
        return;
    }
    // Unlink immediately: the fd keeps the file alive until it is closed, and no
    // path lingers on disk (cleaned up even on crash).
    ::unlink(templ.c_str());

    capture.fd = fd;
    capture.savedStdout = _currentPipelineBuilder.defaultStdoutFd;
    _currentPipelineBuilder.defaultStdoutFd = fd;
#else
    // Windows: keep the pipe-backed capture (prior behavior). A temp-file sink
    // would need extra handle-inheritance plumbing; the >64KB deadlock the POSIX
    // temp file guards against does not gate the Windows builds here.
    auto pipeResult = createPipe();
    if (!pipeResult.has_value())
    {
        // Leave the capture un-redirected (see the POSIX branch): subst_end pops its own.
        error("Failed to create pipe for command substitution: {}", toString(pipeResult.error()));
        return;
    }
    capture.pipe = std::move(pipeResult.value());
    capture.savedStdout = _currentPipelineBuilder.defaultStdoutFd;
    _currentPipelineBuilder.defaultStdoutFd = capture.pipe->writer();
#endif
}

void Shell::builtinSubstEnd(CoreVM::Params& context)
{
    if (_substitutionCaptures.empty())
    {
        error("Command substitution end without matching start");
        context.setResult(std::string {});
        return;
    }

    auto& capture = _substitutionCaptures.back();

    // A capture whose sink creation failed in subst_start never redirected stdout (fd
    // and pipe both unset, savedStdout still InvalidHandle). Restoring from it would
    // clobber the live defaultStdoutFd with InvalidHandle. Detect that case, yield empty,
    // and pop this (our own) capture — leaving any outer capture's redirection intact.
#if !defined(_WIN32)
    bool const sinkCreated = capture.fd != InvalidHandle;
#else
    bool const sinkCreated = static_cast<bool>(capture.pipe);
#endif
    if (!sinkCreated)
    {
        _substitutionCaptures.pop_back();
        context.setResult(std::string {});
        return;
    }

    _currentPipelineBuilder.defaultStdoutFd = capture.savedStdout;

    std::string output;

    // If this substitution was aborted or timed out, the child was killed mid-write,
    // so the temp file holds only a partial capture. Return an empty result rather
    // than feeding half a value into the completion.
    bool const aborted = capture.aborted;
#if !defined(_WIN32)
    // POSIX: read the temp file back from the start. A failed lseek means the
    // offset is indeterminate, so skip the read rather than capture garbage.
    auto const fd = capture.fd;
    if (!aborted && fd != InvalidHandle && ::lseek(fd, 0, SEEK_SET) != static_cast<off_t>(-1))
    {
        // The temp file's size is known exactly and cheaply, so reserve up front to
        // avoid repeated reallocations while appending (large captures like
        // `$(rpm -qa)` can be hundreds of KB).
        if (struct ::stat st {}; ::fstat(fd, &st) == 0 && st.st_size > 0)
            output.reserve(static_cast<std::size_t>(st.st_size));

        std::array<char, 4096> buffer {};
        while (true)
        {
            auto const n = ::read(fd, buffer.data(), buffer.size());
            if (n < 0)
            {
                // Retry a read interrupted by a signal (EINTR) — the shell
                // handles SIGCHLD/SIGWINCH, either of which can interrupt this
                // read; treating it as EOF would silently truncate the capture.
                if (errno == EINTR)
                    continue;
                break;
            }
            if (n == 0)
                break; // genuine EOF
            output.append(buffer.data(), static_cast<std::size_t>(n));
        }
    }
#else
    // Windows: close our writer end, then drain the pipe reader to EOF. (The
    // async/cancellable wait is POSIX-only, so `aborted` is never set here.)
    if (!aborted && capture.pipe)
    {
        capture.pipe->closeWriter();
        std::array<char, 4096> buffer {};
        while (true)
        {
            auto const n = platformRead(capture.pipe->reader(), buffer.data(), buffer.size());
            if (n <= 0)
                break;
            output.append(buffer.data(), static_cast<std::size_t>(n));
        }
        capture.pipe->closeReader();
    }
#endif

    while (!output.empty() && output.back() == '\n')
        output.pop_back();

    // Pop the capture: its destructor closes the temp-file fd (POSIX) / pipe.
    _substitutionCaptures.pop_back();

    context.setResult(std::move(output));
}

#if !defined(_WIN32)

namespace
{
    /// Shared debug-log category for this translation unit (mirrors the helper in
    /// the other process-execution builtins).
    auto& debugLog()
    {
        return endo::log::shellDebug();
    }

    /// Reactor task that resolves when the abort key (Ctrl+C, 0x03) is read from
    /// @p ttyFd. Used as a @c whenAny arm against the child wait so Ctrl+C cancels an
    /// in-flight completion.
    ///
    /// The terminal is in raw mode, so any keystrokes the user types while the completion
    /// runs are read here. Rather than drop them (which swallowed the user's typed-ahead
    /// input), every non-abort byte is appended to @p pushbackOut so the caller can
    /// re-stage it into the terminal input after the completion finishes. Bytes at and
    /// after the abort byte in the triggering read are discarded (they belong to the
    /// cancel, not to typed-ahead input).
    ///
    /// Only Ctrl+C aborts — deliberately NOT Escape. An Escape byte can be a genuine
    /// cancel, but it is also the first byte of every arrow/function-key escape
    /// sequence, and a single @c read may not contain the whole sequence, so treating
    /// ESC as abort made cursor keys cancel completions. Since ghost text no longer
    /// shells out (only an explicit Tab does), the only keystrokes seen here are ones
    /// pressed while a Tab-completion is genuinely running, and Ctrl+C is the
    /// conventional, race-free cancel. Throws @c OperationCancelled if the child wins
    /// the race first (its @c waitReadable is cancelled).
    ///
    /// @param pushbackOut Buffer receiving the non-abort bytes read here (must outlive
    ///                    the task).
    coro::Task<void> watchTtyForAbortKey(tui::runtime::TuiRuntime* runtime,
                                         NativeHandle ttyFd,
                                         std::string* pushbackOut)
    {
        while (true)
        {
            co_await runtime->waitReadable(ttyFd);
            std::array<char, 32> buffer {};
            auto const n = ::read(ttyFd, buffer.data(), buffer.size());
            if (n <= 0)
                continue;
            for (auto const i: std::views::iota(0uz, static_cast<std::size_t>(n)))
            {
                if (buffer.at(i) == 0x03)
                    co_return; // Ctrl+C seen: win the race (bytes from here on are dropped)
                // Typed-ahead byte (a character, or an ESC-prefixed cursor key): preserve
                // it so it is not lost, to be re-fed to the line editor after completion.
                pushbackOut->push_back(buffer.at(i));
            }
        }
    }

    /// Waits for every process in @p ids in order; stores the last one's exit code
    /// (the pipeline's exit status) into @p exitCodeOut. A @c whenAny arm.
    coro::Task<void> waitPipelineChildren(tui::runtime::TuiRuntime* runtime,
                                          platform::ProcessManager* pm,
                                          std::vector<platform::ProcessId> ids,
                                          int* exitCodeOut)
    {
        WaitResult last {};
        for (auto const pid: ids)
            last = co_await shell::waitProcessAsync(runtime, pm, pid);
        *exitCodeOut = last.exitCode;
    }

    /// Races the pipeline wait against the abort-key watcher.
    /// @param pushbackOut Buffer receiving typed-ahead bytes read by the abort watcher.
    /// @return The winning arm's index: 0 if the children finished first, 1 if an
    ///         abort key won (the `whenAny` order below).
    coro::Task<std::size_t> raceChildrenVsAbort(tui::runtime::TuiRuntime* runtime,
                                                platform::ProcessManager* pm,
                                                std::vector<platform::ProcessId> ids,
                                                NativeHandle ttyFd,
                                                int* exitCodeOut,
                                                std::string* pushbackOut)
    {
        co_return co_await coro::whenAny(waitPipelineChildren(runtime, pm, ids, exitCodeOut),
                                         watchTtyForAbortKey(runtime, ttyFd, pushbackOut));
    }
} // namespace

void Shell::waitForPipeline(std::vector<ProcessId> const& pids, ProcessId pgid, std::string const& command)
{
    if (_completionWaitCancellable)
    {
        // During a tab-completion, wait through the cancellable async path (see
        // awaitSubstitutionPipeline). No setForegroundPgrp: the completer child stays
        // background so the abort-key watcher owns the TTY, and it is never a job.
        _exitCode = awaitSubstitutionPipeline(pids, pgid);
        return;
    }

    // Interactive foreground wait: hand the terminal to the pipeline's group so the
    // child receives the tty's Ctrl+C / Ctrl+Z directly, wait each process with
    // WUNTRACED, restore terminal control, then register a stopped pipeline as a job.
    auto const setFgResult = _processManager.setForegroundPgrp(_tty.inputFd(), pgid);
    if (!setFgResult)
        debugLog()()("Failed to set foreground process group: {}", toString(setFgResult.error()));

    bool anyStopped = false;
    for (ProcessId const processPid: pids)
    {
        auto const waitResult = _processManager.wait(processPid, WaitFlag::Untraced);
        if (!waitResult.has_value())
        {
            error("Failed to wait for process {}: {}", processPid, toString(waitResult.error()));
            continue;
        }

        _exitCode = waitResult->exitCode;
        if (waitResult->stopped)
        {
            anyStopped = true;
            debugLog()()("child process {} stopped\n", processPid);
        }
        else if (waitResult->signaled)
        {
            debugLog()()("child process {} exited with signal {}\n", processPid, waitResult->signal);
        }
        else
        {
            debugLog()()("child process {} exited with code {}\n", processPid, _exitCode);
        }
    }

    auto const restoreFgResult = _processManager.setForegroundPgrp(_tty.inputFd(), _shellPgid);
    if (!restoreFgResult)
        debugLog()()("Failed to restore shell foreground: {}", toString(restoreFgResult.error()));

    if (anyStopped)
    {
        (void) jobTable.addJob(pgid, pids, command);
        WaitResult const stoppedResult { .exitCode = 0, .stopped = true };
        jobTable.updateJobState(pids.front(), stoppedResult);
        _tty.writeToStdout(
            std::format("\n[{}]+  Stopped                 {}\n", jobTable.getCurrentJob()->id, command));
    }
}

int Shell::awaitSubstitutionPipeline(std::vector<ProcessId> const& pids, ProcessId pgid)
{
    if (pids.empty())
        return _exitCode;

    // A nested reactor drives the wait (mirrors builtinHttpServe): the enclosing
    // main prompt loop is not pumping while this synchronous builtin runs, so this
    // reactor is the only active reaper of `pids` — a plain waitpid(WNOHANG) poll is
    // safe with no signalfd/onSigchld contention.
    tui::runtime::PollEventSource source;
    tui::runtime::TuiRuntime runtime { source };

    auto const ttyFd = _tty.inputFd();
    auto* const pm = &_processManager;

    enum class Outcome : std::uint8_t
    {
        Exited,
        Aborted,
        TimedOut
    };

    int childExitCode = _exitCode;
    Outcome outcome = Outcome::Exited;

    // Effective deadline. A cold fetch (no usable cached list to fall back on) uses the
    // tighter cold budget so a first-Tab `$(dnf repoquery)` stays snappy; a refresh that
    // has a stale list to fall back on uses the larger overall budget, since waiting
    // longer is cheap when a failure just serves the stale data. A 0 budget disables
    // that budget; both 0 → abort-key only. This is what makes both knobs effective:
    // previously the unconditional min() pinned every fetch to the cold budget, leaving
    // shell_completion_timeout inert. See shell_completion_timeout /
    // shell_completion_cold_timeout and ScriptedCompleter's pre-fetch hook.
    auto const effectiveTimeout = [this] {
        auto const overall = _completionTimeoutMs;
        auto const cold = _completionColdTimeoutMs;
        auto const budget = _completionColdFetch ? cold : overall;
        auto const other = _completionColdFetch ? overall : cold;
        if (budget.count() > 0)
            return budget;
        // Chosen budget disabled: fall back to the other one (0 → abort-key only).
        return other;
    }();

    // Typed-ahead bytes the abort watcher reads off the TTY while the completion runs are
    // accumulated here (not dropped) and re-staged into the terminal input after the wait,
    // so the user's keystrokes during a completion are not lost.
    std::string& pushback = _completionPushbackBytes;

    if (effectiveTimeout.count() > 0)
    {
        // Bound the child-vs-abort race by the timeout.
        auto const finished = runtime.blockOn(tui::runtime::withTimeout(
            &runtime,
            raceChildrenVsAbort(&runtime, pm, pids, ttyFd, &childExitCode, &pushback),
            effectiveTimeout));
        if (!finished.has_value())
            outcome = Outcome::TimedOut;
        else
            outcome = (*finished == 0) ? Outcome::Exited : Outcome::Aborted;
    }
    else
    {
        // Timeout disabled (0): race only children vs the abort key.
        auto const winner =
            runtime.blockOn(raceChildrenVsAbort(&runtime, pm, pids, ttyFd, &childExitCode, &pushback));
        outcome = (winner == 0) ? Outcome::Exited : Outcome::Aborted;
    }

    if (outcome == Outcome::Exited)
    {
        // A successful substitution clears any Aborted/TimedOut left by an EARLIER
        // `$(...)` in the same completer body. Without this, a completer that runs two
        // substitutions where the first times out but the second succeeds would keep the
        // stale TimedOut status, and its usable final result would be wrongly treated as
        // non-cacheable (forcing a re-fetch on every Tab). The status must reflect the
        // outcome that actually produced the completions.
        _lastCompletionOutcome = CompleterExecutionStatus::Ok;
        _exitCode = childExitCode;
        return _exitCode;
    }

    // Aborted or timed out: mark the active capture so subst_end returns empty, then
    // tear down the pipeline's process group and notify. The teardown calls are
    // best-effort — the target may already be gone — so their result is ignored.
    if (!_substitutionCaptures.empty())
        _substitutionCaptures.back().aborted = true;
    [[maybe_unused]] auto const termResult = _processManager.sendSignal(-pgid, SIGTERM);

    // Give the group a brief grace to exit on SIGTERM, then escalate to SIGKILL. A
    // short nested reactor bounds the grace so a stubborn child cannot hang teardown.
    {
        tui::runtime::PollEventSource graceSource;
        tui::runtime::TuiRuntime graceRuntime { graceSource };
        // `childExitCode` is written again here but discarded: this path always
        // returns the abort code (130). It is passed only because it is the helper's
        // required out-param sink.
        auto const reaped = graceRuntime.blockOn(
            tui::runtime::withTimeout(&graceRuntime,
                                      waitPipelineChildren(&graceRuntime, pm, pids, &childExitCode),
                                      std::chrono::milliseconds { 200 }));
        if (!reaped)
        {
            [[maybe_unused]] auto const killResult = _processManager.sendSignal(-pgid, SIGKILL);
            // Final synchronous reap so no zombie survives once the main loop resumes.
            for (auto const pid: pids)
            {
                [[maybe_unused]] auto const reapResult = _processManager.wait(pid);
            }
        }
    }

    // Record the outcome so executeCompleterFunction can avoid caching this run's
    // (empty) result. Set before notifying so the flag is live regardless of sink.
    _lastCompletionOutcome = (outcome == Outcome::TimedOut) ? CompleterExecutionStatus::TimedOut
                                                            : CompleterExecutionStatus::Aborted;

    auto const kind = (outcome == Outcome::TimedOut) ? NotificationKind::TimedOut : NotificationKind::Aborted;
    auto const message = (outcome == Outcome::TimedOut)
                             ? std::format("completion timed out after {}ms", effectiveTimeout.count())
                             : std::string { "completion aborted" };
    _completionNotifier->notify(kind, message);

    _exitCode = 130; // 128 + SIGINT: conventional "aborted by user"
    return _exitCode;
}

#endif // !_WIN32

void Shell::builtinProcSubstFork(CoreVM::Params& context)
{
    bool const isWrite = context.getBool(1);

    auto pipeResult = createPipe();
    if (!pipeResult.has_value())
    {
        error("Failed to create pipe for process substitution: {}", toString(pipeResult.error()));
        context.setResult(static_cast<CoreVM::CoreNumber>(-1));
        return;
    }

    auto pipe = std::move(pipeResult.value());

#if !defined(_WIN32)
    pid_t const pid = fork();

    if (pid < 0)
    {
        error("Failed to fork for process substitution: {}", strerror(errno));
        context.setResult(static_cast<CoreVM::CoreNumber>(-1));
        return;
    }

    if (pid == 0)
    {
        if (isWrite)
        {
            pipe->closeWriter();
            dup2(pipe->reader(), STDIN_FILENO);
            pipe->closeReader();
        }
        else
        {
            pipe->closeReader();
            dup2(pipe->writer(), STDOUT_FILENO);
            pipe->closeWriter();
        }

        context.setResult(static_cast<CoreVM::CoreNumber>(0));
        return;
    }

    _procSubstChildPids.push_back(static_cast<ProcessId>(pid));

    NativeHandle exposedFd = InvalidHandle;
    if (isWrite)
    {
        pipe->closeReader();
        exposedFd = pipe->releaseWriter();
    }
    else
    {
        pipe->closeWriter();
        exposedFd = pipe->releaseReader();
    }

    _procSubstExposedFds.push_back(exposedFd);

    #if defined(__linux__)
    _procSubstFdPath = std::format("/proc/self/fd/{}", exposedFd);
    #else
    _procSubstFdPath = std::format("/dev/fd/{}", exposedFd);
    #endif

    context.setResult(static_cast<CoreVM::CoreNumber>(1));
#else
    error(
        "Process substitution is not supported on Windows. Consider using temporary files or pipes instead.");
    context.setResult(CoreVM::CoreNumber(-1));
#endif
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
void Shell::builtinProcSubstExit(CoreVM::Params&)
{
#if !defined(_WIN32)
    _exit(0);
#endif
}

void Shell::builtinProcSubstGetPath(CoreVM::Params& context)
{
    context.setResult(_procSubstFdPath);
}

void Shell::builtinProcSubstCleanup(CoreVM::Params&)
{
    cleanupProcSubst();
}

} // namespace endo
