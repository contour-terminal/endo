// SPDX-License-Identifier: Apache-2.0
#include <endo-language/sema/TypeRegistry.hpp>

#include <CoreVM/types/TypeRegistry.hpp>

#include <array>
#include <string_view>

namespace endo
{

namespace
{
    /// The builtin records the language exposes to type checking.
    ///
    /// Names only: each record's field layout is read from CoreVM's descriptor, which is where it
    /// is declared and where the VM itself reads it from, so a field added there needs no edit
    /// here. Kept an explicit list rather than "every Product type CoreVM knows", because
    /// resolveRecordByFields() matches anonymous record literals against these — admitting
    /// runtime shapes such as Tuple2, Markdown or Json would let a literal resolve to one.
    constexpr auto ExposedBuiltinRecords = std::array {
        std::string_view { "ProcessInfo" }, std::string_view { "DateTime" },
        std::string_view { "Size" },        std::string_view { "TimeSpan" },
        std::string_view { "FileMode" },    std::string_view { "FileInfo" },
        std::string_view { "JobInfo" },     std::string_view { "KeyBindingInfo" },
    };
} // namespace

void TypeDefinitionRegistry::registerBuiltins()
{
    auto const& runtime = CoreVM::builtinTypes();

    for (auto const name: ExposedBuiltinRecords)
    {
        auto const* descriptor = runtime.getByName(name);
        if (descriptor == nullptr)
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

        _recordTypes[std::string { name }] = std::move(info);
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
