// SPDX-License-Identifier: Apache-2.0
#include "Process.hpp"

#if !defined(_WIN32)
    #include <algorithm>
    #include <cerrno>
    #include <csignal>

    #include <sys/wait.h>

    #include <fcntl.h>
    #include <termios.h>
    #include <unistd.h>
    #if defined(__linux__)
        #include <linux/close_range.h>
    #endif
#endif

namespace endo::platform
{

#if !defined(_WIN32)

PosixProcessManager& PosixProcessManager::instance()
{
    static PosixProcessManager pm;
    return pm;
}

std::expected<ProcessId, PlatformError> PosixProcessManager::spawn(SpawnConfig const& config)
{
    std::vector<char const*> argv;
    argv.reserve(config.arguments.size() + 2);
    argv.push_back(config.program.c_str());
    for (auto const& arg: config.arguments)
        argv.push_back(arg.c_str());
    argv.push_back(nullptr);

    ProcessId const pid = fork();
    if (pid == -1)
        return std::unexpected(PlatformError::ForkFailed);

    if (pid == 0)
    {
        // Child process

        // Unblock signals that the shell blocked for signalfd.
        // Child processes need to receive job control signals (e.g., SIGTSTP from Ctrl+Z)
        // and SIGINT (Ctrl+C). The signal mask is inherited across fork() and preserved across exec().
        sigset_t mask {};
        sigemptyset(&mask);
        sigaddset(&mask, SIGCHLD);
        sigaddset(&mask, SIGTSTP);
        sigaddset(&mask, SIGCONT);
        sigaddset(&mask, SIGINT);
        sigprocmask(SIG_UNBLOCK, &mask, nullptr);

        // Reset signals to default behavior.
        // SIGINT: Interrupt (Ctrl+C) - child should be interruptible
        // SIGTSTP: Terminal stop (Ctrl+Z)
        // SIGTTIN: Background process reading from terminal
        // SIGTTOU: Background process writing to terminal
        signal(SIGINT, SIG_DFL);
        signal(SIGTSTP, SIG_DFL);
        signal(SIGTTIN, SIG_DFL);
        signal(SIGTTOU, SIG_DFL);

        if (config.processGroup.has_value())
            setpgid(0, config.processGroup.value());

        if (config.stdinFd != InvalidHandle && config.stdinFd != STDIN_FILENO)
            dup2(config.stdinFd, STDIN_FILENO);
        if (config.stdoutFd != InvalidHandle && config.stdoutFd != STDOUT_FILENO)
            dup2(config.stdoutFd, STDOUT_FILENO);
        if (config.stderrFd != InvalidHandle && config.stderrFd != STDERR_FILENO)
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

    // Parent process — also set child's process group to eliminate race condition.
    // Both parent and child call setpgid(); whichever runs first wins, the other
    // harmlessly fails with EACCES (child already exec'd) or ESRCH.
    if (config.processGroup.has_value())
    {
        pid_t const pgid = config.processGroup.value() == 0 ? pid : config.processGroup.value();
        setpgid(pid, pgid);
    }

    return pid;
}

std::expected<WaitResult, PlatformError> PosixProcessManager::wait(ProcessId pid, WaitFlags flags)
{
    int waitFlags = 0;
    if (flags.test(WaitFlag::NoHang))
        waitFlags |= WNOHANG;
    if (flags.test(WaitFlag::Untraced))
        waitFlags |= WUNTRACED;

    int wstatus = 0;
    pid_t const waitResult = waitpid(pid, &wstatus, waitFlags);

    if (waitResult == -1)
        return std::unexpected(PlatformError::WaitFailed);

    if (waitResult == 0 && flags.test(WaitFlag::NoHang))
    {
        // No state change yet (WNOHANG case)
        WaitResult result;
        result.exitCode = -1; // Indicate no change
        return result;
    }

    WaitResult result;
    if (WIFEXITED(wstatus))
    {
        result.exitCode = WEXITSTATUS(wstatus);
    }
    else if (WIFSIGNALED(wstatus))
    {
        result.signaled = true;
        result.signal = WTERMSIG(wstatus);
        result.exitCode = 128 + result.signal;
    }
    else if (WIFSTOPPED(wstatus))
    {
        result.stopped = true;
        result.signal = WSTOPSIG(wstatus);
    }
    return result;
}

std::expected<std::optional<std::pair<ProcessId, WaitResult>>, PlatformError> PosixProcessManager::waitPgid(
    ProcessId pgid, WaitFlags flags)
{
    int waitFlags = 0;
    if (flags.test(WaitFlag::NoHang))
        waitFlags |= WNOHANG;
    if (flags.test(WaitFlag::Untraced))
        waitFlags |= WUNTRACED;

    int wstatus = 0;
    pid_t const result = waitpid(-pgid, &wstatus, waitFlags);

    if (result == -1)
    {
        if (errno == ECHILD)
            return std::nullopt; // No children in this process group
        return std::unexpected(PlatformError::WaitFailed);
    }

    if (result == 0)
        return std::nullopt; // No state change (WNOHANG)

    WaitResult waitResult;
    if (WIFEXITED(wstatus))
    {
        waitResult.exitCode = WEXITSTATUS(wstatus);
    }
    else if (WIFSIGNALED(wstatus))
    {
        waitResult.signaled = true;
        waitResult.signal = WTERMSIG(wstatus);
        waitResult.exitCode = 128 + waitResult.signal;
    }
    else if (WIFSTOPPED(wstatus))
    {
        waitResult.stopped = true;
        waitResult.signal = WSTOPSIG(wstatus);
    }

    return std::make_pair(static_cast<ProcessId>(result), waitResult);
}

std::expected<void, PlatformError> PosixProcessManager::sendSignal(ProcessId pid, int signal)
{
    if (kill(pid, signal) == -1)
        return std::unexpected(PlatformError::SignalFailed);
    return {};
}

std::expected<ProcessId, PlatformError> PosixProcessManager::getForegroundPgrp(NativeHandle fd)
{
    pid_t const pgid = tcgetpgrp(fd);
    if (pgid == -1)
        return std::unexpected(PlatformError::TerminalControlFailed);
    return static_cast<ProcessId>(pgid);
}

std::expected<void, PlatformError> PosixProcessManager::setForegroundPgrp(NativeHandle fd, ProcessId pgid)
{
    if (tcsetpgrp(fd, pgid) == -1)
        return std::unexpected(PlatformError::TerminalControlFailed);
    return {};
}

std::expected<NativeHandle, PlatformError> PosixProcessManager::openFile(std::filesystem::path const& path,
                                                                         int flags,
                                                                         int mode)
{
    NativeHandle const fd = open(path.c_str(), flags, mode);
    if (fd == InvalidHandle)
        return std::unexpected(PlatformError::IoError);
    return fd;
}

std::expected<ProcessId, PlatformError> PosixProcessManager::createSession()
{
    ProcessId const sid = setsid();
    if (sid == InvalidProcessId)
        return std::unexpected(PlatformError::SessionCreationFailed);
    return sid;
}

std::expected<void, PlatformError> PosixProcessManager::setProcessGroup(ProcessId pid, ProcessId pgid)
{
    if (setpgid(pid, pgid) == -1)
        return std::unexpected(PlatformError::ProcessGroupFailed);
    return {};
}

std::expected<void, PlatformError> PosixProcessManager::duplicateFd(NativeHandle src, NativeHandle dst)
{
    if (dup2(src, dst) == -1)
        return std::unexpected(PlatformError::HandleDuplicationFailed);
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
        bool const shouldKeep = std::ranges::find(keepOpen, fd) != keepOpen.end();
        if (!shouldKeep)
            ::close(fd);
    }
}

#endif // !_WIN32

} // namespace endo::platform
