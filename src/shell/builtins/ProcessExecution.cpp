// SPDX-License-Identifier: Apache-2.0
#include <shell/Error.hpp>
#include <shell/Shell.hpp>

#include <endo-language/LogCategories.hpp>

#include <CoreVM/CoreVM.hpp>

#include <format>
#include <print>

#include <platform/Process.hpp>
#include <platform/Types.hpp>

#if !defined(_WIN32)
    #include <sys/wait.h>
#endif

using namespace std::placeholders;

namespace
{

auto& debugLog()
{
    return endo::log::shellDebug();
}

} // namespace

namespace endo
{

void Shell::builtinCallProcess(CoreVM::Params& context)
{
    CoreVM::CoreStringArray const& args = context.getStringArray(1);
    std::string const& program = args.at(0);

    // Get the effective output/input fds considering redirects
    NativeHandle const outputFd =
        _redirectState.getEffectiveStdoutFd(_currentPipelineBuilder.defaultStdoutFd, _processManager);
    NativeHandle const inputFd =
        _redirectState.getEffectiveStdinFd(_currentPipelineBuilder.defaultStdinFd, _processManager);

    // Handle inline builtins
    if (program == "echo")
    {
        _exitCode = executeInlineEcho(args, outputFd);
        context.setResult(CoreVM::CoreNumber(_exitCode));
        return;
    }

    if (program == "cat")
    {
        _exitCode = executeInlineCat(args, outputFd, inputFd);
        context.setResult(CoreVM::CoreNumber(_exitCode));
        return;
    }

    if (program == "sleep")
    {
        _exitCode = executeInlineSleep(args, outputFd);
        context.setResult(CoreVM::CoreNumber(_exitCode));
        return;
    }

    if (program == "rm")
    {
        _exitCode = executeInlineRm(args, outputFd);
        context.setResult(CoreVM::CoreNumber(_exitCode));
        return;
    }

    if (program == "mkdir")
    {
        _exitCode = executeInlineMkdir(args, outputFd);
        context.setResult(CoreVM::CoreNumber(_exitCode));
        return;
    }

    // Check if this is a registered shell function
    if (_registeredFunctions.contains(program))
    {
        CoreVM::Function* fn = _currentProgram->findFunction(program);
        if (!fn)
        {
            error("{}: function not found (was it defined in a previous command?)", program);
            _exitCode = 127;
            context.setResult(CoreVM::CoreNumber(127));
            return;
        }

        auto savedPositionalParams = _positionalParameters;
        _positionalParameters.clear();
        _positionalParameters.push_back(program);
        for (size_t i = 1; i < args.size(); ++i)
            _positionalParameters.push_back(args.at(i));

        auto runner = CoreVM::Runner(fn,
                                     nullptr,
                                     &_globals,
                                     CoreVM::RuntimeConfig::defaultConfig(),
                                     std::bind(&Shell::trace, this, _1, _2, _3));
        runner.run();

        _positionalParameters = std::move(savedPositionalParams);

        context.setResult(CoreVM::CoreNumber(_exitCode));
        return;
    }

    auto const programPath = resolveProgram(program);

    if (!programPath.has_value())
    {
        error("{}: {}", program, toString(programPath.error()));
        _exitCode = EXIT_FAILURE;
        context.setResult(CoreVM::CoreNumber(EXIT_FAILURE));
        return;
    }

    SpawnConfig config;
    config.program = *programPath;
    config.arguments = std::vector<std::string>(args.begin() + 1, args.end());
    config.stdinFd = _currentPipelineBuilder.defaultStdinFd;
    config.stdoutFd = _currentPipelineBuilder.defaultStdoutFd;
    config.closeExtraFds = true;
    config.keepOpenFds = _procSubstExposedFds;

    applyRedirects(config);

    // Build command string for job table
    std::string command;
    for (size_t i = 0; i < args.size(); ++i)
    {
        if (i > 0)
            command += ' ';
        command += args.at(i);
    }

    auto const fgResult = runForeground(config, command);
    if (!fgResult.has_value())
    {
        error("Failed to run {}: {}", program, toString(fgResult.error()));
        _exitCode = EXIT_FAILURE;
        context.setResult(CoreVM::CoreNumber(EXIT_FAILURE));
        return;
    }

    _exitCode = fgResult->exitCode;
    if (fgResult->stopped)
        debugLog()()("child process stopped with signal {}\n", fgResult->exitCode);
    else
        debugLog()()("child process exited with code {}\n", _exitCode);

    cleanupProcSubst();

    context.setResult(CoreVM::CoreNumber(_exitCode));
}

void Shell::builtinCallProcessShellPiped(CoreVM::Params& context)
{
    bool const lastInChain = context.getBool(1);
    CoreVM::CoreStringArray const& args = context.getStringArray(2);

    std::string const& program = args.at(0);

    // Handle inline builtins in pipeline
    if (program == "echo")
    {
        auto const [stdinFd, stdoutFd] = _currentPipelineBuilder.requestShellPipe(lastInChain);
        _exitCode = executeInlineEcho(args, stdoutFd);
        finalizePipelineBuiltin(lastInChain, args, "echo", context);
        return;
    }

    if (program == "cat")
    {
        auto const [stdinFd, stdoutFd] = _currentPipelineBuilder.requestShellPipe(lastInChain);
        _exitCode = executeInlineCat(args, stdoutFd, stdinFd);
        finalizePipelineBuiltin(lastInChain, args, "cat", context);
        return;
    }

    if (program == "sleep")
    {
        auto const [stdinFd, stdoutFd] = _currentPipelineBuilder.requestShellPipe(lastInChain);
        _exitCode = executeInlineSleep(args, stdoutFd);
        finalizePipelineBuiltin(lastInChain, args, "sleep", context);
        return;
    }

    if (program == "rm")
    {
        auto const [stdinFd, stdoutFd] = _currentPipelineBuilder.requestShellPipe(lastInChain);
        _exitCode = executeInlineRm(args, stdoutFd);
        finalizePipelineBuiltin(lastInChain, args, "rm", context);
        return;
    }

    if (program == "mkdir")
    {
        auto const [stdinFd, stdoutFd] = _currentPipelineBuilder.requestShellPipe(lastInChain);
        _exitCode = executeInlineMkdir(args, stdoutFd);
        finalizePipelineBuiltin(lastInChain, args, "mkdir", context);
        return;
    }

    auto const programPath = resolveProgram(program);

    if (!programPath.has_value())
    {
        error("{}: {}", program, toString(programPath.error()));
        _exitCode = EXIT_FAILURE;
        context.setResult(CoreVM::CoreNumber(EXIT_FAILURE));
        return;
    }

    auto const [stdinFd, stdoutFd] = _currentPipelineBuilder.requestShellPipe(lastInChain);

    SpawnConfig config;
    config.program = *programPath;
    config.arguments = std::vector<std::string>(args.begin() + 1, args.end());
    config.stdinFd = stdinFd;
    config.stdoutFd = stdoutFd;
    config.processGroup = !_currentProcessGroupPids.empty()
                              ? std::make_optional(_currentProcessGroupPids.front())
                              : std::make_optional<ProcessId>(0);
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
    _leftPid = _rightPid;
    _rightPid = pid;
    _currentProcessGroupPids.push_back(pid);

    // Track command string for job table display
    std::string cmdString;
    for (size_t i = 0; i < args.size(); ++i)
    {
        if (i > 0)
            cmdString += ' ';
        cmdString += args.at(i);
    }
    _pipelineCommands.push_back(std::move(cmdString));

    if (lastInChain)
    {
#if !defined(_WIN32)
        // Build command string for job table from _pipelineCommands
        std::string command;
        for (size_t i = 0; i < _pipelineCommands.size(); ++i)
        {
            if (i > 0)
                command += " | ";
            command += _pipelineCommands[i];
        }

        // Process group leader is the first process
        ProcessId const pgid = _currentProcessGroupPids.front();

        // Give terminal control to the pipeline's process group
        auto const setFgResult = _processManager.setForegroundPgrp(_tty.inputFd(), pgid);
        if (!setFgResult)
            debugLog()()("Failed to set foreground process group: {}", toString(setFgResult.error()));

        bool anyStopped = false;
        for (ProcessId const processPid: _currentProcessGroupPids)
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
                debugLog()()("child process {} exited with signal {}\n", processPid, waitResult->signal);
            else
                debugLog()()("child process {} exited with code {}\n", processPid, _exitCode);
        }

        // Restore shell's terminal control
        auto const restoreFgResult = _processManager.setForegroundPgrp(_tty.inputFd(), _shellPgid);
        if (!restoreFgResult)
            debugLog()()("Failed to restore shell foreground: {}", toString(restoreFgResult.error()));

        // If any process was stopped, add the whole pipeline to job table
        if (anyStopped)
        {
            (void) jobTable.addJob(pgid, _currentProcessGroupPids, command);
            // Mark the job as stopped
            WaitResult stoppedResult { .exitCode = 0, .stopped = true };
            jobTable.updateJobState(_currentProcessGroupPids.front(), stoppedResult);
            std::println("\n[{}]+  Stopped                 {}", jobTable.getCurrentJob()->id, command);
        }

        _pipelineCommands.clear();
#else
        for (ProcessId const processPid: _currentProcessGroupPids)
        {
            auto const waitResult = _processManager.wait(processPid);
            if (!waitResult.has_value())
            {
                error("Failed to wait for process {}: {}", processPid, toString(waitResult.error()));
                continue;
            }

            _exitCode = waitResult->exitCode;
            if (waitResult->signaled)
                debugLog()()("child process {} exited with signal {}\n", processPid, waitResult->signal);
            else
                debugLog()()("child process {} exited with code {}\n", processPid, _exitCode);
        }
#endif
        _currentProcessGroupPids.clear();
        _leftPid = std::nullopt;
        _rightPid = std::nullopt;

        cleanupProcSubst();
    }

    context.setResult(CoreVM::CoreNumber(_exitCode));
}

} // namespace endo
