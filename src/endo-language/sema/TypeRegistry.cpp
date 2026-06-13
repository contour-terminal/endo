// SPDX-License-Identifier: Apache-2.0
#include <endo-language/sema/TypeRegistry.hpp>

namespace endo
{

void TypeDefinitionRegistry::registerBuiltins()
{
    // ProcessInfo: ps-style process information
    {
        RecordTypeInfo processInfoType;
        processInfoType.typeId = CoreVM::BuiltinTypeId::ProcessInfo;
        processInfoType.name = "ProcessInfo";
        processInfoType.fields = {
            { .name = "pid", .offset = 0, .type = CoreVM::LiteralType::Number },
            { .name = "ppid", .offset = 1, .type = CoreVM::LiteralType::Number },
            { .name = "user", .offset = 2, .type = CoreVM::LiteralType::String },
            { .name = "cpu", .offset = 3, .type = CoreVM::LiteralType::Float },
            { .name = "mem", .offset = 4, .type = CoreVM::LiteralType::Object },
            { .name = "command", .offset = 5, .type = CoreVM::LiteralType::String },
        };
        for (auto const& f: processInfoType.fields)
            processInfoType.fieldTypes[f.name] = f.type;
        processInfoType.fieldObjectTypeIds["mem"] = CoreVM::BuiltinTypeId::Size;
        _recordTypes["ProcessInfo"] = std::move(processInfoType);
    }

    // DateTime: date/time components
    {
        RecordTypeInfo dateTimeType;
        dateTimeType.typeId = CoreVM::BuiltinTypeId::DateTime;
        dateTimeType.name = "DateTime";
        dateTimeType.fields = {
            { .name = "year", .offset = 0, .type = CoreVM::LiteralType::Number },
            { .name = "month", .offset = 1, .type = CoreVM::LiteralType::Number },
            { .name = "day", .offset = 2, .type = CoreVM::LiteralType::Number },
            { .name = "hour", .offset = 3, .type = CoreVM::LiteralType::Number },
            { .name = "minute", .offset = 4, .type = CoreVM::LiteralType::Number },
            { .name = "second", .offset = 5, .type = CoreVM::LiteralType::Number },
            { .name = "epoch", .offset = 6, .type = CoreVM::LiteralType::Number },
        };
        for (auto const& f: dateTimeType.fields)
            dateTimeType.fieldTypes[f.name] = f.type;
        _recordTypes["DateTime"] = std::move(dateTimeType);
    }

    // Size: byte-count wrapper
    {
        RecordTypeInfo sizeType;
        sizeType.typeId = CoreVM::BuiltinTypeId::Size;
        sizeType.name = "Size";
        sizeType.fields = {
            { .name = "bytes", .offset = 0, .type = CoreVM::LiteralType::Number },
        };
        for (auto const& f: sizeType.fields)
            sizeType.fieldTypes[f.name] = f.type;
        _recordTypes["Size"] = std::move(sizeType);
    }

    // TimeSpan: duration in milliseconds
    {
        RecordTypeInfo timeSpanType;
        timeSpanType.typeId = CoreVM::BuiltinTypeId::TimeSpan;
        timeSpanType.name = "TimeSpan";
        timeSpanType.fields = {
            { .name = "milliseconds", .offset = 0, .type = CoreVM::LiteralType::Number },
        };
        for (auto const& f: timeSpanType.fields)
            timeSpanType.fieldTypes[f.name] = f.type;
        _recordTypes["TimeSpan"] = std::move(timeSpanType);
    }

    // FileMode: permission bits
    {
        RecordTypeInfo fileModeType;
        fileModeType.typeId = CoreVM::BuiltinTypeId::FileMode;
        fileModeType.name = "FileMode";
        fileModeType.fields = {
            { .name = "bits", .offset = 0, .type = CoreVM::LiteralType::Number },
        };
        for (auto const& f: fileModeType.fields)
            fileModeType.fieldTypes[f.name] = f.type;
        _recordTypes["FileMode"] = std::move(fileModeType);
    }

    // FileInfo: file metadata from the ls builtin
    {
        RecordTypeInfo fileInfoType;
        fileInfoType.typeId = CoreVM::BuiltinTypeId::FileInfo;
        fileInfoType.name = "FileInfo";
        fileInfoType.fields = {
            { .name = "name", .offset = 0, .type = CoreVM::LiteralType::String },
            { .name = "size", .offset = 1, .type = CoreVM::LiteralType::Object },
            { .name = "mode", .offset = 2, .type = CoreVM::LiteralType::Object },
            { .name = "mtime", .offset = 3, .type = CoreVM::LiteralType::Object },
            { .name = "isDir", .offset = 4, .type = CoreVM::LiteralType::Boolean },
        };
        for (auto const& f: fileInfoType.fields)
            fileInfoType.fieldTypes[f.name] = f.type;
        fileInfoType.fieldObjectTypeIds["mode"] = CoreVM::BuiltinTypeId::FileMode;
        fileInfoType.fieldObjectTypeIds["mtime"] = CoreVM::BuiltinTypeId::DateTime;
        fileInfoType.fieldObjectTypeIds["size"] = CoreVM::BuiltinTypeId::Size;
        _recordTypes["FileInfo"] = std::move(fileInfoType);
    }

    // JobInfo: background job metadata
    {
        RecordTypeInfo jobInfoType;
        jobInfoType.typeId = CoreVM::BuiltinTypeId::JobInfo;
        jobInfoType.name = "JobInfo";
        jobInfoType.fields = {
            { .name = "id", .offset = 0, .type = CoreVM::LiteralType::Number },
            { .name = "state", .offset = 1, .type = CoreVM::LiteralType::String },
            { .name = "command", .offset = 2, .type = CoreVM::LiteralType::String },
            { .name = "pid", .offset = 3, .type = CoreVM::LiteralType::Number },
        };
        for (auto const& f: jobInfoType.fields)
            jobInfoType.fieldTypes[f.name] = f.type;
        _recordTypes["JobInfo"] = std::move(jobInfoType);
    }

    // KeyBindingInfo: keybinding metadata from the bind builtin
    {
        RecordTypeInfo keyBindingInfoType;
        keyBindingInfoType.typeId = CoreVM::BuiltinTypeId::KeyBindingInfo;
        keyBindingInfoType.name = "KeyBindingInfo";
        keyBindingInfoType.fields = {
            { .name = "key", .offset = 0, .type = CoreVM::LiteralType::String },
            { .name = "action", .offset = 1, .type = CoreVM::LiteralType::String },
        };
        for (auto const& f: keyBindingInfoType.fields)
            keyBindingInfoType.fieldTypes[f.name] = f.type;
        _recordTypes["KeyBindingInfo"] = std::move(keyBindingInfoType);
    }
}

void TypeDefinitionRegistry::registerRecord(std::string name, RecordTypeInfo info)
{
    _recordTypes[std::move(name)] = std::move(info);
}

void TypeDefinitionRegistry::registerUnion(std::string name, UnionTypeInfo info)
{
    _unionTypes[std::move(name)] = std::move(info);
}

void TypeDefinitionRegistry::registerConstructor(std::string name, ConstructorInfo info)
{
    _constructorRegistry[std::move(name)] = std::move(info);
}

RecordTypeInfo const* TypeDefinitionRegistry::lookupRecord(std::string const& name) const
{
    if (auto it = _recordTypes.find(name); it != _recordTypes.end())
        return &it->second;
    return nullptr;
}

RecordTypeInfo const* TypeDefinitionRegistry::resolveRecordByFields(
    std::vector<std::string> const& fieldNames) const
{
    for (auto const& [name, info]: _recordTypes)
    {
        if (info.fields.size() != fieldNames.size())
            continue;
        bool match = true;
        for (size_t i = 0; i < fieldNames.size(); ++i)
        {
            if (info.fields[i].name != fieldNames[i])
            {
                match = false;
                break;
            }
        }
        if (match)
            return &info;
    }
    return nullptr;
}

UnionTypeInfo const* TypeDefinitionRegistry::lookupUnion(std::string const& name) const
{
    if (auto it = _unionTypes.find(name); it != _unionTypes.end())
        return &it->second;
    return nullptr;
}

ConstructorInfo const* TypeDefinitionRegistry::lookupConstructor(std::string const& name) const
{
    if (auto it = _constructorRegistry.find(name); it != _constructorRegistry.end())
        return &it->second;
    return nullptr;
}

} // namespace endo
