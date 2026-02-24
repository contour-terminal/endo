// SPDX-License-Identifier: Apache-2.0
#include <endo-language/ide/TypeRegistryCompletionAdapter.hpp>

namespace endo
{

namespace
{
    /// @brief Converts a LiteralType to a human-readable type name for completion display.
    [[nodiscard]] std::string literalTypeName(CoreVM::LiteralType type)
    {
        switch (type)
        {
            case CoreVM::LiteralType::Number: return "int";
            case CoreVM::LiteralType::Float: return "float";
            case CoreVM::LiteralType::String: return "str";
            case CoreVM::LiteralType::Boolean: return "bool";
            case CoreVM::LiteralType::Object: return "object";
            default: return "unknown";
        }
    }
} // namespace

std::unordered_map<std::string, std::vector<RecordFieldInfo>> builtinRecordFields(
    CoreVM::TypeRegistry const& registry)
{
    std::unordered_map<std::string, std::vector<RecordFieldInfo>> result;

    for (auto const& type: registry.allTypes())
    {
        if (type->kind != CoreVM::TypeKind::Product)
            continue;

        // Skip tuple types (no named fields)
        if (type->name == "Tuple2" || type->name == "Tuple3")
            continue;

        std::vector<RecordFieldInfo> fields;
        for (auto const& field: type->fields)
        {
            if (field.name.empty())
                continue; // Skip unnamed positional fields

            auto typeName = field.type == CoreVM::LiteralType::Object && !field.nestedTypeName.empty()
                                ? field.nestedTypeName
                                : literalTypeName(field.type);

            fields.push_back(RecordFieldInfo { .name = field.name, .typeName = std::move(typeName) });
        }

        if (!fields.empty())
            result[type->name] = std::move(fields);
    }

    return result;
}

ModuleFunctionMap builtinModuleFunctions(CoreVM::TypeRegistry const& registry)
{
    ModuleFunctionMap result;

    for (auto const& type: registry.allTypes())
    {
        if (type->moduleFunctions.empty())
            continue;

        result[type->name] = type->moduleFunctions;
    }

    return result;
}

std::vector<CompletionCandidate> constructorCandidatesFromRegistry(CoreVM::TypeRegistry const& registry)
{
    std::vector<CompletionCandidate> result;

    for (auto const& type: registry.allTypes())
    {
        if (type->kind != CoreVM::TypeKind::Sum)
            continue;

        // Skip List — internal type, not user-constructible via variant names
        if (type->name == "List")
            continue;

        for (auto const& variant: type->variants)
        {
            auto description = type->name + " constructor";
            if (variant.payloadSlots == 0)
                description += " (no value)";
            else
                description += " (value present)";

            result.push_back(CompletionCandidate {
                .text = variant.name,
                .displayText = variant.name,
                .description = std::move(description),
                .detail = {},
                .kind = CompletionKind::Constructor,
            });
        }
    }

    return result;
}

std::vector<CompletionCandidate> moduleFunctionStdLibCandidates(CoreVM::TypeRegistry const& registry)
{
    std::vector<CompletionCandidate> result;

    for (auto const& type: registry.allTypes())
    {
        for (auto const& fn: type->moduleFunctions)
        {
            auto const qualifiedName = type->name + "." + fn.name;
            result.push_back(CompletionCandidate {
                .text = qualifiedName,
                .displayText = qualifiedName,
                .description = fn.signature,
                .detail = {},
                .kind = CompletionKind::Function,
            });
        }
    }

    return result;
}

} // namespace endo
