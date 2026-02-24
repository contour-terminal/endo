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
    optionType->slotCount = 2; // 1 payload slot + 1 type tag slot
    optionType->variants = {
        { "None", 0 }, // tag 0: no payload
        { "Some", 1 }, // tag 1: 1 slot payload
    };
    optionType->moduleFunctions = {
        { "map", "Option.map f opt -> option" },
        { "bind", "Option.bind f opt -> option" },
        { "defaultValue", "Option.defaultValue d opt -> value" },
    };
    addType(std::move(optionType));

    // Result<T,E>: Error (tag=0, 1 payload slot) | Ok (tag=1, 1 payload slot)
    auto resultType = std::make_unique<TypeDescriptor>();
    resultType->kind = TypeKind::Sum;
    resultType->id = BuiltinTypeId::Result;
    resultType->name = "Result";
    resultType->slotCount = 2; // 1 payload slot + 1 type tag slot
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
    tuple2Type->fields = {
        { "", 0 }, // slot 0 (unnamed positional)
        { "", 1 }, // slot 1
    };
    tuple2Type->slotCount = 3; // 2 element slots + 1 packed type tag slot
    addType(std::move(tuple2Type));

    // Tuple3: 3-element product type
    auto tuple3Type = std::make_unique<TypeDescriptor>();
    tuple3Type->kind = TypeKind::Product;
    tuple3Type->id = BuiltinTypeId::Tuple3;
    tuple3Type->name = "Tuple3";
    tuple3Type->fields = {
        { "", 0 }, // slot 0
        { "", 1 }, // slot 1
        { "", 2 }, // slot 2
    };
    tuple3Type->slotCount = 4; // 3 element slots + 1 packed type tag slot
    addType(std::move(tuple3Type));

    // List: Nil (tag=0, 0 payload slots) | Cons (tag=1, 2 slots: head + tail)
    auto listType = std::make_unique<TypeDescriptor>();
    listType->kind = TypeKind::Sum;
    listType->id = BuiltinTypeId::List;
    listType->name = "List";
    listType->slotCount = 3; // 2 payload slots (head + tail) + 1 type tag slot
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
        { "pid", 0, LiteralType::Number },  { "ppid", 1, LiteralType::Number },
        { "user", 2, LiteralType::String }, { "cpu", 3, LiteralType::Float },
        { "mem", 4, LiteralType::Float },   { "command", 5, LiteralType::String },
    };
    addType(std::move(processInfoType));

    // DateTime: Product type with 7 fields for date/time representation
    auto dateTimeType = std::make_unique<TypeDescriptor>();
    dateTimeType->kind = TypeKind::Product;
    dateTimeType->id = BuiltinTypeId::DateTime;
    dateTimeType->name = "DateTime";
    dateTimeType->slotCount = 7;
    dateTimeType->fields = {
        { "year", 0, LiteralType::Number },   { "month", 1, LiteralType::Number },
        { "day", 2, LiteralType::Number },    { "hour", 3, LiteralType::Number },
        { "minute", 4, LiteralType::Number }, { "second", 5, LiteralType::Number },
        { "epoch", 6, LiteralType::Number },
    };
    dateTimeType->moduleFunctions = {
        { "now", "DateTime.now -> DateTime (current UTC time)" },
        { "fromEpoch", "DateTime.fromEpoch epoch -> DateTime" },
    };
    addType(std::move(dateTimeType));

    // FileInfo: Product type with 5 fields for file/directory information
    auto fileInfoType = std::make_unique<TypeDescriptor>();
    fileInfoType->kind = TypeKind::Product;
    fileInfoType->id = BuiltinTypeId::FileInfo;
    fileInfoType->name = "FileInfo";
    fileInfoType->slotCount = 5;
    fileInfoType->fields = {
        { "name", 0, LiteralType::String },
        { "size", 1, LiteralType::Object, "Size" },
        { "mode", 2, LiteralType::Object, "FileMode" },
        { "mtime", 3, LiteralType::Object, "DateTime" },
        { "isDir", 4, LiteralType::Boolean },
    };
    addType(std::move(fileInfoType));

    // JobInfo: Product type with 4 fields for background job information
    auto jobInfoType = std::make_unique<TypeDescriptor>();
    jobInfoType->kind = TypeKind::Product;
    jobInfoType->id = BuiltinTypeId::JobInfo;
    jobInfoType->name = "JobInfo";
    jobInfoType->slotCount = 4;
    jobInfoType->fields = {
        { "id", 0, LiteralType::Number },
        { "state", 1, LiteralType::String },
        { "command", 2, LiteralType::String },
        { "pid", 3, LiteralType::Number },
    };
    addType(std::move(jobInfoType));

    // Size: Product type with 1 field for byte count
    auto sizeType = std::make_unique<TypeDescriptor>();
    sizeType->kind = TypeKind::Product;
    sizeType->id = BuiltinTypeId::Size;
    sizeType->name = "Size";
    sizeType->slotCount = 1;
    sizeType->fields = {
        { "bytes", 0, LiteralType::Number },
    };
    sizeType->moduleFunctions = {
        { "fromBytes", "Size.fromBytes n -> Size" },
        { "fromKB", "Size.fromKB n -> Size (n * 1024 bytes)" },
        { "fromMB", "Size.fromMB n -> Size (n * 1024² bytes)" },
        { "fromGB", "Size.fromGB n -> Size (n * 1024³ bytes)" },
        { "fromTB", "Size.fromTB n -> Size (n * 1024⁴ bytes)" },
    };
    addType(std::move(sizeType));

    // FileMode: Product type with 1 field for raw Unix permission bits
    auto fileModeType = std::make_unique<TypeDescriptor>();
    fileModeType->kind = TypeKind::Product;
    fileModeType->id = BuiltinTypeId::FileMode;
    fileModeType->name = "FileMode";
    fileModeType->slotCount = 1;
    fileModeType->fields = {
        { "bits", 0, LiteralType::Number },
    };
    fileModeType->moduleFunctions = {
        { "fromBits", "FileMode.fromBits n -> FileMode" },
    };
    addType(std::move(fileModeType));

    // Update _nextId to be after the builtin type IDs
    _nextId = std::max(_nextId, static_cast<uint16_t>(BuiltinTypeId::LastBuiltin + 1));
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
