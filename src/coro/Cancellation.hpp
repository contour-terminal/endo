// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file
/// Cancellation primitives for @c endo::coro coroutines.
///
/// Cancellation uses the standard C++20 `<stop_token>` facility
/// (`std::stop_token` / `std::stop_source` / `std::stop_callback`). Following the
/// same policy as @c endo::Generator (which prefers `std::generator` and only
/// falls back when a supported standard library lacks it), this header prefers
/// the standard types and exposes them under the @c endo::coro namespace so call
/// sites are stable. Every standard library Endo targets (libstdc++, MSVC STL,
/// and libc++ from LLVM 18) ships `<stop_token>`; if a future target platform
/// does not, add a minimal fallback here mirroring `Generator.hpp` rather than
/// changing call sites.

#include <version>

#if defined(__cpp_lib_jthread) && __cpp_lib_jthread >= 201911L

    #include <stop_token>

namespace endo::coro
{

/// Observes a cancellation request. Cheap to copy; shares state with its source.
using StopToken = std::stop_token;

/// Requests cancellation of all @c StopToken instances obtained from it.
using StopSource = std::stop_source;

/// Invokes a callback when cancellation is requested on the associated token.
/// @tparam Callback The invocable to run on cancellation.
template <typename Callback>
using StopCallback = std::stop_callback<Callback>;

} // namespace endo::coro

#else
    #error "endo::coro cancellation requires C++20 <stop_token> (__cpp_lib_jthread). " \
        "Add a fallback in Cancellation.hpp mirroring platform/Generator.hpp if a target lacks it."
#endif

namespace endo::coro
{

/// Exception thrown into an awaiting coroutine frame when its operation is
/// cancelled, so the frame unwinds through ordinary RAII (restoring focus,
/// clearing prompts, etc.). Runtime awaitables throw this from @c await_resume
/// when their associated @c StopToken has @c stop_requested().
struct OperationCancelled
{
};

} // namespace endo::coro
