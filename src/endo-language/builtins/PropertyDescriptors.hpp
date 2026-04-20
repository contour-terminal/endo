// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file PropertyDescriptors.hpp
/// @brief Single source of truth for shell/agent property metadata.
///
/// Each property is declared once with name, type, description, detail,
/// read-only flag, and optional enumerated values. All consumers
/// (VM registration, completion, diagnostics, argument completion)
/// derive their data from these descriptors.

#include <CoreVM/CoreVM.hpp>

#include <span>
#include <string_view>

namespace endo
{

/// @brief Enumerated argument value with description for property completion.
struct EnumValueEntry
{
    std::string_view value;       ///< The value to insert (e.g., "powerline")
    std::string_view description; ///< Short description (e.g., "Powerline-style segments")
};

/// @brief Describes a shell or agent configuration property.
struct PropertyDescriptor
{
    std::string_view name;        ///< Property name (e.g., "shell_prompt_preset")
    CoreVM::LiteralType type;     ///< Property getter/primary setter value type
    std::string_view description; ///< Short description for registration and completion display
    std::string_view detail;      ///< Detailed markdown documentation for completion detail panel
    bool readOnly = false;        ///< True for read-only properties (no setter)
    std::span<EnumValueEntry const> enumValues; ///< Enumerated values for argument completion

    /// @brief Extra setter-argument types accepted by the property, in addition to `type`.
    ///
    /// When non-empty, a Function-typed setter overload (or similar) is registered
    /// alongside the primary one. The semantic analyzer consults this list to accept
    /// RHS values whose literal type is either `type` or one of these. Empty means
    /// the property accepts exactly `type`.
    std::span<CoreVM::LiteralType const> extraSetterTypes;
};

/// Returns prompt/shell property descriptors.
[[nodiscard]] std::span<PropertyDescriptor const> promptPropertyDescriptors();

/// Returns agent configuration property descriptors.
[[nodiscard]] std::span<PropertyDescriptor const> agentPropertyDescriptors();

/// Returns all property descriptors (prompt + agent combined).
[[nodiscard]] std::span<PropertyDescriptor const> allPropertyDescriptors();

} // namespace endo
