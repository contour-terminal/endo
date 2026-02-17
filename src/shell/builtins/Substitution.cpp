// SPDX-License-Identifier: Apache-2.0
#include <shell/Pipe.hpp>
#include <shell/Platform.hpp>
#include <shell/Process.hpp>
#include <shell/Shell.hpp>

#if !defined(_WIN32)
    #include <unistd.h>
#endif

namespace endo
{

void Shell::builtinSubstStart(CoreVM::Params&)
{
    _substitutionCapture.emplace();

    auto pipeResult = createPipe();
    if (!pipeResult.has_value())
    {
        error("Failed to create pipe for command substitution: {}", toString(pipeResult.error()));
        _substitutionCapture.reset();
        return;
    }

    _substitutionCapture->pipe = std::move(pipeResult.value());
    _substitutionCapture->savedStdout = _currentPipelineBuilder.defaultStdoutFd;
    _currentPipelineBuilder.defaultStdoutFd = _substitutionCapture->pipe->writer();
}

void Shell::builtinSubstEnd(CoreVM::Params& context)
{
    if (!_substitutionCapture)
    {
        error("Command substitution end without matching start");
        context.setResult(std::string {});
        return;
    }

    _currentPipelineBuilder.defaultStdoutFd = _substitutionCapture->savedStdout;
    _substitutionCapture->pipe->closeWriter();

    std::string output;
    char buffer[4096];
    while (true)
    {
        auto const bytesRead = platformRead(_substitutionCapture->pipe->reader(), buffer, sizeof(buffer));
        if (bytesRead <= 0)
            break;
        output.append(buffer, static_cast<size_t>(bytesRead));
    }

    _substitutionCapture->pipe->closeReader();

    while (!output.empty() && output.back() == '\n')
        output.pop_back();

    _substitutionCapture.reset();

    context.setResult(std::move(output));
}

void Shell::builtinProcSubstFork(CoreVM::Params& context)
{
    bool const isWrite = context.getBool(1);

    auto pipeResult = createPipe();
    if (!pipeResult.has_value())
    {
        error("Failed to create pipe for process substitution: {}", toString(pipeResult.error()));
        context.setResult(CoreVM::CoreNumber(-1));
        return;
    }

    auto pipe = std::move(pipeResult.value());

#if !defined(_WIN32)
    pid_t const pid = fork();

    if (pid < 0)
    {
        error("Failed to fork for process substitution: {}", strerror(errno));
        context.setResult(CoreVM::CoreNumber(-1));
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

        context.setResult(CoreVM::CoreNumber(0));
        return;
    }

    _procSubstChildPids.push_back(static_cast<ProcessId>(pid));

    NativeHandle exposedFd;
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

    context.setResult(CoreVM::CoreNumber(1));
#else
    error("Process substitution not implemented on Windows");
    context.setResult(CoreVM::CoreNumber(-1));
#endif
}

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
