// SPDX-License-Identifier: Apache-2.0
#include <endo-language/sema/TypeRegistry.hpp>

#include <CoreVM/types/TypeRegistry.hpp>

namespace endo
{

void TypeDefinitionRegistry::registerBuiltins()
{
    // Both which records the language exposes and their field layouts come from CoreVM's
    // descriptors — the former from TypeDescriptor::languageRecord, the latter from `fields` — so
    // adding a builtin record or a field to one needs no edit here. A name list would leave the
    // set to drift silently: a record dropped from it keeps working at runtime while losing type
    // checking, which nothing would report.
    auto const& runtime = CoreVM::builtinTypes();

    for (auto const& descriptor: runtime.allTypes())
    {
        if (!descriptor->languageRecord)
            continue;

        auto info = RecordTypeInfo {};
        info.typeId = descriptor->id;
        info.name = descriptor->name;
        info.fields = descriptor->fields;

        for (auto const& field: info.fields)
        {
            info.fieldTypes[field.name] = field.type;
            // An Object-typed field names its record type in the descriptor; resolve that name to
            // the id the type checker compares against.
            if (!field.nestedTypeName.empty())
                if (auto const* nested = runtime.getByName(field.nestedTypeName))
                    info.fieldObjectTypeIds[field.name] = nested->id;
        }

        _recordTypes[descriptor->name] = std::move(info);
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
