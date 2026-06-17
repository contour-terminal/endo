// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file Types.hpp
/// @brief Platform-agnostic type definitions for cross-platform compatibility.

#if defined(_WIN32)
    #include <fcntl.h>
    #include <io.h>
    #include <windows.h>
    #ifndef STDIN_FILENO
        #define STDIN_FILENO  0
        #define STDOUT_FILENO 1
        #define STDERR_FILENO 2
    #endif
    #ifndef SIGINT
        #define SIGINT  2
        #define SIGTERM 15
        #define SIGKILL 9
        #define SIGTSTP 20
        #define SIGCONT 18
        #define SIGCHLD 17
    #endif
#else
    #include <sys/types.h>

    #include <unistd.h>
#endif

#include <cstddef>
#include <cstdint>

namespace endo::platform
{

#ifdef _WIN32
/// Native file/pipe handle type for the current platform.
using NativeHandle = HANDLE;

/// Process identifier type for the current platform.
using ProcessId = DWORD;

/// Invalid handle sentinel value.
/// Cannot be constexpr on Windows because INVALID_HANDLE_VALUE involves a cast.
inline NativeHandle const InvalidHandle = INVALID_HANDLE_VALUE;

/// Invalid process ID sentinel value.
constexpr ProcessId InvalidProcessId = 0;

/// @brief Returns the standard input handle for the current platform.
inline auto standardInput() -> NativeHandle
{
    return GetStdHandle(STD_INPUT_HANDLE);
}

/// @brief Returns the standard output handle for the current platform.
inline auto standardOutput() -> NativeHandle
{
    return GetStdHandle(STD_OUTPUT_HANDLE);
}

/// @brief Returns the standard error handle for the current platform.
inline auto standardError() -> NativeHandle
{
    return GetStdHandle(STD_ERROR_HANDLE);
}
#else
/// Native file/pipe handle type for the current platform.
using NativeHandle = int;

/// Process identifier type for the current platform.
using ProcessId = pid_t;

/// Invalid handle sentinel value.
constexpr NativeHandle InvalidHandle = -1;

/// Invalid process ID sentinel value.
constexpr ProcessId InvalidProcessId = -1;

/// @brief Returns the standard input handle for the current platform.
constexpr auto standardInput() -> NativeHandle
{
    return STDIN_FILENO;
}

/// @brief Returns the standard output handle for the current platform.
constexpr auto standardOutput() -> NativeHandle
{
    return STDOUT_FILENO;
}

/// @brief Returns the standard error handle for the current platform.
constexpr auto standardError() -> NativeHandle
{
    return STDERR_FILENO;
}
#endif

/// @brief Converts a native handle to an integer suitable for the VM number type.
///
/// @c NativeHandle is an @c int on POSIX but a @c void* (@c HANDLE) on Windows.
/// A single @c static_cast cannot express both conversions (it rejects the
/// pointer-to-integer case), so this helper selects the correct cast at compile
/// time, keeping call sites portable and free of C-style casts.
///
/// @param handle The native handle to convert.
/// @return The handle reinterpreted as a signed 64-bit integer.
[[nodiscard]] inline auto nativeHandleToNumber(NativeHandle handle) noexcept -> std::int64_t
{
#ifdef _WIN32
    return static_cast<std::int64_t>(reinterpret_cast<std::intptr_t>(handle));
#else
    return static_cast<std::int64_t>(handle);
#endif
}

#ifdef _WIN32
/// Cross-platform close using Windows CloseHandle.
///
/// @param h The native handle to close.
inline void platformClose(NativeHandle h) noexcept
{
    if (h != InvalidHandle)
        CloseHandle(h);
}

/// Cross-platform write using Windows WriteFile.
///
/// @param h The native handle to write to.
/// @param data Pointer to data to write.
/// @param size Number of bytes to write.
/// @return Number of bytes written, or -1 on error.
inline auto platformWrite(NativeHandle h, void const* data, size_t size) -> intptr_t
{
    DWORD written = 0;
    if (!WriteFile(h, data, static_cast<DWORD>(size), &written, nullptr))
        return -1;
    return static_cast<intptr_t>(written);
}

/// Cross-platform read using Windows ReadFile.
///
/// Semantics match POSIX `read(2)`: returns the number of bytes read, 0 on
/// end-of-file (including the case where a pipe's write end has been closed
/// and the buffer is drained), or -1 on a genuine I/O error.
///
/// @param h The native handle to read from.
/// @param data Pointer to buffer to read into.
/// @param size Maximum number of bytes to read.
/// @return Bytes read, 0 on EOF / broken pipe, or -1 on error.
inline auto platformRead(NativeHandle h, void* data, size_t size) -> intptr_t
{
    DWORD bytesRead = 0;
    if (!ReadFile(h, data, static_cast<DWORD>(size), &bytesRead, nullptr))
    {
        // ERROR_BROKEN_PIPE / ERROR_HANDLE_EOF are stream terminators, not
        // errors — normalize them to POSIX-style EOF so the cross-platform
        // abstraction is uniform for callers that distinguish EOF from error.
        auto const err = GetLastError();
        if (err == ERROR_BROKEN_PIPE || err == ERROR_HANDLE_EOF)
            return 0;
        return -1;
    }
    return static_cast<intptr_t>(bytesRead);
}

/// Checks whether a native handle refers to a terminal (console) device.
///
/// @param h The native handle to check.
/// @return true if the handle is connected to a terminal.
inline bool isTerminal(NativeHandle h) noexcept
{
    DWORD mode = 0;
    return GetConsoleMode(h, &mode) != 0;
}
#else
/// Cross-platform close using POSIX close.
///
/// @param fd The file descriptor to close.
inline void platformClose(NativeHandle fd) noexcept
{
    if (fd != InvalidHandle)
        ::close(fd);
}

/// Cross-platform write using POSIX write.
///
/// @param fd The file descriptor to write to.
/// @param data Pointer to data to write.
/// @param size Number of bytes to write.
/// @return Number of bytes written, or -1 on error.
inline auto platformWrite(NativeHandle fd, void const* data, size_t size) -> intptr_t
{
    return static_cast<intptr_t>(::write(fd, data, size));
}

/// Cross-platform read using POSIX read.
///
/// @param fd The file descriptor to read from.
/// @param data Pointer to buffer to read into.
/// @param size Maximum number of bytes to read.
/// @return Bytes read, 0 on EOF / broken pipe, or -1 on error.
inline auto platformRead(NativeHandle fd, void* data, size_t size) -> intptr_t
{
    return static_cast<intptr_t>(::read(fd, data, size));
}

/// Checks whether a native handle refers to a terminal device.
///
/// @param fd The file descriptor to check.
/// @return true if the descriptor is connected to a terminal.
inline bool isTerminal(NativeHandle fd) noexcept
{
    return ::isatty(fd) != 0;
}
#endif

} // namespace endo::platform

// Backward-compatible aliases in the endo namespace
namespace endo
{
using endo::platform::InvalidHandle;
using endo::platform::InvalidProcessId;
using endo::platform::isTerminal;
using endo::platform::NativeHandle;
using endo::platform::platformClose;
using endo::platform::platformRead;
using endo::platform::platformWrite;
using endo::platform::ProcessId;
using endo::platform::standardError;
using endo::platform::standardInput;
using endo::platform::standardOutput;
} // namespace endo
