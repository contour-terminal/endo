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
            { "pid", 0, CoreVM::LiteralType::Number },  { "ppid", 1, CoreVM::LiteralType::Number },
            { "user", 2, CoreVM::LiteralType::String }, { "cpu", 3, CoreVM::LiteralType::Float },
            { "mem", 4, CoreVM::LiteralType::Object },  { "command", 5, CoreVM::LiteralType::String },
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
            { "year", 0, CoreVM::LiteralType::Number },   { "month", 1, CoreVM::LiteralType::Number },
            { "day", 2, CoreVM::LiteralType::Number },    { "hour", 3, CoreVM::LiteralType::Number },
            { "minute", 4, CoreVM::LiteralType::Number }, { "second", 5, CoreVM::LiteralType::Number },
            { "epoch", 6, CoreVM::LiteralType::Number },
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
            { "bytes", 0, CoreVM::LiteralType::Number },
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
            { "milliseconds", 0, CoreVM::LiteralType::Number },
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
            { "bits", 0, CoreVM::LiteralType::Number },
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
            { "name", 0, CoreVM::LiteralType::String },   { "size", 1, CoreVM::LiteralType::Object },
            { "mode", 2, CoreVM::LiteralType::Object },   { "mtime", 3, CoreVM::LiteralType::Object },
            { "isDir", 4, CoreVM::LiteralType::Boolean },
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
            { "id", 0, CoreVM::LiteralType::Number },
            { "state", 1, CoreVM::LiteralType::String },
            { "command", 2, CoreVM::LiteralType::String },
            { "pid", 3, CoreVM::LiteralType::Number },
        };
        for (auto const& f: jobInfoType.fields)
            jobInfoType.fieldTypes[f.name] = f.type;
        _recordTypes["JobInfo"] = std::move(jobInfoType);
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
