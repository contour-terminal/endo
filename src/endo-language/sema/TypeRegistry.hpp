// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <CoreVM/CoreVM.hpp>

#include <string>
#include <unordered_map>
#include <vector>

namespace endo
{

/// Metadata for a registered record (product) type definition.
struct RecordTypeInfo
{
    uint16_t typeId;                                                 ///< Assigned type ID
    std::string name;                                                ///< Type name
    std::vector<CoreVM::FieldInfo> fields;                           ///< Field definitions (name + offset)
    std::unordered_map<std::string, CoreVM::LiteralType> fieldTypes; ///< Field name -> VM literal type
    std::unordered_map<std::string, uint16_t>
        fieldObjectTypeIds; ///< Object-typed field -> nested record type ID
};

/// Metadata for a registered discriminated union (sum) type definition.
struct UnionTypeInfo
{
    uint16_t typeId;                           ///< Assigned type ID
    std::string name;                          ///< Type name (e.g., "Shape")
    std::vector<CoreVM::VariantInfo> variants; ///< Variant definitions

    /// Maps field name to (variant_tag, slot_offset) for field access on union values.
    std::unordered_map<std::string, std::pair<int, uint8_t>> fieldLookup;
};

/// Information about a single constructor of a discriminated union.
struct ConstructorInfo
{
    std::string typeName;                ///< Parent union type name
    uint16_t typeId;                     ///< Assigned type ID of the parent union
    int tag;                             ///< Tag value for this constructor variant
    uint8_t payloadSlots;                ///< Number of payload slots (0 for unit constructors)
    std::vector<std::string> fieldNames; ///< Named fields (parallel to payload slots, empty if unnamed)
};

/// Registry for record types, union types, and constructors.
///
/// Manages the metadata for user-defined and builtin structured types.
/// Populated during setup (builtin types) and during AST visits
/// (RecordTypeDefStmt, UnionTypeDefStmt).
class TypeDefinitionRegistry
{
  public:
    /// Registers all well-known builtin record types (ProcessInfo, DateTime, etc.).
    void registerBuiltins();

    /// Registers a record type with the given name and metadata.
    void registerRecord(std::string name, RecordTypeInfo info);

    /// Registers a union type with the given name and metadata.
    void registerUnion(std::string name, UnionTypeInfo info);

    /// Registers a constructor with the given name and metadata.
    void registerConstructor(std::string name, ConstructorInfo info);

    /// Looks up a record type by name, returning nullptr if not found.
    [[nodiscard]] RecordTypeInfo const* lookupRecord(std::string const& name) const;

    /// Resolves a record type from a set of field names (for anonymous record literals).
    [[nodiscard]] RecordTypeInfo const* resolveRecordByFields(
        std::vector<std::string> const& fieldNames) const;

    /// Looks up a union type by name, returning nullptr if not found.
    [[nodiscard]] UnionTypeInfo const* lookupUnion(std::string const& name) const;

    /// Looks up a constructor by name, returning nullptr if not found.
    [[nodiscard]] ConstructorInfo const* lookupConstructor(std::string const& name) const;

    /// Returns the full record type map (for iteration in PatternIRGenerator, etc.).
    [[nodiscard]] auto const& records() const noexcept { return _recordTypes; }

    /// Returns the full union type map (for iteration).
    [[nodiscard]] auto const& unions() const noexcept { return _unionTypes; }

    /// Returns the full constructor map (for iteration).
    [[nodiscard]] auto const& constructors() const noexcept { return _constructorRegistry; }

  private:
    std::unordered_map<std::string, RecordTypeInfo> _recordTypes;
    std::unordered_map<std::string, UnionTypeInfo> _unionTypes;
    std::unordered_map<std::string, ConstructorInfo> _constructorRegistry;
};

} // namespace endo
