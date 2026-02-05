// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <expected>
#include <string_view>

namespace endo
{

/// Shell error codes for operations that can fail
enum class ShellError
{
    // Process errors
    ForkFailed,
    ExecFailed,
    WaitFailed,
    ProgramNotFound,

    // File errors
    FileNotFound,
    PermissionDenied,
    IoError,

    // Parse/execution errors
    ParseFailed,
    ExecutionFailed,

    // Environment errors
    VariableNotFound,
};

/// Converts a ShellError to a human-readable string.
///
/// @param error The error code to convert
/// @return A string view describing the error
[[nodiscard]] constexpr std::string_view toString(ShellError error) noexcept
{
    switch (error)
    {
        case ShellError::ForkFailed: return "fork failed";
        case ShellError::ExecFailed: return "exec failed";
        case ShellError::WaitFailed: return "wait failed";
        case ShellError::ProgramNotFound: return "program not found";
        case ShellError::FileNotFound: return "file not found";
        case ShellError::PermissionDenied: return "permission denied";
        case ShellError::IoError: return "I/O error";
        case ShellError::ParseFailed: return "parse failed";
        case ShellError::ExecutionFailed: return "execution failed";
        case ShellError::VariableNotFound: return "variable not found";
    }
    return "unknown error";
}

/// Result of waiting for a process to complete.
struct WaitResult
{
    int exitCode = 0;      ///< Exit code of the process
    bool signaled = false; ///< Whether the process was terminated by a signal
    int signal = 0;        ///< Signal number if signaled is true
};

} // namespace endo
