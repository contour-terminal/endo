// SPDX-License-Identifier: Apache-2.0
#include "TypeRegistry.hpp"

#include <algorithm>
#include <cassert>

namespace CoreVM
{

TypeRegistry::TypeRegistry()
{
    registerBuiltins();
}

void TypeRegistry::registerBuiltins()
{
    // Option<T>: None (tag=0, 0 payload slots) | Some (tag=1, 1 payload slot)
    auto optionType = std::make_unique<TypeDescriptor>();
    optionType->kind = TypeKind::Sum;
    optionType->id = BuiltinTypeId::Option;
    optionType->name = "Option";
    optionType->slotCount = 1; // Maximum payload size (Some has 1 slot)
    optionType->variants = {
        { "None", 0 }, // tag 0: no payload
        { "Some", 1 }, // tag 1: 1 slot payload
    };
    addType(std::move(optionType));

    // Result<T,E>: Error (tag=0, 1 payload slot) | Ok (tag=1, 1 payload slot)
    auto resultType = std::make_unique<TypeDescriptor>();
    resultType->kind = TypeKind::Sum;
    resultType->id = BuiltinTypeId::Result;
    resultType->name = "Result";
    resultType->slotCount = 1; // Both variants have 1 slot payload
    resultType->variants = {
        { "Error", 1 }, // tag 0: error payload
        { "Ok", 1 },    // tag 1: success payload
    };
    addType(std::move(resultType));

    // Tuple2: 2-element product type
    auto tuple2Type = std::make_unique<TypeDescriptor>();
    tuple2Type->kind = TypeKind::Product;
    tuple2Type->id = BuiltinTypeId::Tuple2;
    tuple2Type->name = "Tuple2";
    tuple2Type->slotCount = 2;
    tuple2Type->fields = {
        { "", 0 }, // slot 0 (unnamed positional)
        { "", 1 }, // slot 1
    };
    addType(std::move(tuple2Type));

    // Tuple3: 3-element product type
    auto tuple3Type = std::make_unique<TypeDescriptor>();
    tuple3Type->kind = TypeKind::Product;
    tuple3Type->id = BuiltinTypeId::Tuple3;
    tuple3Type->name = "Tuple3";
    tuple3Type->slotCount = 3;
    tuple3Type->fields = {
        { "", 0 }, // slot 0
        { "", 1 }, // slot 1
        { "", 2 }, // slot 2
    };
    addType(std::move(tuple3Type));

    // List: Nil (tag=0, 0 payload slots) | Cons (tag=1, 2 slots: head + tail)
    auto listType = std::make_unique<TypeDescriptor>();
    listType->kind = TypeKind::Sum;
    listType->id = BuiltinTypeId::List;
    listType->name = "List";
    listType->slotCount = 2; // Maximum payload size (Cons has 2 slots: head + tail)
    listType->variants = {
        { "Nil", 0 },  // tag 0: empty list
        { "Cons", 2 }, // tag 1: head (slot 0) + tail (slot 1)
    };
    addType(std::move(listType));

    // ProcessInfo: Product type with 6 fields for process information
    auto processInfoType = std::make_unique<TypeDescriptor>();
    processInfoType->kind = TypeKind::Product;
    processInfoType->id = BuiltinTypeId::ProcessInfo;
    processInfoType->name = "ProcessInfo";
    processInfoType->slotCount = 6;
    processInfoType->fields = {
        { "pid", 0, LiteralType::Number },
        { "ppid", 1, LiteralType::Number },
        { "user", 2, LiteralType::String },
        { "cpu", 3, LiteralType::Number },  // stored as bit_cast<uint64_t>(double)
        { "mem", 4, LiteralType::Number },
        { "command", 5, LiteralType::String },
    };
    addType(std::move(processInfoType));

    // Update _nextId to be after the builtin type IDs
    _nextId = std::max(_nextId, static_cast<uint16_t>(BuiltinTypeId::ProcessInfo + 1));
}

TypeDescriptor* TypeRegistry::registerSumType(std::string name, std::vector<VariantInfo> variants)
{
    auto type = std::make_unique<TypeDescriptor>();
    type->kind = TypeKind::Sum;
    type->id = _nextId++;
    type->name = std::move(name);
    type->variants = std::move(variants);

    // Calculate slot count as maximum of all variant payload sizes
    type->slotCount = 0;
    for (const auto& variant: type->variants)
    {
        type->slotCount = std::max(type->slotCount, static_cast<uint16_t>(variant.payloadSlots));
    }

    return addType(std::move(type));
}

TypeDescriptor* TypeRegistry::registerProductType(std::string name, std::vector<FieldInfo> fields)
{
    auto type = std::make_unique<TypeDescriptor>();
    type->kind = TypeKind::Product;
    type->id = _nextId++;
    type->name = std::move(name);
    type->fields = std::move(fields);

    // Slot count is the number of fields (each field is one slot)
    type->slotCount = static_cast<uint16_t>(type->fields.size());

    // Assign offsets if not already set
    for (uint8_t i = 0; i < type->fields.size(); ++i)
    {
        type->fields[i].offset = i;
    }

    return addType(std::move(type));
}

TypeDescriptor* TypeRegistry::registerProductType(std::unique_ptr<TypeDescriptor> type)
{
    // Ensure the type has the correct kind and consistent offsets
    assert(type->kind == TypeKind::Product);
    for (uint8_t i = 0; i < type->fields.size(); ++i)
        type->fields[i].offset = i;
    type->slotCount = static_cast<uint16_t>(type->fields.size());

    // Update _nextId to stay ahead of the assigned ID
    if (type->id >= _nextId)
        _nextId = type->id + 1;

    return addType(std::move(type));
}

TypeDescriptor* TypeRegistry::registerSumType(std::unique_ptr<TypeDescriptor> type)
{
    assert(type->kind == TypeKind::Sum);

    // Calculate slotCount as max of variant payload sizes
    uint16_t maxSlots = 0;
    for (auto const& v: type->variants)
        maxSlots = std::max(maxSlots, static_cast<uint16_t>(v.payloadSlots));
    type->slotCount = maxSlots;

    // Update _nextId to stay ahead of the assigned ID
    if (type->id >= _nextId)
        _nextId = type->id + 1;

    return addType(std::move(type));
}

TypeDescriptor* TypeRegistry::registerFunctionType(std::string name, uint16_t captureCount)
{
    auto type = std::make_unique<TypeDescriptor>();
    type->kind = TypeKind::Function;
    type->id = _nextId++;
    type->name = std::move(name);
    type->captureCount = captureCount;
    type->slotCount = captureCount; // Each capture is one slot

    return addType(std::move(type));
}

const TypeDescriptor* TypeRegistry::get(uint16_t id) const
{
    // Types are stored with their ID as index (offset by 1 since ID 0 is reserved)
    // But we may have sparse IDs, so search linearly for now
    // TODO: Optimize with direct indexing if IDs are always sequential
    for (const auto& type: _types)
    {
        if (type->id == id)
            return type.get();
    }
    return nullptr;
}

const TypeDescriptor* TypeRegistry::getByName(std::string_view name) const
{
    auto it = _nameToId.find(std::string(name));
    if (it != _nameToId.end())
        return get(it->second);
    return nullptr;
}

TypeDescriptor* TypeRegistry::addType(std::unique_ptr<TypeDescriptor> type)
{
    assert(type != nullptr);
    assert(!type->name.empty());

    // Ensure ID is unique
    assert(get(type->id) == nullptr && "Type ID already registered");

    // Ensure name is unique
    assert(_nameToId.find(type->name) == _nameToId.end() && "Type name already registered");

    TypeDescriptor* ptr = type.get();
    _nameToId[type->name] = type->id;
    _types.push_back(std::move(type));
    return ptr;
}

} // namespace CoreVM
