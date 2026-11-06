// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <core/Environment.hpp>
#include <core/platform/PlatformError.hpp>

#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace endo::platform
{

/// What a program run by @ref captureOutput wrote to its standard output, and how it ended.
struct CapturedOutput
{
    std::string standardOutput; ///< Everything the program wrote to its standard output.

    /// Its exit status, 128 plus the signal number if a signal ended it -- or nothing on POSIX when
    /// something else in this process reaped it first: an interactive shell's SIGCHLD handler reaps
    /// every child with `waitpid(-1)`, and may win that race. The output is complete either way.
    std::optional<int> exitCode;
};

/// Runs a program to completion and captures its standard output.
///
/// For the shell's own background queries -- the prompt's `git status` -- which must behave the same
/// whatever the user's directory is called and whatever state the terminal is in:
///
/// - **No shell parses anything.** @p arguments reach the program exactly as given, so a space, a
///   quote, `&`, `;` or `$(...)` in a directory name is data, not syntax.
/// - **A bare @p program is looked up in the absolute directories PATH lists, and nowhere else.** An
///   empty or relative PATH entry names the current directory, which for an interactive shell is
///   wherever the user happens to be -- a freshly cloned repository included -- so it is skipped. On
///   Windows only `<name>.exe` is looked for: a `.cmd` or `.bat` runs only through `cmd.exe`, whose
///   parsing of its command line is what this avoids.
/// - **It owns no terminal.** Its stdin and stderr are the null device. On Windows it gets a console
///   of its own (`CREATE_NO_WINDOW`) instead of attaching to the caller's, so it starts even after the
///   caller's console host has gone; a child that inherits a dead console fails to start with
///   `0xC0000142` and raises an error dialog. On POSIX it runs in a process group of its own, so the
///   terminal's job-control signals (Ctrl+C, a hangup) are not delivered to it.
///
/// Shares no state, so a background thread may call it. It blocks until the program exits.
///
/// @param program A bare program name, looked up as above, or a path to the program.
/// @param arguments The arguments that follow the program name.
/// @param environment Where PATH is read from to find a bare @p program -- `core::LiveEnvironment`
///        for the process's own. The program itself inherits this process's environment.
/// @return What the program wrote and its exit status; @c PlatformError::ProgramNotFound when a bare
///         name is not found; @c PlatformError::PipeCreationFailed, @c PlatformError::ExecFailed or
///         @c PlatformError::WaitFailed when the process could not be run or waited for.
[[nodiscard]] auto captureOutput(std::string_view program,
                                 std::span<std::string const> arguments,
                                 core::Environment const& environment)
    -> std::expected<CapturedOutput, core::platform::PlatformError>;

} // namespace endo::platform
