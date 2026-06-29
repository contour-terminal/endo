// SPDX-License-Identifier: Apache-2.0
#include <shell/Shell.hpp>

#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <string>

#include <platform/Pipe.hpp>
#include <platform/Process.hpp>
#include <platform/Types.hpp>

#if !defined(_WIN32)
    #include <sys/stat.h>

    #include <fcntl.h>
    #include <unistd.h>
#endif

namespace endo
{

void Shell::builtinSubstStart(CoreVM::Params&)
{
    // Push a new capture. A stack (rather than a single slot) keeps nested
    // substitutions like `$(echo $(rpm -qa))` correct: the inner capture is
    // popped on completion, restoring the outer capture untouched.
    auto& capture = _substitutionCaptures.emplace_back();

    // Capture into an anonymous temp file rather than a pipe: a regular file has
    // no fixed kernel buffer, so the captured command(s) never block on write no
    // matter how much they emit. A pipe-backed capture deadlocks at ~64KB because
    // the writer blocks while the shell has not yet drained the reader.
#if !defined(_WIN32)
    auto const tmpDir = []() -> std::string {
        if (auto const* t = std::getenv("TMPDIR"); t != nullptr && *t != '\0')
            return t;
        return "/tmp";
    }();
    auto templ = tmpDir + "/endo-subst-XXXXXX";
    auto const fd = ::mkstemp(templ.data());
    if (fd < 0)
    {
        error("Failed to create temporary file for command substitution: {}", std::strerror(errno));
        _substitutionCaptures.pop_back();
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
        error("Failed to create pipe for command substitution: {}", toString(pipeResult.error()));
        _substitutionCaptures.pop_back();
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
    _currentPipelineBuilder.defaultStdoutFd = capture.savedStdout;

    std::string output;
#if !defined(_WIN32)
    // POSIX: read the temp file back from the start. A failed lseek means the
    // offset is indeterminate, so skip the read rather than capture garbage.
    auto const fd = capture.fd;
    if (fd != InvalidHandle && ::lseek(fd, 0, SEEK_SET) != static_cast<off_t>(-1))
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
    // Windows: close our writer end, then drain the pipe reader to EOF.
    if (capture.pipe)
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
