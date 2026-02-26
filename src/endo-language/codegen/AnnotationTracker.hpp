// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <CoreVM/CoreVM.hpp>

#include <cstdint>
#include <optional>
#include <unordered_map>

namespace endo
{

/// Consolidates the 6 parallel annotation maps that track semantic type information
/// through IR values during code generation.
///
/// Each annotation map associates a CoreVM::Value* (produced by IR emission) with
/// a piece of type metadata that is not representable in the IR type system but is
/// needed for correct codegen (e.g., list element types, Option inner types).
class AnnotationTracker
{
  public:
    // --- Inner type (the T in Option<T> or Result<T,E>) ---

    void annotateInnerType(CoreVM::Value* val, CoreVM::LiteralType type);
    [[nodiscard]] std::optional<CoreVM::LiteralType> getInnerType(CoreVM::Value* val) const;

    // --- Object type ID (e.g., BuiltinTypeId::List, BuiltinTypeId::Option) ---

    void annotateObjectTypeId(CoreVM::Value* val, uint16_t typeId);
    [[nodiscard]] std::optional<uint16_t> getObjectTypeId(CoreVM::Value* val) const;

    // --- Inner object type ID (e.g., List type ID inside Some [1;2;3]) ---

    void annotateInnerObjectTypeId(CoreVM::Value* val, uint16_t typeId);
    [[nodiscard]] std::optional<uint16_t> getInnerObjectTypeId(CoreVM::Value* val) const;

    // --- List element type ID (e.g., ProcessInfo for ps output) ---

    void annotateListElementTypeId(CoreVM::Value* val, uint16_t typeId);
    [[nodiscard]] std::optional<uint16_t> getListElementTypeId(CoreVM::Value* val) const;

    // --- List element literal type (e.g., String for split output) ---

    void annotateListElementLiteralType(CoreVM::Value* val, CoreVM::LiteralType type);
    [[nodiscard]] std::optional<CoreVM::LiteralType> getListElementLiteralType(CoreVM::Value* val) const;

    // --- List element inner type (runtime type differing from IR type) ---

    void annotateListElementInnerType(CoreVM::Value* val, CoreVM::LiteralType type);
    [[nodiscard]] std::optional<CoreVM::LiteralType> getListElementInnerType(CoreVM::Value* val) const;

    // --- Bulk operations ---

    /// Copies all annotations from @p source to @p dest.
    void propagateAll(CoreVM::Value* source, CoreVM::Value* dest);

  private:
    std::unordered_map<CoreVM::Value*, CoreVM::LiteralType> _innerTypeAnnotations;
    std::unordered_map<CoreVM::Value*, uint16_t> _objectTypeIdAnnotations;
    std::unordered_map<CoreVM::Value*, uint16_t> _innerObjectTypeIdAnnotations;
    std::unordered_map<CoreVM::Value*, uint16_t> _listElementTypeAnnotations;
    std::unordered_map<CoreVM::Value*, CoreVM::LiteralType> _listElementLiteralTypes;
    std::unordered_map<CoreVM::Value*, CoreVM::LiteralType> _listElementInnerTypes;
};

} // namespace endo
