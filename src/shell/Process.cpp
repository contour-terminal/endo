// SPDX-License-Identifier: Apache-2.0
module;

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "Error.h"
#include "Platform.h"

#if !defined(_WIN32)
    #include <sys/wait.h>

    #include <fcntl.h>
    #include <unistd.h>
    #if defined(__linux__)
        #include <linux/close_range.h>
    #endif
#endif

export module Process;

namespace endo
{

/// Configuration for spawning a new process.
export struct SpawnConfig
{
    std::filesystem::path program;                        ///< Path to the program to execute
    std::vector<std::string> arguments;                   ///< Command line arguments (excluding program name)
    NativeHandle stdinFd = 0;                             ///< File descriptor/handle for stdin
    NativeHandle stdoutFd = 1;                            ///< File descriptor/handle for stdout
    NativeHandle stderrFd = 2;                            ///< File descriptor/handle for stderr
    std::optional<ProcessId> processGroup = std::nullopt; ///< Process group ID (0 for new group)
    bool closeExtraFds = true;                            ///< Close file descriptors > 2 after fork
};

/// Abstract interface for process management operations.
///
/// This interface abstracts platform-specific process operations, enabling
/// testability and potential cross-platform support.
export class ProcessManager
{
  public:
    virtual ~ProcessManager() = default;

    /// Spawns a new process with the given configuration.
    ///
    /// @param config Process spawn configuration
    /// @return The PID of the spawned process on success, or an error
    [[nodiscard]] virtual std::expected<ProcessId, ShellError> spawn(SpawnConfig const& config) = 0;

    /// Waits for a process to terminate.
    ///
    /// @param pid Process ID to wait for
    /// @return Wait result on success, or an error
    [[nodiscard]] virtual std::expected<WaitResult, ShellError> wait(ProcessId pid) = 0;

    /// Changes the current working directory.
    ///
    /// @param path New working directory path
    /// @return Success or an error
    [[nodiscard]] virtual std::expected<void, ShellError> changeDirectory(
        std::filesystem::path const& path) = 0;

    /// Opens a file with the specified flags and mode.
    ///
    /// @param path Path to the file
    /// @param flags Open flags (O_RDONLY, O_WRONLY, etc.)
    /// @param mode File mode for creation (default 0644)
    /// @return File descriptor/handle on success, or an error
    [[nodiscard]] virtual std::expected<NativeHandle, ShellError> openFile(std::filesystem::path const& path,
                                                                           int flags,
                                                                           int mode = 0644) = 0;

    /// Creates a new session (becomes session leader).
    ///
    /// @return New session ID on success, or an error
    [[nodiscard]] virtual std::expected<ProcessId, ShellError> createSession() = 0;

    /// Sets the process group for a process.
    ///
    /// @param pid Process ID (0 for current process)
    /// @param pgid Process group ID (0 for new group with pid as leader)
    /// @return Success or an error
    [[nodiscard]] virtual std::expected<void, ShellError> setProcessGroup(ProcessId pid, ProcessId pgid) = 0;

    /// Duplicates a file descriptor/handle.
    ///
    /// @param src Source file descriptor/handle
    /// @param dst Destination file descriptor/handle
    /// @return Success or an error
    [[nodiscard]] virtual std::expected<void, ShellError> duplicateFd(NativeHandle src, NativeHandle dst) = 0;

    /// Closes a file descriptor/handle.
    ///
    /// @param handle Handle to close
    virtual void closeHandle(NativeHandle handle) noexcept = 0;

    /// Closes all file descriptors/handles above stderr.
    virtual void closeExtraHandles() noexcept = 0;
};

#if !defined(_WIN32)
/// POSIX implementation of ProcessManager.
export class PosixProcessManager final: public ProcessManager
{
  public:
    /// Returns the singleton instance of PosixProcessManager.
    [[nodiscard]] static PosixProcessManager& instance()
    {
        static PosixProcessManager pm;
        return pm;
    }

    [[nodiscard]] std::expected<ProcessId, ShellError> spawn(SpawnConfig const& config) override
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
                closeExtraHandles();

            execvp(config.program.c_str(), const_cast<char* const*>(argv.data()));
            _exit(EXIT_FAILURE);
        }

        return pid;
    }

    [[nodiscard]] std::expected<WaitResult, ShellError> wait(ProcessId pid) override
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

    [[nodiscard]] std::expected<void, ShellError> changeDirectory(std::filesystem::path const& path) override
    {
        if (chdir(path.c_str()) != 0)
            return std::unexpected(ShellError::FileNotFound);
        return {};
    }

    [[nodiscard]] std::expected<NativeHandle, ShellError> openFile(std::filesystem::path const& path,
                                                                   int flags,
                                                                   int mode) override
    {
        NativeHandle const fd = open(path.c_str(), flags, mode);
        if (fd == InvalidHandle)
            return std::unexpected(ShellError::IoError);
        return fd;
    }

    [[nodiscard]] std::expected<ProcessId, ShellError> createSession() override
    {
        ProcessId const sid = setsid();
        if (sid == InvalidProcessId)
            return std::unexpected(ShellError::ForkFailed);
        return sid;
    }

    [[nodiscard]] std::expected<void, ShellError> setProcessGroup(ProcessId pid, ProcessId pgid) override
    {
        if (setpgid(pid, pgid) == -1)
            return std::unexpected(ShellError::ExecutionFailed);
        return {};
    }

    [[nodiscard]] std::expected<void, ShellError> duplicateFd(NativeHandle src, NativeHandle dst) override
    {
        if (dup2(src, dst) == -1)
            return std::unexpected(ShellError::HandleDuplicationFailed);
        return {};
    }

    void closeHandle(NativeHandle handle) noexcept override
    {
        if (handle != InvalidHandle)
            ::close(handle);
    }

    void closeExtraHandles() noexcept override
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
};
#endif // !_WIN32

} // namespace endo
