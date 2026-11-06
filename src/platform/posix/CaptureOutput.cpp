// SPDX-License-Identifier: Apache-2.0
#include <platform/CaptureOutput.hpp>
#include <platform/Pipe.hpp>

#if !defined(_WIN32)
    #include <array>
    #include <cerrno>
    #include <csignal>
    #include <optional>
    #include <ranges>
    #include <string>
    #include <string_view>
    #include <vector>

    #include <sys/stat.h>
    #include <sys/wait.h>

    #include <fcntl.h>
    #include <spawn.h>
    #include <unistd.h>

    #if defined(__APPLE__)
        #include <crt_externs.h>
    #endif

namespace endo::platform
{

namespace
{
    using core::platform::PlatformError;

    /// Owns a `posix_spawn_file_actions_t`.
    class FileActions
    {
      public:
        FileActions() noexcept { posix_spawn_file_actions_init(&_actions); }

        ~FileActions() { posix_spawn_file_actions_destroy(&_actions); }

        FileActions(FileActions const&) = delete;
        FileActions& operator=(FileActions const&) = delete;
        FileActions(FileActions&&) = delete;
        FileActions& operator=(FileActions&&) = delete;

        [[nodiscard]] posix_spawn_file_actions_t* get() noexcept { return &_actions; }

      private:
        posix_spawn_file_actions_t _actions {};
    };

    /// Owns a `posix_spawnattr_t`.
    class SpawnAttributes
    {
      public:
        SpawnAttributes() noexcept { posix_spawnattr_init(&_attributes); }

        ~SpawnAttributes() { posix_spawnattr_destroy(&_attributes); }

        SpawnAttributes(SpawnAttributes const&) = delete;
        SpawnAttributes& operator=(SpawnAttributes const&) = delete;
        SpawnAttributes(SpawnAttributes&&) = delete;
        SpawnAttributes& operator=(SpawnAttributes&&) = delete;

        [[nodiscard]] posix_spawnattr_t* get() noexcept { return &_attributes; }

      private:
        posix_spawnattr_t _attributes {};
    };

    /// @return The exit status @p status reports: the code a program exited with, or 128 plus the
    ///         number of the signal that ended it, as a shell reports it.
    [[nodiscard]] int exitCodeOf(int status) noexcept
    {
        if (WIFEXITED(status))
            return WEXITSTATUS(status);
        if (WIFSIGNALED(status))
            return 128 + WTERMSIG(status);
        return -1;
    }

    /// @return This process's environment, for the child to inherit.
    [[nodiscard]] char** processEnvironment() noexcept
    {
    #if defined(__APPLE__)
        // macOS's headers declare no `environ`; this is the documented way to reach it.
        return *_NSGetEnviron();
    #else
        return environ; // <unistd.h>
    #endif
    }

    /// Finds the executable a bare name stands for in the absolute directories PATH lists.
    ///
    /// Not `posix_spawnp`, which treats an empty PATH entry -- a leading, trailing or doubled `:` --
    /// as the current directory, as `execvp` does: for an interactive shell that is wherever the
    /// user is, a cloned repository included. A relative entry is skipped for the same reason.
    /// @param name The program's name.
    /// @param environment Where PATH is read from.
    /// @return Its path, or nothing when no such directory holds an executable file of that name.
    [[nodiscard]] std::optional<std::string> findOnPath(std::string_view name, core::Environment const& environment)
    {
        auto const path = environment.get("PATH");
        if (!path)
            return std::nullopt;
        for (auto const entryRange: std::views::split(*path, ':'))
        {
            auto const directory = std::string_view(entryRange.begin(), entryRange.end());
            if (!directory.starts_with('/'))
                continue;
            auto candidate = std::string(directory);
            candidate += '/';
            candidate += name;
            struct stat info {};
            if (::stat(candidate.c_str(), &info) == 0 && S_ISREG(info.st_mode)
                && ::access(candidate.c_str(), X_OK) == 0)
                return candidate;
        }
        return std::nullopt;
    }
} // namespace

auto captureOutput(std::string_view program,
                   std::span<std::string const> arguments,
                   core::Environment const& environment) -> std::expected<CapturedOutput, PlatformError>
{
    auto const executable =
        program.contains('/') ? std::optional { std::string(program) } : findOnPath(program, environment);
    if (!executable)
        return std::unexpected(PlatformError::ProgramNotFound);

    // Close-on-exec, so no child another thread starts meanwhile inherits either end; the dup2
    // below gives this child its own copy of the write end, without the flag.
    auto pipe = createPipe(O_CLOEXEC);
    if (!pipe)
        return std::unexpected(pipe.error());

    auto actions = FileActions {};
    posix_spawn_file_actions_addopen(actions.get(), STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_adddup2(actions.get(), (*pipe)->writer(), STDOUT_FILENO);
    posix_spawn_file_actions_addopen(actions.get(), STDERR_FILENO, "/dev/null", O_WRONLY, 0);

    // The caller may be a worker thread with signals blocked, and an interactive shell ignores
    // SIGINT, SIGTSTP and SIGTTOU: the child gets neither. And a process group of its own, so the
    // terminal's Ctrl+C or hangup, sent to the foreground group, does not reach it.
    auto unblocked = sigset_t {};
    sigemptyset(&unblocked);
    auto defaulted = sigset_t {};
    sigfillset(&defaulted);
    sigdelset(&defaulted, SIGKILL);
    sigdelset(&defaulted, SIGSTOP);
    auto attributes = SpawnAttributes {};
    posix_spawnattr_setsigmask(attributes.get(), &unblocked);
    posix_spawnattr_setsigdefault(attributes.get(), &defaulted);
    posix_spawnattr_setpgroup(attributes.get(), 0);
    posix_spawnattr_setflags(attributes.get(),
                             POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_SETPGROUP);

    auto programName = std::string(program);
    auto argumentCopies = std::vector<std::string>(arguments.begin(), arguments.end());
    auto argv = std::vector<char*> {};
    argv.reserve(argumentCopies.size() + 2);
    argv.push_back(programName.data());
    for (auto& argument: argumentCopies)
        argv.push_back(argument.data());
    argv.push_back(nullptr);

    auto pid = pid_t { -1 };
    auto const spawned = posix_spawn(
        &pid, executable->c_str(), actions.get(), attributes.get(), argv.data(), processEnvironment());
    // The child holds its own copy; this one would keep the read below from ever reaching the end.
    (*pipe)->closeWriter();
    if (spawned != 0)
        return std::unexpected(spawned == ENOENT ? PlatformError::ProgramNotFound
                                                 : PlatformError::ExecFailed);

    auto captured = CapturedOutput {};
    auto buffer = std::array<char, 4096> {};
    while (true)
    {
        auto const n = ::read((*pipe)->reader(), buffer.data(), buffer.size());
        if (n > 0)
            captured.standardOutput.append(buffer.data(), static_cast<std::size_t>(n));
        else if (n == 0 || errno != EINTR)
            break;
    }

    auto status = 0;
    while (::waitpid(pid, &status, 0) < 0)
    {
        if (errno == EINTR)
            continue;
        // Reaped already, by a `waitpid(-1)` elsewhere in this process -- an interactive shell's
        // SIGCHLD handler. The output is complete; only the status is gone.
        if (errno == ECHILD)
            return captured;
        return std::unexpected(PlatformError::WaitFailed);
    }
    captured.exitCode = exitCodeOf(status);
    return captured;
}

} // namespace endo::platform

#endif // !_WIN32
