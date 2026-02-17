// SPDX-License-Identifier: Apache-2.0
#include <shell/Error.hpp>
#include <shell/Platform.hpp>
#include <shell/Process.hpp>
#include <shell/Shell.hpp>

#include <endo-language/LogCategories.hpp>

#include <format>
#include <print>

#if !defined(_WIN32)
    #include <csignal>

    #include <sys/wait.h>
#endif

namespace
{

auto& debugLog()
{
    return endo::log::shellDebug();
}

} // namespace

namespace endo
{

void Shell::builtinJobs(CoreVM::Params& context)
{
    auto const jobs = jobTable.listJobs();
    for (auto const* job: jobs)
    {
        char const marker = (jobTable.getCurrentJob() && job->id == jobTable.getCurrentJob()->id)     ? '+'
                            : (jobTable.getPreviousJob() && job->id == jobTable.getPreviousJob()->id) ? '-'
                                                                                                      : ' ';
        std::string stateStr;
        switch (job->state)
        {
            case JobState::Running: stateStr = "Running"; break;
            case JobState::Stopped: stateStr = "Stopped"; break;
            case JobState::Done: stateStr = "Done"; break;
            case JobState::Terminated: stateStr = std::format("Terminated ({})", job->signal); break;
        }

        std::println("[{}]{} {}\t{}", job->id, marker, stateStr, job->command);
    }

    _exitCode = 0;
    context.setResult(CoreVM::CoreNumber(0));
}

void Shell::builtinFg(CoreVM::Params& context)
{
#if !defined(_WIN32)
    // Get job to foreground
    Job* job = nullptr;
    if (context.count() > 1)
    {
        int const jobId = static_cast<int>(context.getInt(1));
        job = jobTable.getJob(jobId);
        if (!job)
        {
            error("fg: %{}: no such job", jobId);
            _exitCode = 1;
            context.setResult(CoreVM::CoreNumber(1));
            return;
        }
    }
    else
    {
        job = jobTable.getCurrentJob();
        if (!job)
        {
            error("fg: no current job");
            _exitCode = 1;
            context.setResult(CoreVM::CoreNumber(1));
            return;
        }
    }

    // Print the command being resumed
    std::println("{}", job->command);

    // Give the job's process group control of the terminal
    auto const setFgResult = _processManager.setForegroundPgrp(_tty.inputFd(), job->pgid);
    if (!setFgResult.has_value())
    {
        error("fg: failed to set foreground process group: {}", toString(setFgResult.error()));
    }

    // If the job was stopped, send SIGCONT
    if (job->state == JobState::Stopped)
    {
        auto const sigResult = _processManager.sendSignal(-static_cast<int>(job->pgid), SIGCONT);
        if (!sigResult.has_value())
        {
            error("fg: failed to send SIGCONT: {}", toString(sigResult.error()));
        }
        job->state = JobState::Running;
    }

    // Wait for the job to complete or stop
    for (ProcessId const pid: job->pids)
    {
        int status = 0;
        pid_t const waitedPid = waitpid(static_cast<pid_t>(pid), &status, WUNTRACED);
        if (waitedPid > 0)
        {
            WaitResult result;
            if (WIFEXITED(status))
            {
                result.exitCode = WEXITSTATUS(status);
                _exitCode = result.exitCode;
            }
            else if (WIFSIGNALED(status))
            {
                result.signaled = true;
                result.signal = WTERMSIG(status);
                _exitCode = 128 + result.signal;
            }
            else if (WIFSTOPPED(status))
            {
                result.stopped = true;
                result.signal = WSTOPSIG(status);
            }
            jobTable.updateJobState(pid, result);
        }
    }

    // Restore shell's terminal control
    auto const restoreResult = _processManager.setForegroundPgrp(_tty.inputFd(), _shellPgid);
    if (!restoreResult.has_value())
    {
        error("fg: failed to restore shell foreground: {}", toString(restoreResult.error()));
    }

    context.setResult(CoreVM::CoreNumber(_exitCode));
#else
    // Windows: basic foreground job support
    Job* job = nullptr;
    if (context.count() > 1)
    {
        int const jobId = static_cast<int>(context.getInt(1));
        job = jobTable.getJob(jobId);
        if (!job)
        {
            error("fg: %{}: no such job", jobId);
            _exitCode = 1;
            context.setResult(CoreVM::CoreNumber(1));
            return;
        }
    }
    else
    {
        job = jobTable.getCurrentJob();
        if (!job)
        {
            error("fg: no current job");
            _exitCode = 1;
            context.setResult(CoreVM::CoreNumber(1));
            return;
        }
    }

    // Print the command being resumed
    std::println("{}", job->command);

    // If the job was stopped (suspended threads), resume it
    if (job->state == JobState::Stopped)
    {
        for (ProcessId const pid: job->pids)
        {
            auto const sigResult = _processManager.sendSignal(pid, SIGCONT);
            if (!sigResult.has_value())
                debugLog()()("fg: failed to resume process {}: {}", pid, toString(sigResult.error()));
        }
        job->state = JobState::Running;
    }

    // Wait for the job to complete
    for (ProcessId const pid: job->pids)
    {
        auto const waitResult = _processManager.wait(pid);
        if (waitResult.has_value())
        {
            _exitCode = waitResult->exitCode;
            jobTable.updateJobState(pid, *waitResult);
        }
    }

    context.setResult(CoreVM::CoreNumber(_exitCode));
#endif
}

void Shell::builtinBg(CoreVM::Params& context)
{
#if !defined(_WIN32)
    // Get job to background
    Job* job = nullptr;
    if (context.count() > 1)
    {
        int const jobId = static_cast<int>(context.getInt(1));
        job = jobTable.getJob(jobId);
        if (!job)
        {
            error("bg: %{}: no such job", jobId);
            _exitCode = 1;
            context.setResult(CoreVM::CoreNumber(1));
            return;
        }
    }
    else
    {
        job = jobTable.getCurrentJob();
        if (!job)
        {
            error("bg: no current job");
            _exitCode = 1;
            context.setResult(CoreVM::CoreNumber(1));
            return;
        }
    }

    if (job->state != JobState::Stopped)
    {
        error("bg: job {} not stopped", job->id);
        _exitCode = 1;
        context.setResult(CoreVM::CoreNumber(1));
        return;
    }

    // Print the command being resumed
    std::println("[{}]+ {} &", job->id, job->command);

    // Send SIGCONT to the process group
    auto const sigResult = _processManager.sendSignal(-static_cast<int>(job->pgid), SIGCONT);
    if (!sigResult.has_value())
    {
        error("bg: failed to send SIGCONT: {}", toString(sigResult.error()));
        _exitCode = 1;
        context.setResult(CoreVM::CoreNumber(1));
        return;
    }

    job->state = JobState::Running;
    _exitCode = 0;
    context.setResult(CoreVM::CoreNumber(0));
#else
    // Windows: basic background resume support
    Job* job = nullptr;
    if (context.count() > 1)
    {
        int const jobId = static_cast<int>(context.getInt(1));
        job = jobTable.getJob(jobId);
        if (!job)
        {
            error("bg: %{}: no such job", jobId);
            _exitCode = 1;
            context.setResult(CoreVM::CoreNumber(1));
            return;
        }
    }
    else
    {
        job = jobTable.getCurrentJob();
        if (!job)
        {
            error("bg: no current job");
            _exitCode = 1;
            context.setResult(CoreVM::CoreNumber(1));
            return;
        }
    }

    if (job->state != JobState::Stopped)
    {
        error("bg: job {} not stopped", job->id);
        _exitCode = 1;
        context.setResult(CoreVM::CoreNumber(1));
        return;
    }

    // Print the command being resumed
    std::println("[{}]+ {} &", job->id, job->command);

    // Resume suspended process threads
    for (ProcessId const pid: job->pids)
    {
        auto const sigResult = _processManager.sendSignal(pid, SIGCONT);
        if (!sigResult.has_value())
        {
            error("bg: failed to resume process {}: {}", pid, toString(sigResult.error()));
            _exitCode = 1;
            context.setResult(CoreVM::CoreNumber(1));
            return;
        }
    }

    job->state = JobState::Running;
    _exitCode = 0;
    context.setResult(CoreVM::CoreNumber(0));
#endif
}

void Shell::builtinWait(CoreVM::Params& context)
{
#if !defined(_WIN32)
    if (context.count() >= 1)
    {
        // Wait for specific job
        int const jobId = static_cast<int>(context.getInt(1));
        Job* job = jobTable.getJob(jobId);
        if (!job)
        {
            error("wait: %{}: no such job", jobId);
            _exitCode = 127;
            context.setResult(CoreVM::CoreNumber(127));
            return;
        }

        // Wait for all processes in the job
        for (ProcessId const pid: job->pids)
        {
            int status = 0;
            waitpid(static_cast<pid_t>(pid), &status, 0);
            if (WIFEXITED(status))
                _exitCode = WEXITSTATUS(status);
            else if (WIFSIGNALED(status))
                _exitCode = 128 + WTERMSIG(status);
        }

        job->state = JobState::Done;
        job->exitCode = _exitCode;
    }
    else
    {
        // Wait for all background jobs
        auto jobs = jobTable.listJobs();
        for (auto const* constJob: jobs)
        {
            if (constJob->state != JobState::Running && constJob->state != JobState::Stopped)
                continue;

            // Get mutable job
            Job* job = jobTable.getJob(constJob->id);
            if (!job)
                continue;

            for (ProcessId const pid: job->pids)
            {
                int status = 0;
                waitpid(static_cast<pid_t>(pid), &status, 0);
                if (WIFEXITED(status))
                    _exitCode = WEXITSTATUS(status);
                else if (WIFSIGNALED(status))
                    _exitCode = 128 + WTERMSIG(status);
            }

            job->state = JobState::Done;
            job->exitCode = _exitCode;
        }
    }

    context.setResult(CoreVM::CoreNumber(_exitCode));
#else
    // Windows: wait for background jobs using ProcessManager
    if (context.count() >= 1)
    {
        // Wait for specific job
        int const jobId = static_cast<int>(context.getInt(1));
        Job* job = jobTable.getJob(jobId);
        if (!job)
        {
            error("wait: %{}: no such job", jobId);
            _exitCode = 127;
            context.setResult(CoreVM::CoreNumber(127));
            return;
        }

        for (ProcessId const pid: job->pids)
        {
            auto const waitResult = _processManager.wait(pid);
            if (waitResult.has_value())
            {
                _exitCode = waitResult->exitCode;
                jobTable.updateJobState(pid, *waitResult);
            }
        }

        job->state = JobState::Done;
        job->exitCode = _exitCode;
    }
    else
    {
        // Wait for all background jobs
        auto jobs = jobTable.listJobs();
        for (auto const* constJob: jobs)
        {
            if (constJob->state != JobState::Running && constJob->state != JobState::Stopped)
                continue;

            Job* job = jobTable.getJob(constJob->id);
            if (!job)
                continue;

            for (ProcessId const pid: job->pids)
            {
                auto const waitResult = _processManager.wait(pid);
                if (waitResult.has_value())
                {
                    _exitCode = waitResult->exitCode;
                    jobTable.updateJobState(pid, *waitResult);
                }
            }

            job->state = JobState::Done;
            job->exitCode = _exitCode;
        }
    }

    context.setResult(CoreVM::CoreNumber(_exitCode));
#endif
}

void Shell::builtinCmdExecPipedBackground(CoreVM::Params& context)
{
#if !defined(_WIN32)
    std::string const command = context.getString(1);

    if (cmdBuilderArgs().empty())
    {
        error("No command to execute");
        _exitCode = EXIT_FAILURE;
        context.setResult(CoreVM::CoreNumber(EXIT_FAILURE));
        return;
    }

    std::string const& program = cmdBuilderArgs().at(0);
    auto const programPath = resolveProgram(program);

    if (!programPath.has_value())
    {
        error("{}: {}", program, toString(programPath.error()));
        _exitCode = EXIT_FAILURE;
        context.setResult(CoreVM::CoreNumber(EXIT_FAILURE));
        return;
    }

    // Close any existing pipeline pipe (background jobs start fresh)
    _currentPipelineBuilder.currentPipe.reset();

    SpawnConfig config;
    config.program = *programPath;
    config.arguments = std::vector<std::string>(cmdBuilderArgs().begin() + 1, cmdBuilderArgs().end());
    config.stdinFd = _currentPipelineBuilder.defaultStdinFd;
    config.stdoutFd = _currentPipelineBuilder.defaultStdoutFd;
    config.processGroup = std::make_optional<ProcessId>(0); // Create new process group
    config.closeExtraFds = true;
    config.keepOpenFds = _procSubstExposedFds;

    applyRedirects(config);

    auto const spawnResult = _processManager.spawn(config);
    if (!spawnResult.has_value())
    {
        error("Failed to spawn {}: {}", program, toString(spawnResult.error()));
        _exitCode = EXIT_FAILURE;
        context.setResult(CoreVM::CoreNumber(EXIT_FAILURE));
        return;
    }

    ProcessId const pid = spawnResult.value();
    _lastBackgroundPid = pid;

    // Add to job table
    std::vector<ProcessId> pids;
    pids.push_back(pid);
    int const jobId = jobTable.addJob(pid, std::move(pids), command);

    // Print job info
    std::println("[{}] {}", jobId, pid);

    if (!_cmdBuilderStack.empty())
        _cmdBuilderStack.pop_back();

    // Background jobs return 0 immediately
    _exitCode = 0;
    context.setResult(CoreVM::CoreNumber(0));
#else
    // Windows: background execution using CreateProcess without job control
    std::string const command = context.getString(1);

    if (cmdBuilderArgs().empty())
    {
        error("No command to execute");
        _exitCode = EXIT_FAILURE;
        context.setResult(CoreVM::CoreNumber(EXIT_FAILURE));
        return;
    }

    std::string const& program = cmdBuilderArgs().at(0);
    auto const programPath = resolveProgram(program);

    if (!programPath.has_value())
    {
        error("{}: {}", program, toString(programPath.error()));
        _exitCode = EXIT_FAILURE;
        context.setResult(CoreVM::CoreNumber(EXIT_FAILURE));
        return;
    }

    // Close any existing pipeline pipe (background jobs start fresh)
    _currentPipelineBuilder.currentPipe.reset();

    SpawnConfig config;
    config.program = *programPath;
    config.arguments = std::vector<std::string>(cmdBuilderArgs().begin() + 1, cmdBuilderArgs().end());
    config.stdinFd = _currentPipelineBuilder.defaultStdinFd;
    config.stdoutFd = _currentPipelineBuilder.defaultStdoutFd;
    config.processGroup = std::make_optional<ProcessId>(0); // Create new process group
    config.closeExtraFds = true;

    applyRedirects(config);

    auto const spawnResult = _processManager.spawn(config);
    if (!spawnResult.has_value())
    {
        error("Failed to spawn {}: {}", program, toString(spawnResult.error()));
        _exitCode = EXIT_FAILURE;
        context.setResult(CoreVM::CoreNumber(EXIT_FAILURE));
        return;
    }

    ProcessId const pid = spawnResult.value();
    _lastBackgroundPid = pid;

    // Add to job table
    std::vector<ProcessId> pids;
    pids.push_back(pid);
    int const jobId = jobTable.addJob(pid, std::move(pids), command);

    // Print job info
    std::println("[{}] {}", jobId, pid);

    if (!_cmdBuilderStack.empty())
        _cmdBuilderStack.pop_back();

    // Background jobs return 0 immediately
    _exitCode = 0;
    context.setResult(CoreVM::CoreNumber(0));
#endif
}

} // namespace endo
