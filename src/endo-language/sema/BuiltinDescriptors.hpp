// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <CoreVM/CoreVM.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace endo
{

/// Descriptor for a builtin function call's metadata.
///
/// Captures the static type annotation information that should be applied
/// to the result of a builtin call. Separates this metadata from the IR emission
/// logic so that the semantic analyzer can validate calls without generating IR.
struct BuiltinCallDescriptor
{
    std::string_view name; ///< Builtin name (e.g., "head", "split")
    size_t minArity = 0;   ///< Minimum number of arguments
    size_t maxArity = 0;   ///< Maximum number of arguments (same as minArity for fixed-arity)

    /// Static return annotations (applied unconditionally to the result).
    std::optional<uint16_t> returnObjectTypeId;            ///< e.g., BuiltinTypeId::Option for head
    std::optional<CoreVM::LiteralType> returnInnerType;    ///< e.g., LiteralType::String for env
    std::optional<uint16_t> returnListElementTypeId;       ///< e.g., BuiltinTypeId::ProcessInfo for ps
    std::optional<CoreVM::LiteralType> returnListElemType; ///< e.g., LiteralType::String for split

    /// Whether this builtin propagates input list annotations to the output.
    bool propagatesListElementType = false;       ///< tail, filter, etc.
    bool propagatesListElementAsInnerObj = false; ///< head, nth, last (element becomes inner of Option)
};

/// Descriptor for a builtin property access (e.g., list.length, option.isSome).
struct BuiltinPropertyDescriptor
{
    uint16_t typeId;            ///< Object type ID this property applies to
    std::string_view fieldName; ///< Property name

    /// Static return annotations.
    std::optional<uint16_t> returnObjectTypeId;
    std::optional<CoreVM::LiteralType> returnInnerType;

    /// Whether this property propagates annotations from the source object.
    bool propagatesListElementType = false;
    bool propagatesListElementAsInnerObj = false;
};

/// Registry of builtin function and property descriptors.
///
/// Provides O(1) lookup by name for builtin calls, and by (typeId, fieldName)
/// for property access. The metadata enables semantic validation without IR generation.
class BuiltinDescriptorRegistry
{
  public:
    BuiltinDescriptorRegistry();

    /// Looks up a builtin call descriptor by name.
    [[nodiscard]] BuiltinCallDescriptor const* lookupCall(std::string_view name) const;

    /// Looks up a builtin property descriptor by (typeId, fieldName).
    [[nodiscard]] BuiltinPropertyDescriptor const* lookupProperty(uint16_t typeId,
                                                                  std::string_view field) const;

    /// Returns the full call descriptor map (for iteration/testing).
    [[nodiscard]] auto const& calls() const noexcept { return _calls; }

  private:
    void registerCall(BuiltinCallDescriptor desc);
    void registerProperty(BuiltinPropertyDescriptor desc);

    std::unordered_map<std::string_view, BuiltinCallDescriptor> _calls;

    /// Property descriptors keyed on "typeId:fieldName" composite key.
    std::unordered_map<std::string, BuiltinPropertyDescriptor> _properties;
};

} // namespace endo
