// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <CoreVM/enums.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace CoreVM
{

/// The kind of composite type.
enum class TypeKind : uint8_t
{
    Sum,      ///< Discriminated unions (Option, Result, user-defined ADTs)
    Product,  ///< Tuples, records/structs
    Function, ///< Closures with captured environment
    Array,    ///< Homogeneous dynamic arrays
};

/// Information about a variant in a sum type.
struct VariantInfo
{
    std::string name;     ///< Variant name ("Some", "None", "Ok", "Error", etc.)
    uint8_t payloadSlots; ///< Number of Value slots for payload (0 for unit variants)
};

/// Information about a field in a product type.
struct FieldInfo
{
    std::string name;                       ///< Field name (empty string for tuple positions)
    uint8_t offset;                         ///< Slot offset within the object's data area
    LiteralType type = LiteralType::Number; ///< The VM type of this field's value
};

/// Describes a composite type's structure.
///
/// TypeDescriptors are registered once and shared by all instances of that type.
/// They provide the metadata needed for:
/// - Object allocation (how many slots to allocate)
/// - Pattern matching (variant tags, field access)
/// - Runtime type checking (type ID comparison)
/// - Debugging/tracing (type and variant names)
struct TypeDescriptor
{
    TypeKind kind;      ///< What kind of composite type this is
    uint16_t id;        ///< Unique type ID for fast comparison
    std::string name;   ///< Human-readable type name ("Option", "Result", etc.)
    uint16_t slotCount; ///< Total Value slots needed for payload data

    /// Variant information (only for Sum types).
    /// Index in this vector is the tag value.
    std::vector<VariantInfo> variants;

    /// Field information (only for Product types).
    std::vector<FieldInfo> fields;

    /// For Function types: number of captured variables.
    uint16_t captureCount = 0;

    /// Returns the variant info for a given tag, or nullptr if invalid.
    [[nodiscard]] const VariantInfo* getVariant(uint8_t tag) const
    {
        if (tag < variants.size())
            return &variants[tag];
        return nullptr;
    }

    /// Returns the field info by index, or nullptr if invalid.
    [[nodiscard]] const FieldInfo* getField(uint8_t index) const
    {
        if (index < fields.size())
            return &fields[index];
        return nullptr;
    }

    /// Returns the field info by name, or nullptr if not found.
    [[nodiscard]] const FieldInfo* getFieldByName(std::string_view fieldName) const
    {
        for (const auto& field: fields)
        {
            if (field.name == fieldName)
                return &field;
        }
        return nullptr;
    }

    /// Looks up a variant by name and returns its tag, or -1 if not found.
    [[nodiscard]] int getVariantTag(std::string_view variantName) const
    {
        for (size_t i = 0; i < variants.size(); ++i)
        {
            if (variants[i].name == variantName)
                return static_cast<int>(i);
        }
        return -1;
    }
};

/// Well-known type IDs for built-in types.
/// These are registered first and have fixed IDs.
namespace BuiltinTypeId
{
    constexpr uint16_t Option = 1;
    constexpr uint16_t Result = 2;
    constexpr uint16_t Tuple2 = 3;
    constexpr uint16_t Tuple3 = 4;
    constexpr uint16_t List = 5;
    constexpr uint16_t ProcessInfo = 6;
    constexpr uint16_t FileInfo = 7;
    constexpr uint16_t OutputDefBase = 100; ///< Base ID for output definition record types (100, 101, ...)
} // namespace BuiltinTypeId

} // namespace CoreVM
