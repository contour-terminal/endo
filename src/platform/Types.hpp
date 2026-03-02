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
/// @param h The native handle to read from.
/// @param data Pointer to buffer to read into.
/// @param size Maximum number of bytes to read.
/// @return Number of bytes read, or -1 on error.
inline auto platformRead(NativeHandle h, void* data, size_t size) -> intptr_t
{
    DWORD bytesRead = 0;
    if (!ReadFile(h, data, static_cast<DWORD>(size), &bytesRead, nullptr))
        return -1;
    return static_cast<intptr_t>(bytesRead);
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
/// @return Number of bytes read, or -1 on error.
inline auto platformRead(NativeHandle fd, void* data, size_t size) -> intptr_t
{
    return static_cast<intptr_t>(::read(fd, data, size));
}
#endif

} // namespace endo::platform

// Backward-compatible aliases in the endo namespace
namespace endo
{
using endo::platform::InvalidHandle;
using endo::platform::InvalidProcessId;
using endo::platform::NativeHandle;
using endo::platform::platformClose;
using endo::platform::platformRead;
using endo::platform::platformWrite;
using endo::platform::ProcessId;
using endo::platform::standardError;
using endo::platform::standardInput;
using endo::platform::standardOutput;
} // namespace endo
