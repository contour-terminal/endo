// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "TypeDescriptor.hpp"

namespace CoreVM
{

/// Registry for composite types.
///
/// Types are registered at program load time and remain immutable during execution.
/// The registry owns all TypeDescriptor instances and provides lookup by ID or name.
///
/// Built-in types (Option, Result) are registered automatically via registerBuiltins().
class TypeRegistry
{
  public:
    TypeRegistry();
    ~TypeRegistry() = default;

    TypeRegistry(const TypeRegistry&) = delete;
    TypeRegistry& operator=(const TypeRegistry&) = delete;
    TypeRegistry(TypeRegistry&&) = default;
    TypeRegistry& operator=(TypeRegistry&&) = default;

    /// Registers built-in types (Option, Result).
    /// Called automatically by constructor.
    void registerBuiltins();

    /// Registers a sum type (discriminated union).
    /// @param name The type name (e.g., "Color", "Tree")
    /// @param variants Vector of variant definitions
    /// @return Pointer to the registered type descriptor
    TypeDescriptor* registerSumType(std::string name, std::vector<VariantInfo> variants);

    /// Registers a product type (tuple or record).
    /// @param name The type name (e.g., "Point", "Person")
    /// @param fields Vector of field definitions
    /// @return Pointer to the registered type descriptor
    TypeDescriptor* registerProductType(std::string name, std::vector<FieldInfo> fields);

    /// Registers a pre-built product type descriptor with a fixed ID.
    /// Used for custom record types where the ID was pre-assigned at IR generation time.
    /// @param type The complete type descriptor to register (takes ownership)
    /// @return Pointer to the registered type descriptor
    TypeDescriptor* registerProductType(std::unique_ptr<TypeDescriptor> type);

    /// Registers a pre-built sum type descriptor with a fixed ID.
    /// Used for custom discriminated unions where the ID was pre-assigned at IR generation time.
    /// @param type The complete type descriptor to register (takes ownership)
    /// @return Pointer to the registered type descriptor
    TypeDescriptor* registerSumType(std::unique_ptr<TypeDescriptor> type);

    /// Registers a function/closure type.
    /// @param name The type name (usually auto-generated)
    /// @param captureCount Number of captured variables
    /// @return Pointer to the registered type descriptor
    TypeDescriptor* registerFunctionType(std::string name, uint16_t captureCount);

    /// Looks up a type by its unique ID.
    /// @return Pointer to the type descriptor, or nullptr if not found
    [[nodiscard]] const TypeDescriptor* get(uint16_t id) const;

    /// Looks up a type by its unique ID (mutable access).
    /// @return Pointer to the type descriptor, or nullptr if not found
    [[nodiscard]] TypeDescriptor* getMutable(uint16_t id);

    /// Looks up a type by name.
    /// @return Pointer to the type descriptor, or nullptr if not found
    [[nodiscard]] const TypeDescriptor* getByName(std::string_view name) const;

    /// Returns the total number of registered types.
    [[nodiscard]] size_t size() const { return _types.size(); }

    /// Returns a read-only view of all registered type descriptors.
    [[nodiscard]] std::span<std::unique_ptr<TypeDescriptor> const> allTypes() const { return _types; }

    /// Returns the Option type descriptor.
    [[nodiscard]] const TypeDescriptor* optionType() const { return get(BuiltinTypeId::Option); }

    /// Returns the Result type descriptor.
    [[nodiscard]] const TypeDescriptor* resultType() const { return get(BuiltinTypeId::Result); }

  private:
    uint16_t _nextId = 1; // Start at 1, 0 is reserved for "no type"
    std::vector<std::unique_ptr<TypeDescriptor>> _types;
    std::unordered_map<std::string, uint16_t> _nameToId;

    TypeDescriptor* addType(std::unique_ptr<TypeDescriptor> type);
};

/// @brief Returns a shared registry holding only the builtin types.
///
/// Builtin descriptors are immutable once registered, so consumers that merely need to read a
/// builtin's layout — the frontend's TypeDefinitionRegistry, hover, tests — can share one
/// instance instead of each constructing its own. A registry that will also hold program types
/// (a Runner's) must still be constructed per program.
[[nodiscard]] TypeRegistry const& builtinTypes();

} // namespace CoreVM
