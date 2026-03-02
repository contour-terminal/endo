// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string_view>

#include <platform/PlatformError.hpp>
#include <platform/WaitResult.hpp>

namespace endo
{

/// Shell error codes for operations that can fail.
///
/// Platform-level errors (fork, pipe, file I/O) are in endo::platform::PlatformError.
/// This enum retains shell-specific errors and platform-error aliases for backward compatibility.
enum class ShellError
{
    // Process errors (aliases for backward compat — prefer PlatformError for new code)
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

    // Platform abstraction errors
    PipeCreationFailed,
    HandleDuplicationFailed,
    NotImplemented,
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
        case ShellError::PipeCreationFailed: return "pipe creation failed";
        case ShellError::HandleDuplicationFailed: return "handle duplication failed";
        case ShellError::NotImplemented: return "not implemented";
    }
    return "unknown error";
}

/// Converts a PlatformError to a ShellError for backward compatibility.
///
/// @param error The platform error to convert
/// @return The corresponding ShellError
[[nodiscard]] constexpr ShellError toShellError(platform::PlatformError error) noexcept
{
    switch (error)
    {
        case platform::PlatformError::ForkFailed: return ShellError::ForkFailed;
        case platform::PlatformError::ExecFailed: return ShellError::ExecFailed;
        case platform::PlatformError::WaitFailed: return ShellError::WaitFailed;
        case platform::PlatformError::ProgramNotFound: return ShellError::ProgramNotFound;
        case platform::PlatformError::PipeCreationFailed: return ShellError::PipeCreationFailed;
        case platform::PlatformError::HandleDuplicationFailed: return ShellError::HandleDuplicationFailed;
        case platform::PlatformError::FileNotFound: return ShellError::FileNotFound;
        case platform::PlatformError::PermissionDenied: return ShellError::PermissionDenied;
        case platform::PlatformError::IoError: return ShellError::IoError;
        case platform::PlatformError::SignalFailed: return ShellError::ExecutionFailed;
        case platform::PlatformError::SessionCreationFailed: return ShellError::ForkFailed;
        case platform::PlatformError::ProcessGroupFailed: return ShellError::ExecutionFailed;
        case platform::PlatformError::TerminalControlFailed: return ShellError::ExecutionFailed;
        case platform::PlatformError::NotImplemented: return ShellError::NotImplemented;
    }
    return ShellError::ExecutionFailed;
}

} // namespace endo
