// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file StdlibDescriptors.hpp
/// @brief Single source of truth for stdlib function metadata.
///
/// Unifies the user-facing completion data (name, description, detail) with the
/// shared implementation callbacks (vmName, arity, sharedImpl). All consumers
/// (completion, resolveSharedImpl, diagnostics) derive their data from this table.

#include <CoreVM/CoreVM.hpp>

#include <optional>
#include <span>
#include <string_view>

namespace endo
{

/// @brief Callback function pointer for stdlib implementations.
using StdlibImplFn = void (*)(CoreVM::Params&);

/// @brief Describes a standard library function for completion and callback resolution.
struct StdlibDescriptor
{
    std::string_view userFacingName; ///< User-facing name (e.g., "head"); empty if internal-only
    std::string_view vmName;         ///< VM registration name (e.g., "list_head")
    size_t arity;                    ///< Parameter count for resolveSharedImpl lookup
    StdlibImplFn sharedImpl;         ///< Stateless callback; nullptr for IR-generated functions
    std::string_view description;    ///< Signature description for completion display
    std::string_view detail;         ///< Markdown documentation for completion detail panel
};

/// Returns all stdlib descriptors (user-facing + internal).
[[nodiscard]] std::span<StdlibDescriptor const> stdlibDescriptors();

/// Looks up a shared implementation by VM name and arity.
/// Replaces the hand-coded if-chain in BuiltinImpls.cpp.
[[nodiscard]] std::optional<CoreVM::NativeCallback::Functor> resolveStdlibImpl(std::string_view vmName,
                                                                               size_t arity);

} // namespace endo
