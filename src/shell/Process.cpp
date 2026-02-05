// SPDX-License-Identifier: Apache-2.0
#include "Process.hpp"

#if !defined(_WIN32)
    #include <sys/wait.h>

    #include <fcntl.h>
    #include <unistd.h>
    #if defined(__linux__)
        #include <linux/close_range.h>
    #endif
#endif

#include <algorithm>

namespace endo
{

#if !defined(_WIN32)

PosixProcessManager& PosixProcessManager::instance()
{
    static PosixProcessManager pm;
    return pm;
}

std::expected<ProcessId, ShellError> PosixProcessManager::spawn(SpawnConfig const& config)
{
    std::vector<char const*> argv;
    argv.reserve(config.arguments.size() + 2);
    argv.push_back(config.program.c_str());
    for (auto const& arg: config.arguments)
        argv.push_back(arg.c_str());
    argv.push_back(nullptr);

    ProcessId const pid = fork();
    if (pid == -1)
        return std::unexpected(ShellError::ForkFailed);

    if (pid == 0)
    {
        // Child process
        if (config.processGroup.has_value())
            setpgid(0, config.processGroup.value());

        if (config.stdinFd != STDIN_FILENO)
            dup2(config.stdinFd, STDIN_FILENO);
        if (config.stdoutFd != STDOUT_FILENO)
            dup2(config.stdoutFd, STDOUT_FILENO);
        if (config.stderrFd != STDERR_FILENO)
            dup2(config.stderrFd, STDERR_FILENO);

        if (config.closeExtraFds)
        {
            if (config.keepOpenFds.empty())
                closeExtraHandles();
            else
                closeExtraHandlesExcept(config.keepOpenFds);
        }

        execvp(config.program.c_str(), const_cast<char* const*>(argv.data()));
        _exit(EXIT_FAILURE);
    }

    return pid;
}

std::expected<WaitResult, ShellError> PosixProcessManager::wait(ProcessId pid)
{
    int wstatus = 0;
    if (waitpid(pid, &wstatus, 0) == -1)
        return std::unexpected(ShellError::WaitFailed);

    WaitResult result;
    if (WIFSIGNALED(wstatus))
    {
        result.signaled = true;
        result.signal = WTERMSIG(wstatus);
        result.exitCode = 128 + result.signal;
    }
    else if (WIFEXITED(wstatus))
    {
        result.exitCode = WEXITSTATUS(wstatus);
    }
    return result;
}

std::expected<void, ShellError> PosixProcessManager::changeDirectory(std::filesystem::path const& path)
{
    if (chdir(path.c_str()) != 0)
        return std::unexpected(ShellError::FileNotFound);
    return {};
}

std::expected<NativeHandle, ShellError> PosixProcessManager::openFile(std::filesystem::path const& path,
                                                                       int flags,
                                                                       int mode)
{
    NativeHandle const fd = open(path.c_str(), flags, mode);
    if (fd == InvalidHandle)
        return std::unexpected(ShellError::IoError);
    return fd;
}

std::expected<ProcessId, ShellError> PosixProcessManager::createSession()
{
    ProcessId const sid = setsid();
    if (sid == InvalidProcessId)
        return std::unexpected(ShellError::ForkFailed);
    return sid;
}

std::expected<void, ShellError> PosixProcessManager::setProcessGroup(ProcessId pid, ProcessId pgid)
{
    if (setpgid(pid, pgid) == -1)
        return std::unexpected(ShellError::ExecutionFailed);
    return {};
}

std::expected<void, ShellError> PosixProcessManager::duplicateFd(NativeHandle src, NativeHandle dst)
{
    if (dup2(src, dst) == -1)
        return std::unexpected(ShellError::HandleDuplicationFailed);
    return {};
}

void PosixProcessManager::closeHandle(NativeHandle handle) noexcept
{
    if (handle != InvalidHandle)
        ::close(handle);
}

void PosixProcessManager::closeExtraHandles() noexcept
{
#if defined(__linux__)
    close_range(STDERR_FILENO + 1, ~0U, 0);
#else
    // Fallback for non-Linux: manually close FDs 3 and above
    int const maxFd = static_cast<int>(sysconf(_SC_OPEN_MAX));
    for (int fd = STDERR_FILENO + 1; fd < maxFd; ++fd)
        ::close(fd);
#endif
}

void PosixProcessManager::closeExtraHandlesExcept(std::vector<NativeHandle> const& keepOpen) noexcept
{
    int const maxFd = static_cast<int>(sysconf(_SC_OPEN_MAX));
    for (int fd = STDERR_FILENO + 1; fd < maxFd; ++fd)
    {
        bool const shouldKeep = std::find(keepOpen.begin(), keepOpen.end(), fd) != keepOpen.end();
        if (!shouldKeep)
            ::close(fd);
    }
}

#endif // !_WIN32

} // namespace endo
