// SPDX-License-Identifier: Apache-2.0
#include <endo-language/codegen/AnnotationTracker.hpp>

namespace endo
{

void AnnotationTracker::annotateInnerType(CoreVM::Value* val, CoreVM::LiteralType type)
{
    _innerTypeAnnotations[val] = type;
}

std::optional<CoreVM::LiteralType> AnnotationTracker::getInnerType(CoreVM::Value* val) const
{
    if (auto it = _innerTypeAnnotations.find(val); it != _innerTypeAnnotations.end())
        return it->second;
    return std::nullopt;
}

void AnnotationTracker::annotateObjectTypeId(CoreVM::Value* val, uint16_t typeId)
{
    _objectTypeIdAnnotations[val] = typeId;
}

std::optional<uint16_t> AnnotationTracker::getObjectTypeId(CoreVM::Value* val) const
{
    if (auto it = _objectTypeIdAnnotations.find(val); it != _objectTypeIdAnnotations.end())
        return it->second;
    return std::nullopt;
}

void AnnotationTracker::annotateInnerObjectTypeId(CoreVM::Value* val, uint16_t typeId)
{
    _innerObjectTypeIdAnnotations[val] = typeId;
}

std::optional<uint16_t> AnnotationTracker::getInnerObjectTypeId(CoreVM::Value* val) const
{
    if (auto it = _innerObjectTypeIdAnnotations.find(val); it != _innerObjectTypeIdAnnotations.end())
        return it->second;
    return std::nullopt;
}

void AnnotationTracker::annotateListElementTypeId(CoreVM::Value* val, uint16_t typeId)
{
    _listElementTypeAnnotations[val] = typeId;
}

std::optional<uint16_t> AnnotationTracker::getListElementTypeId(CoreVM::Value* val) const
{
    if (auto it = _listElementTypeAnnotations.find(val); it != _listElementTypeAnnotations.end())
        return it->second;
    return std::nullopt;
}

void AnnotationTracker::annotateListElementLiteralType(CoreVM::Value* val, CoreVM::LiteralType type)
{
    _listElementLiteralTypes[val] = type;
}

std::optional<CoreVM::LiteralType> AnnotationTracker::getListElementLiteralType(CoreVM::Value* val) const
{
    if (auto it = _listElementLiteralTypes.find(val); it != _listElementLiteralTypes.end())
        return it->second;
    return std::nullopt;
}

void AnnotationTracker::annotateListElementInnerType(CoreVM::Value* val, CoreVM::LiteralType type)
{
    _listElementInnerTypes[val] = type;
}

std::optional<CoreVM::LiteralType> AnnotationTracker::getListElementInnerType(CoreVM::Value* val) const
{
    if (auto it = _listElementInnerTypes.find(val); it != _listElementInnerTypes.end())
        return it->second;
    return std::nullopt;
}

void AnnotationTracker::propagateAll(CoreVM::Value* source, CoreVM::Value* dest)
{
    if (auto v = getInnerType(source))
        annotateInnerType(dest, *v);
    if (auto v = getObjectTypeId(source))
        annotateObjectTypeId(dest, *v);
    if (auto v = getInnerObjectTypeId(source))
        annotateInnerObjectTypeId(dest, *v);
    if (auto v = getListElementTypeId(source))
        annotateListElementTypeId(dest, *v);
    if (auto v = getListElementLiteralType(source))
        annotateListElementLiteralType(dest, *v);
    if (auto v = getListElementInnerType(source))
        annotateListElementInnerType(dest, *v);
}

} // namespace endo
