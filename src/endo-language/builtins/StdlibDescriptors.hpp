// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file StdlibDescriptors.hpp
/// @brief Single source of truth for stdlib function metadata.
///
/// Unifies the user-facing completion data (name, description, detail) with the
/// shared implementation callbacks (vmName, params, returnType, sharedImpl). All consumers
/// (completion, resolveSharedImpl, diagnostics, VM registration) derive their data from this table.

#include <CoreVM/CoreVM.hpp>

#include <optional>
#include <span>
#include <string_view>

namespace endo
{

/// @brief Callback function pointer for stdlib implementations.
using StdlibImplFn = void (*)(CoreVM::Params&);

/// @brief Describes a single parameter for a builtin function.
struct ParamDescriptor
{
    std::string_view name;    ///< Parameter name (e.g., "text", "list")
    CoreVM::LiteralType type; ///< VM type (Number, String, Boolean, etc.)
};

/// @brief Describes a standard library function for completion, VM registration, and callback resolution.
struct StdlibDescriptor
{
    std::string_view userFacingName;         ///< User-facing name (e.g., "head"); empty if internal-only
    std::string_view vmName;                 ///< VM registration name (e.g., "list_head")
    CoreVM::LiteralType returnType;          ///< Return type for VM registration
    std::span<ParamDescriptor const> params; ///< Parameter descriptors (replaces arity)
    StdlibImplFn sharedImpl;      ///< Stateless callback; nullptr for IR-generated or Shell-only functions
    std::string_view description; ///< Signature description for completion display
    std::string_view detail;      ///< Markdown documentation for completion detail panel
};

/// Returns all stdlib descriptors (user-facing + internal).
[[nodiscard]] std::span<StdlibDescriptor const> stdlibDescriptors();

/// Looks up a shared implementation by VM name and arity.
[[nodiscard]] std::optional<CoreVM::NativeCallback::Functor> resolveStdlibImpl(std::string_view vmName,
                                                                               size_t arity);

} // namespace endo
