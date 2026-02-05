// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file Platform.h
/// @brief Platform-agnostic type definitions for cross-platform compatibility.

#if defined(_WIN32)
    #include <windows.h>
#else
    #include <sys/types.h>
#endif

namespace endo
{

#if defined(_WIN32)
/// Native file/pipe handle type for the current platform.
using NativeHandle = HANDLE;

/// Process identifier type for the current platform.
using ProcessId = DWORD;

/// Invalid handle sentinel value.
constexpr NativeHandle InvalidHandle = INVALID_HANDLE_VALUE;

/// Invalid process ID sentinel value.
constexpr ProcessId InvalidProcessId = 0;
#else
/// Native file/pipe handle type for the current platform.
using NativeHandle = int;

/// Process identifier type for the current platform.
using ProcessId = pid_t;

/// Invalid handle sentinel value.
constexpr NativeHandle InvalidHandle = -1;

/// Invalid process ID sentinel value.
constexpr ProcessId InvalidProcessId = -1;
#endif

} // namespace endo
