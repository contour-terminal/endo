// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <CoreVM/enums.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace CoreVM
{

struct TypedObject; // forward declaration
class Runner;       // forward declaration

/// Optional custom formatter for human-readable display of typed objects.
/// When set, valueToString() calls this instead of the generic Product/Sum formatter.
using TypeFormatFn = std::string (*)(TypedObject const& obj, Runner* runner);

/// The kind of composite type.
enum class TypeKind : uint8_t
{
    Sum,      ///< Discriminated unions (Option, Result, user-defined ADTs)
    Product,  ///< Tuples, records/structs
    Function, ///< Closures with captured environment
    Array,    ///< Homogeneous dynamic arrays
};

/// Information about a field in a product type or named union variant.
struct FieldInfo
{
    std::string name;                       ///< Field name (empty string for tuple positions)
    uint8_t offset {};                      ///< Slot offset within the object's data area
    LiteralType type = LiteralType::Number; ///< The VM type of this field's value
    std::string nestedTypeName; ///< For Object-typed fields, the nested type name (e.g., "Size", "DateTime")
    bool display = true;        ///< Whether this field is shown as a column in default table output.
                                ///< Hidden fields remain accessible via field access and completion;
                                ///< they typically drive presentation (e.g. an isSymlink flag rendered
                                ///< inline) rather than warranting their own column.
};

/// Information about a module-level function associated with a type.
struct ModuleFunctionInfo
{
    std::string name;      ///< Function name (e.g., "now", "fromEpoch")
    std::string signature; ///< Signature description (e.g., "DateTime.now -> DateTime (current UTC time)")
};

/// Information about a variant in a sum type.
struct VariantInfo
{
    std::string name;              ///< Variant name ("Some", "None", "Ok", "Error", etc.)
    uint8_t payloadSlots {};       ///< Number of Value slots for payload (0 for unit variants)
    std::vector<FieldInfo> fields; ///< Named fields (empty if positional or unit variant)
};

/// Precomputed slot tracing info for GC and recursive release.
///
/// Describes which slots of a TypedObject hold child object pointers that
/// must be traced during garbage collection or released during recursive deallocation.
struct SlotTraceInfo
{
    /// Product types: slot indices that always hold object pointers.
    std::vector<uint8_t> fixedObjectSlots;

    /// Sum types: per-variant (indexed by tag) fixed object slot indices.
    std::vector<std::vector<uint8_t>> variantFixedSlots;

    /// A slot whose contained type depends on a runtime type-tag slot.
    struct DynamicSlot
    {
        uint8_t slotIndex;   ///< The slot that might hold an object pointer.
        uint8_t typeTagSlot; ///< The slot containing the LiteralType tag.
        uint8_t tagPosition; ///< Position for packed type tags (tuples), 0 for simple.
    };

    /// Product types: dynamic slots.
    std::vector<DynamicSlot> dynamicSlots;

    /// Sum types: per-variant dynamic slots.
    std::vector<std::vector<DynamicSlot>> variantDynamicSlots;
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
    TypeKind kind {};      ///< What kind of composite type this is
    uint16_t id {};        ///< Unique type ID for fast comparison
    std::string name;      ///< Human-readable type name ("Option", "Result", etc.)
    uint16_t slotCount {}; ///< Total Value slots needed for payload data

    /// Variant information (only for Sum types).
    /// Index in this vector is the tag value.
    std::vector<VariantInfo> variants;

    /// Field information (only for Product types).
    std::vector<FieldInfo> fields;

    /// Module-level functions associated with this type (e.g., DateTime.now, Size.fromBytes).
    std::vector<ModuleFunctionInfo> moduleFunctions;

    /// Shell command that produces this type (e.g., "ls" for FileInfo).
    std::string producingCommand;

    /// Whether the language surfaces this Product type as a named record type.
    ///
    /// Set it on a type whose fields the type checker should resolve (`f.size`) and whose shape an
    /// anonymous record literal may be matched against. Leave it false for runtime-only shapes —
    /// tuples, List cells, and module-style types such as `Markdown` or `Json` whose single field
    /// is an implementation detail — since admitting those would let `{ content = "x" }` resolve to
    /// `Markdown`. Completion offers dot-access on a wider set, which is deliberate: knowing a
    /// value's type is enough to suggest its fields, without making the shape resolvable by name.
    bool languageRecord = false;

    /// For Function types: number of captured variables.
    uint16_t captureCount = 0;

    /// Optional dispose callback name for scoped resource management (`let use`).
    /// When set, the type is considered disposable and `let use` bindings will
    /// automatically call this callback at scope exit.
    std::optional<std::string> disposeCallbackName;

    /// Custom formatter for human-readable display. When nullptr, generic
    /// Product/Sum formatting based on TypeKind is used.
    TypeFormatFn formatFn = nullptr;

    /// Precomputed tracing info for GC mark/sweep and recursive release.
    SlotTraceInfo traceInfo;

    /// Whether instances of this type have mutable slots (e.g., ref cells).
    /// Used by the write barrier to decide whether to track mutation suspects.
    bool hasMutableSlots = false;

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

/// Packs two LiteralType values into a single uint64_t for Tuple2 type tag slots.
constexpr uint64_t packTypeTag(LiteralType t0, LiteralType t1) noexcept
{
    return static_cast<uint64_t>(t0) | (static_cast<uint64_t>(t1) << 8);
}

/// Packs three LiteralType values into a single uint64_t for Tuple3 type tag slots.
constexpr uint64_t packTypeTag(LiteralType t0, LiteralType t1, LiteralType t2) noexcept
{
    return static_cast<uint64_t>(t0) | (static_cast<uint64_t>(t1) << 8) | (static_cast<uint64_t>(t2) << 16);
}

/// Unpacks a LiteralType from a packed type tag at the given position (0, 1, or 2).
constexpr LiteralType unpackTypeTag(uint64_t packed, uint8_t position) noexcept
{
    return static_cast<LiteralType>((packed >> (static_cast<unsigned>(position) * 8)) & 0xFF);
}

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
    constexpr uint16_t JobInfo = 8;
    constexpr uint16_t DateTime = 9;
    constexpr uint16_t Size = 10;
    constexpr uint16_t FileMode = 11;
    constexpr uint16_t Markdown = 12;
    constexpr uint16_t TimeSpan = 13;
    constexpr uint16_t Lazy = 14;
    constexpr uint16_t Seq = 15;
    constexpr uint16_t FileHandle = 16;
    constexpr uint16_t Callable = 17;
    constexpr uint16_t Path = 18;
    constexpr uint16_t KeyBindingInfo = 19;
    constexpr uint16_t Json = 20;
    constexpr uint16_t Process = 21;
    constexpr uint16_t Ref = 22;
    constexpr uint16_t CompletionEntry = 23;
    constexpr uint16_t LastBuiltin =
        CompletionEntry; ///< Highest sequential builtin type ID; update when adding new builtins.
    constexpr uint16_t OutputDefBase = 100; ///< Base ID for output definition record types (100, 101, ...)
} // namespace BuiltinTypeId

} // namespace CoreVM
