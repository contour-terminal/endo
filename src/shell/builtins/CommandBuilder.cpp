// SPDX-License-Identifier: Apache-2.0
#include <shell/Error.hpp>
#include <shell/Shell.hpp>
#include <shell/util/CommandResolver.hpp>

#include <endo-language/LogCategories.hpp>

#include <crispy/utils.h>

#include <filesystem>
#include <format>
#include <print>

#include <platform/Pipe.hpp>
#include <platform/Process.hpp>
#include <platform/Types.hpp>

#if !defined(_WIN32)
    #include <sys/wait.h>

    #include <fcntl.h>
    #include <unistd.h>
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

void Shell::builtinCmdStart(CoreVM::Params& context)
{
    _cmdBuilderStack.emplace_back();
    cmdBuilderArgs().push_back(context.getString(1));
}

void Shell::builtinCmdArg(CoreVM::Params& context)
{
    cmdBuilderArgs().push_back(context.getString(1));
}

void Shell::builtinCmdExec(CoreVM::Params& context)
{
    if (cmdBuilderArgs().empty())
    {
        error("No command to execute");
        _exitCode = EXIT_FAILURE;
        context.setResult(CoreVM::CoreNumber(EXIT_FAILURE));
        return;
    }

    std::string const& program = cmdBuilderArgs().at(0);

    // Handle inline builtins (mirrors builtinCallProcess in ProcessExecution.cpp)
    {
        NativeHandle const outputFd =
            _redirectState.getEffectiveStdoutFd(_currentPipelineBuilder.defaultStdoutFd, _processManager);
        NativeHandle const inputFd =
            _redirectState.getEffectiveStdinFd(_currentPipelineBuilder.defaultStdinFd, _processManager);

        auto handled = true;
        if (program == "echo")
            _exitCode = executeInlineEcho(cmdBuilderArgs(), outputFd);
        else if (program == "cat")
            _exitCode = executeInlineCat(cmdBuilderArgs(), outputFd, inputFd);
        else if (program == "sleep")
            _exitCode = executeInlineSleep(cmdBuilderArgs(), outputFd);
        else if (program == "rm")
            _exitCode = executeInlineRm(cmdBuilderArgs(), outputFd);
        else if (program == "mkdir")
            _exitCode = executeInlineMkdir(cmdBuilderArgs(), outputFd);
        else if (program == "cp")
            _exitCode = executeInlineCp(cmdBuilderArgs(), outputFd);
        else if (program == "mv")
            _exitCode = executeInlineMv(cmdBuilderArgs(), outputFd);
        else if (program == "find")
            _exitCode = executeInlineFind(cmdBuilderArgs(), outputFd);
        else
            handled = false;

        if (handled)
        {
            if (!_cmdBuilderStack.empty())
                _cmdBuilderStack.pop_back();
            context.setResult(CoreVM::CoreNumber(_exitCode));
            return;
        }
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
    config.arguments = std::vector<std::string>(cmdBuilderArgs().begin() + 1, cmdBuilderArgs().end());
    config.stdinFd = _currentPipelineBuilder.defaultStdinFd;
    config.stdoutFd = _currentPipelineBuilder.defaultStdoutFd;
    config.closeExtraFds = true;
    config.keepOpenFds = _procSubstExposedFds;

    applyRedirects(config);

    // Build command string for job table
    std::string command;
    for (size_t i = 0; i < cmdBuilderArgs().size(); ++i)
    {
        if (i > 0)
            command += ' ';
        command += cmdBuilderArgs().at(i);
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
        debugLog()()("child process stopped\n");
    else
        debugLog()()("child process exited with code {}\n", _exitCode);

    cleanupProcSubst();

    if (!_cmdBuilderStack.empty())
        _cmdBuilderStack.pop_back();
    context.setResult(CoreVM::CoreNumber(_exitCode));
}

void Shell::builtinCmdExecPiped(CoreVM::Params& context)
{
    bool const lastInChain = context.getBool(1);

    if (cmdBuilderArgs().empty())
    {
        error("No command to execute");
        _exitCode = EXIT_FAILURE;
        context.setResult(CoreVM::CoreNumber(EXIT_FAILURE));
        return;
    }

    std::string const& program = cmdBuilderArgs().at(0);
    auto const [stdinFd, stdoutFd] = _currentPipelineBuilder.requestShellPipe(lastInChain);

    // Handle inline builtins in pipeline (mirrors builtinCallProcessShellPiped)
    {
        auto handled = true;
        if (program == "echo")
            _exitCode = executeInlineEcho(cmdBuilderArgs(), stdoutFd);
        else if (program == "cat")
            _exitCode = executeInlineCat(cmdBuilderArgs(), stdoutFd, stdinFd);
        else if (program == "sleep")
            _exitCode = executeInlineSleep(cmdBuilderArgs(), stdoutFd);
        else if (program == "rm")
            _exitCode = executeInlineRm(cmdBuilderArgs(), stdoutFd);
        else if (program == "mkdir")
            _exitCode = executeInlineMkdir(cmdBuilderArgs(), stdoutFd);
        else if (program == "cp")
            _exitCode = executeInlineCp(cmdBuilderArgs(), stdoutFd);
        else if (program == "mv")
            _exitCode = executeInlineMv(cmdBuilderArgs(), stdoutFd);
        else if (program == "find")
            _exitCode = executeInlineFind(cmdBuilderArgs(), stdoutFd);
        else
            handled = false;

        if (handled)
        {
            finalizePipelineBuiltin(lastInChain, cmdBuilderArgs(), program, context);
            if (!_cmdBuilderStack.empty())
                _cmdBuilderStack.pop_back();
            return;
        }
    }

    auto const programPath = resolveProgram(program);

    if (!programPath.has_value())
    {
        error("{}: {}", program, toString(programPath.error()));
        _exitCode = EXIT_FAILURE;
        context.setResult(CoreVM::CoreNumber(EXIT_FAILURE));
        _currentPipelineBuilder.closePipeFdsInParent();
        return;
    }

    SpawnConfig config;
    config.program = *programPath;
    config.arguments = std::vector<std::string>(cmdBuilderArgs().begin() + 1, cmdBuilderArgs().end());
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
        _currentPipelineBuilder.closePipeFdsInParent();
        return;
    }

    ProcessId const pid = spawnResult.value();
    _leftPid = _rightPid;
    _rightPid = pid;
    _currentProcessGroupPids.push_back(pid);
    _currentPipelineBuilder.closePipeFdsInParent();

    // Track command string for job table display
    std::string cmdString;
    for (size_t i = 0; i < cmdBuilderArgs().size(); ++i)
    {
        if (i > 0)
            cmdString += ' ';
        cmdString += cmdBuilderArgs().at(i);
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

    if (!_cmdBuilderStack.empty())
        _cmdBuilderStack.pop_back();
    context.setResult(CoreVM::CoreNumber(_exitCode));
}

std::vector<std::string>& Shell::cmdBuilderArgs()
{
    if (_cmdBuilderStack.empty())
        _cmdBuilderStack.emplace_back();
    return _cmdBuilderStack.back();
}

void Shell::cleanupProcSubst()
{
#if !defined(_WIN32)
    for (ProcessId childPid: _procSubstChildPids)
    {
        int status = 0;
        waitpid(static_cast<pid_t>(childPid), &status, 0);
    }
    _procSubstChildPids.clear();

    for (NativeHandle fd: _procSubstExposedFds)
    {
        if (fd >= 0)
            close(fd);
    }
    _procSubstExposedFds.clear();

    _procSubstFdPath.clear();
#else
    // Windows: wait for process substitution children and close handles
    for (ProcessId childPid: _procSubstChildPids)
        (void) _processManager.wait(childPid);
    _procSubstChildPids.clear();

    for (NativeHandle handle: _procSubstExposedFds)
    {
        if (handle != InvalidHandle)
            _processManager.closeHandle(handle);
    }
    _procSubstExposedFds.clear();

    _procSubstFdPath.clear();
#endif
}

std::expected<Shell::ForegroundResult, ShellError> Shell::runForeground(SpawnConfig& config,
                                                                        std::string const& command)
{
#if !defined(_WIN32)
    // Create new process group with child as leader
    config.processGroup = 0;

    auto spawnResult = _processManager.spawn(config);
    if (!spawnResult)
        return std::unexpected(toShellError(spawnResult.error()));

    ProcessId const pid = spawnResult.value();
    ProcessId const pgid = pid; // Child is process group leader

    // Give terminal control to child's process group
    auto const setFgResult = _processManager.setForegroundPgrp(_tty.inputFd(), pgid);
    if (!setFgResult)
    {
        debugLog()()("Failed to set foreground process group: {}", toString(setFgResult.error()));
    }

    // Wait with WUNTRACED to detect stopped processes (Ctrl+Z)
    auto waitResult = _processManager.wait(pid, WaitFlag::Untraced);

    // Restore shell's terminal control
    auto const restoreFgResult = _processManager.setForegroundPgrp(_tty.inputFd(), _shellPgid);
    if (!restoreFgResult)
    {
        debugLog()()("Failed to restore shell foreground: {}", toString(restoreFgResult.error()));
    }

    if (!waitResult)
        return std::unexpected(toShellError(waitResult.error()));

    ForegroundResult result {
        .exitCode = waitResult->exitCode,
        .stopped = waitResult->stopped,
        .pid = pid,
        .pgid = pgid,
    };

    // If process was stopped, add to job table
    if (waitResult->stopped)
    {
        (void) jobTable.addJob(pgid, { pid }, command);
        jobTable.updateJobState(pid, *waitResult);
        std::println("\n[{}]+  Stopped                 {}", jobTable.getCurrentJob()->id, command);
    }

    return result;
#else
    // Windows: no job control, just spawn and wait
    auto spawnResult = _processManager.spawn(config);
    if (!spawnResult)
        return std::unexpected(toShellError(spawnResult.error()));

    auto waitResult = _processManager.wait(spawnResult.value());
    if (!waitResult)
        return std::unexpected(toShellError(waitResult.error()));

    return ForegroundResult {
        .exitCode = waitResult->exitCode,
        .stopped = false,
        .pid = spawnResult.value(),
        .pgid = 0,
    };
#endif
}

void Shell::applyRedirects(SpawnConfig& config)
{
    for (auto& entry: _redirectState.entries)
    {
        switch (entry.type)
        {
            case RedirectState::Type::InputFile: {
                auto const result = _processManager.openFile(entry.path, O_RDONLY);
                if (!result.has_value())
                {
                    error("Failed to open '{}' for reading: {}", entry.path, toString(result.error()));
                    continue;
                }
                entry.openedFd = result.value();
                if (entry.targetFd == STDIN_FILENO)
                    config.stdinFd = entry.openedFd;
                break;
            }
            case RedirectState::Type::OutputFile: {
                int const oflags =
                    entry.append ? (O_WRONLY | O_CREAT | O_APPEND) : (O_WRONLY | O_CREAT | O_TRUNC);
                auto const result = _processManager.openFile(entry.path, oflags);
                if (!result.has_value())
                {
                    error("Failed to open '{}' for writing: {}", entry.path, toString(result.error()));
                    continue;
                }
                entry.openedFd = result.value();
                if (entry.sourceFd == STDOUT_FILENO)
                    config.stdoutFd = entry.openedFd;
                else if (entry.sourceFd == STDERR_FILENO)
                    config.stderrFd = entry.openedFd;
                break;
            }
            case RedirectState::Type::FdDup: {
                if (entry.sourceFd == STDERR_FILENO && entry.targetFd == STDOUT_FILENO)
                    config.stderrFd = config.stdoutFd;
                else if (entry.sourceFd == STDOUT_FILENO && entry.targetFd == STDERR_FILENO)
                    config.stdoutFd = config.stderrFd;
                break;
            }
            case RedirectState::Type::HereDoc:
            case RedirectState::Type::HereString: {
                auto pipeResult = createPipe();
                if (!pipeResult.has_value())
                {
                    error("Failed to create pipe for here-string: {}", toString(pipeResult.error()));
                    continue;
                }
                auto pipe = std::move(pipeResult.value());

                std::string const& content = entry.content;
                auto const written = platformWrite(pipe->writer(), content.data(), content.size());
                if (written < 0)
                {
                    error("Failed to write to here-string pipe: {}", strerror(errno));
                    continue;
                }

                if (entry.type == RedirectState::Type::HereString && !content.empty()
                    && content.back() != '\n')
                {
                    platformWrite(pipe->writer(), "\n", 1);
                }

                pipe->closeWriter();

                entry.openedFd = pipe->releaseReader();
                if (entry.targetFd == STDIN_FILENO)
                    config.stdinFd = entry.openedFd;
                break;
            }
        }
    }
}

std::expected<std::filesystem::path, ShellError> Shell::resolveProgram(std::string const& program) const
{
    auto programPath = std::filesystem::path(program);

    // Absolute path or explicitly relative path (contains a directory separator).
    if (programPath.is_absolute() || program.contains('/')
#if defined(_WIN32)
        || program.contains('\\')
#endif
    )
    {
        std::error_code ec;
        if (std::filesystem::exists(programPath, ec)
            && (std::filesystem::is_regular_file(programPath, ec)
                || std::filesystem::is_symlink(programPath, ec)))
        {
            return programPath;
        }
        return std::unexpected(ShellError::ProgramNotFound);
    }

    // Bare command name: search PATH (with PATHEXT awareness on Windows).
    if (!_env.get("PATH").has_value())
        return std::unexpected(ShellError::VariableNotFound);

    auto const resolver = CommandResolver(_env);
    auto const found = resolver.findInPath(program);
    if (found.empty())
        return std::unexpected(ShellError::ProgramNotFound);

    debugLog()()("Found program: {}", found);
    return std::filesystem::path(found);
}

} // namespace endo
