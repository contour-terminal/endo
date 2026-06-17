// SPDX-License-Identifier: Apache-2.0
#include <endo-language/types/Type.hpp>

#include <algorithm>
#include <format>
#include <ranges>
#include <sstream>
#include <unordered_map>

namespace endo
{

// Equality operators for compound types (need to compare TypePtr contents)

bool FunctionType::operator==(FunctionType const& other) const
{
    return *paramType == *other.paramType && *returnType == *other.returnType;
}

bool ListType::operator==(ListType const& other) const
{
    return *elementType == *other.elementType;
}

bool TupleType::operator==(TupleType const& other) const
{
    if (elementTypes.size() != other.elementTypes.size())
        return false;
    for (size_t i = 0; i < elementTypes.size(); ++i)
    {
        if (*elementTypes[i] != *other.elementTypes[i])
            return false;
    }
    return true;
}

bool OptionType::operator==(OptionType const& other) const
{
    return *innerType == *other.innerType;
}

bool ResultType::operator==(ResultType const& other) const
{
    return *okType == *other.okType && *errorType == *other.errorType;
}

bool RefType::operator==(RefType const& other) const
{
    return *innerType == *other.innerType;
}

bool RecordField::operator==(RecordField const& other) const
{
    return name == other.name && *type == *other.type;
}

bool RecordType::operator==(RecordType const& other) const
{
    if (name != other.name || fields.size() != other.fields.size())
        return false;
    for (size_t i = 0; i < fields.size(); ++i)
    {
        if (fields[i] != other.fields[i])
            return false;
    }
    return true;
}

std::optional<TypePtr> RecordType::fieldType(std::string const& fieldName) const
{
    for (auto const& field: fields)
    {
        if (field.name == fieldName)
            return field.type;
    }
    return std::nullopt;
}

bool UnionCase::operator==(UnionCase const& other) const
{
    if (name != other.name)
        return false;
    if (payloadType.has_value() != other.payloadType.has_value())
        return false;
    if (payloadType.has_value() && *payloadType.value() != *other.payloadType.value())
        return false;
    return true;
}

bool UnionType::operator==(UnionType const& other) const
{
    if (name != other.name || cases.size() != other.cases.size())
        return false;
    for (size_t i = 0; i < cases.size(); ++i)
    {
        if (cases[i] != other.cases[i])
            return false;
    }
    return true;
}

std::optional<UnionCase const*> UnionType::findCase(std::string const& caseName) const
{
    for (auto const& c: cases)
    {
        if (c.name == caseName)
            return &c;
    }
    return std::nullopt;
}

bool TypeApp::operator==(TypeApp const& other) const
{
    if (name != other.name || args.size() != other.args.size())
        return false;
    for (size_t i = 0; i < args.size(); ++i)
    {
        if (*args[i] != *other.args[i])
            return false;
    }
    return true;
}

bool TypeScheme::operator==(TypeScheme const& other) const
{
    if (quantifiedVars.size() != other.quantifiedVars.size())
        return false;
    // Note: We compare the structure, not the actual variable IDs
    // Two schemes are equal if they have the same shape
    return *type == *other.type;
}

TypePtr TypeScheme::instantiate(std::function<TypeVarId()> const& freshVarGen) const
{
    if (quantifiedVars.empty())
        return type;

    // Create a mapping from old type variable IDs to fresh ones
    std::unordered_map<TypeVarId, TypeVarId> substitution;
    for (auto varId: quantifiedVars)
        substitution[varId] = freshVarGen();

    return transformType(type, [&](TypePtr const& t) -> TypePtr {
        if (auto const* tv = t->asTypeVar())
        {
            if (auto it = substitution.find(tv->id); it != substitution.end())
                return types::typeVar(it->second);
        }
        return nullptr;
    });
}

TypePtr transformType(TypePtr const& type, std::function<TypePtr(TypePtr const&)> const& visitor)
{
    if (auto result = visitor(type))
        return result;

    if (auto const* fn = type->asFunction())
        return types::function(transformType(fn->paramType, visitor), transformType(fn->returnType, visitor));
    if (auto const* lst = type->asList())
        return types::list(transformType(lst->elementType, visitor));
    if (auto const* tup = type->asTuple())
    {
        std::vector<TypePtr> newElements;
        newElements.reserve(tup->elementTypes.size());
        for (auto const& elem: tup->elementTypes)
            newElements.push_back(transformType(elem, visitor));
        return types::tuple(std::move(newElements));
    }
    if (auto const* opt = type->asOption())
        return types::option(transformType(opt->innerType, visitor));
    if (auto const* res = type->asResult())
        return types::result(transformType(res->okType, visitor), transformType(res->errorType, visitor));
    if (auto const* r = type->asRef())
        return types::ref(transformType(r->innerType, visitor));
    if (auto const* rec = type->asRecord())
    {
        std::vector<RecordField> newFields;
        newFields.reserve(rec->fields.size());
        for (auto const& field: rec->fields)
            newFields.push_back({ .name = field.name, .type = transformType(field.type, visitor) });
        return types::record(rec->name, std::move(newFields));
    }
    if (auto const* un = type->asUnion())
    {
        std::vector<UnionCase> newCases;
        newCases.reserve(un->cases.size());
        for (auto const& c: un->cases)
        {
            if (c.payloadType)
                newCases.push_back({ .name = c.name, .payloadType = transformType(*c.payloadType, visitor) });
            else
                newCases.push_back({ .name = c.name, .payloadType = std::nullopt });
        }
        return types::unionType(un->name, std::move(newCases));
    }
    if (auto const* app = type->asTypeApp())
    {
        std::vector<TypePtr> newArgs;
        newArgs.reserve(app->args.size());
        for (auto const& arg: app->args)
            newArgs.push_back(transformType(arg, visitor));
        return types::typeApp(app->name, std::move(newArgs));
    }

    return type; // Primitives and TypeVars unchanged
}

// Factory functions
namespace types
{
    namespace
    {
        // Cached primitive types (singletons)
        TypePtr cachedInt;
        TypePtr cachedFloat;
        TypePtr cachedStr;
        TypePtr cachedBool;
        TypePtr cachedUnit;
    } // namespace

    TypePtr intType()
    {
        if (!cachedInt)
            cachedInt = std::make_shared<Type>(Type { PrimitiveTypeNode { PrimitiveType::Int } });
        return cachedInt;
    }

    TypePtr floatType()
    {
        if (!cachedFloat)
            cachedFloat = std::make_shared<Type>(Type { PrimitiveTypeNode { PrimitiveType::Float } });
        return cachedFloat;
    }

    TypePtr strType()
    {
        if (!cachedStr)
            cachedStr = std::make_shared<Type>(Type { PrimitiveTypeNode { PrimitiveType::Str } });
        return cachedStr;
    }

    TypePtr boolType()
    {
        if (!cachedBool)
            cachedBool = std::make_shared<Type>(Type { PrimitiveTypeNode { PrimitiveType::Bool } });
        return cachedBool;
    }

    TypePtr unitType()
    {
        if (!cachedUnit)
            cachedUnit = std::make_shared<Type>(Type { PrimitiveTypeNode { PrimitiveType::Unit } });
        return cachedUnit;
    }

    TypePtr typeVar(TypeVarId id)
    {
        return std::make_shared<Type>(Type { TypeVar { id } });
    }

    TypePtr function(TypePtr param, TypePtr ret)
    {
        return std::make_shared<Type>(
            Type { FunctionType { .paramType = std::move(param), .returnType = std::move(ret) } });
    }

    TypePtr function(std::vector<TypePtr> const& params, TypePtr ret)
    {
        if (params.empty())
            return ret;

        // Build curried function type right-to-left: a -> b -> c -> ret
        TypePtr result = ret;
        for (const auto& param: std::ranges::reverse_view(params))
        {
            result = function(param, result);
        }
        return result;
    }

    TypePtr list(TypePtr element)
    {
        return std::make_shared<Type>(Type { ListType { std::move(element) } });
    }

    TypePtr tuple(std::vector<TypePtr> elements)
    {
        return std::make_shared<Type>(Type { TupleType { std::move(elements) } });
    }

    TypePtr option(TypePtr inner)
    {
        return std::make_shared<Type>(Type { OptionType { std::move(inner) } });
    }

    TypePtr result(TypePtr ok, TypePtr error)
    {
        return std::make_shared<Type>(
            Type { ResultType { .okType = std::move(ok), .errorType = std::move(error) } });
    }

    TypePtr ref(TypePtr inner)
    {
        return std::make_shared<Type>(Type { RefType { std::move(inner) } });
    }

    TypePtr record(std::string name, std::vector<RecordField> fields)
    {
        return std::make_shared<Type>(
            Type { RecordType { .name = std::move(name), .fields = std::move(fields) } });
    }

    TypePtr anonymousRecord(std::vector<RecordField> fields)
    {
        return record("", std::move(fields));
    }

    TypePtr unionType(std::string name, std::vector<UnionCase> cases)
    {
        return std::make_shared<Type>(
            Type { UnionType { .name = std::move(name), .cases = std::move(cases) } });
    }

    TypePtr typeApp(std::string name, std::vector<TypePtr> args)
    {
        return std::make_shared<Type>(Type { TypeApp { .name = std::move(name), .args = std::move(args) } });
    }

    TypeScheme scheme(std::vector<TypeVarId> quantified, TypePtr type)
    {
        return TypeScheme { .quantifiedVars = std::move(quantified), .type = std::move(type) };
    }

    TypeScheme monomorphic(TypePtr type)
    {
        return TypeScheme { .quantifiedVars = {}, .type = std::move(type) };
    }

} // namespace types

// Pretty-printing

std::string toString(PrimitiveType prim)
{
    switch (prim)
    {
        case PrimitiveType::Int: return "int";
        case PrimitiveType::Float: return "float";
        case PrimitiveType::Str: return "string";
        case PrimitiveType::Bool: return "bool";
        case PrimitiveType::Unit: return "unit";
    }
    return "?";
}

std::string toString(Type const& type)
{
    if (const auto* tv = type.asTypeVar())
    {
        // Use lowercase letters with tick prefix for type variables: 'a, 'b, 'c, ...
        // For ids >= 26, use 'a1, 'b1, etc.
        auto letter = static_cast<char>('a' + static_cast<int>(tv->id % 26));
        uint32_t suffix = tv->id / 26;
        if (suffix == 0)
            return std::format("'{}", letter);
        else
            return std::format("'{}{}", letter, suffix);
    }
    else if (const auto* prim = type.asPrimitive())
    {
        return toString(prim->kind);
    }
    else if (const auto* fn = type.asFunction())
    {
        std::string paramStr = toString(*fn->paramType);
        // Add parentheses around function parameter types for clarity
        if (fn->paramType->isFunction())
            paramStr = "(" + paramStr + ")";
        return paramStr + " -> " + toString(*fn->returnType);
    }
    else if (const auto* lst = type.asList())
    {
        auto inner = toString(*lst->elementType);
        return "list<" + inner + (inner.back() == '>' ? " >" : ">");
    }
    else if (const auto* tup = type.asTuple())
    {
        std::ostringstream oss;
        oss << "(";
        for (size_t i = 0; i < tup->elementTypes.size(); ++i)
        {
            if (i > 0)
                oss << ", ";
            oss << toString(*tup->elementTypes[i]);
        }
        oss << ")";
        return oss.str();
    }
    else if (const auto* opt = type.asOption())
    {
        auto inner = toString(*opt->innerType);
        return "option<" + inner + (inner.back() == '>' ? " >" : ">");
    }
    else if (const auto* res = type.asResult())
    {
        auto errStr = toString(*res->errorType);
        return "result<" + toString(*res->okType) + ", " + errStr + (errStr.back() == '>' ? " >" : ">");
    }
    else if (const auto* r = type.asRef())
    {
        auto inner = toString(*r->innerType);
        return "ref<" + inner + (inner.back() == '>' ? " >" : ">");
    }
    else if (const auto* rec = type.asRecord())
    {
        std::ostringstream oss;
        if (!rec->name.empty())
            oss << rec->name << " ";
        oss << "{ ";
        for (size_t i = 0; i < rec->fields.size(); ++i)
        {
            if (i > 0)
                oss << "; ";
            oss << rec->fields[i].name << ": " << toString(*rec->fields[i].type);
        }
        oss << " }";
        return oss.str();
    }
    else if (const auto* un = type.asUnion())
    {
        std::ostringstream oss;
        oss << un->name;
        bool first = true;
        for (auto const& c: un->cases)
        {
            if (!first)
                oss << " |";
            else
                oss << " =";
            first = false;
            oss << " " << c.name;
            if (c.payloadType)
                oss << " of " << toString(**c.payloadType);
        }
        return oss.str();
    }
    else if (const auto* app = type.asTypeApp())
    {
        std::ostringstream oss;
        oss << app->name << "<";
        for (size_t i = 0; i < app->args.size(); ++i)
        {
            if (i > 0)
                oss << ", ";
            oss << toString(*app->args[i]);
        }
        auto result = oss.str();
        if (!result.empty() && result.back() == '>')
            result += " >";
        else
            result += ">";
        return result;
    }
    return "?";
}

std::string toString(TypePtr const& type)
{
    if (!type)
        return "null";
    return toString(*type);
}

std::string toString(Type const& type, TypeVarNameMap const& nameMap)
{
    if (const auto* tv = type.asTypeVar())
    {
        if (auto it = nameMap.find(tv->id); it != nameMap.end())
            return "'" + it->second;
        // Fall back to computed name
        auto letter = static_cast<char>('a' + static_cast<int>(tv->id % 26));
        uint32_t suffix = tv->id / 26;
        if (suffix == 0)
            return std::format("'{}", letter);
        else
            return std::format("'{}{}", letter, suffix);
    }
    else if (const auto* prim = type.asPrimitive())
    {
        return toString(prim->kind);
    }
    else if (const auto* fn = type.asFunction())
    {
        auto paramStr = toString(*fn->paramType, nameMap);
        if (fn->paramType->isFunction())
            paramStr = "(" + paramStr + ")";
        return paramStr + " -> " + toString(*fn->returnType, nameMap);
    }
    else if (const auto* lst = type.asList())
    {
        auto inner = toString(*lst->elementType, nameMap);
        return "list<" + inner + (inner.back() == '>' ? " >" : ">");
    }
    else if (const auto* tup = type.asTuple())
    {
        std::ostringstream oss;
        oss << "(";
        for (size_t i = 0; i < tup->elementTypes.size(); ++i)
        {
            if (i > 0)
                oss << ", ";
            oss << toString(*tup->elementTypes[i], nameMap);
        }
        oss << ")";
        return oss.str();
    }
    else if (const auto* opt = type.asOption())
    {
        auto inner = toString(*opt->innerType, nameMap);
        return "option<" + inner + (inner.back() == '>' ? " >" : ">");
    }
    else if (const auto* res = type.asResult())
    {
        auto errStr = toString(*res->errorType, nameMap);
        return "result<" + toString(*res->okType, nameMap) + ", " + errStr
               + (errStr.back() == '>' ? " >" : ">");
    }
    else if (const auto* r = type.asRef())
    {
        auto inner = toString(*r->innerType, nameMap);
        return "ref<" + inner + (inner.back() == '>' ? " >" : ">");
    }
    else if (const auto* rec = type.asRecord())
    {
        std::ostringstream oss;
        if (!rec->name.empty())
            oss << rec->name << " ";
        oss << "{ ";
        for (size_t i = 0; i < rec->fields.size(); ++i)
        {
            if (i > 0)
                oss << "; ";
            oss << rec->fields[i].name << ": " << toString(*rec->fields[i].type, nameMap);
        }
        oss << " }";
        return oss.str();
    }
    else if (const auto* un = type.asUnion())
    {
        std::ostringstream oss;
        oss << un->name;
        bool first = true;
        for (auto const& c: un->cases)
        {
            if (!first)
                oss << " |";
            else
                oss << " =";
            first = false;
            oss << " " << c.name;
            if (c.payloadType)
                oss << " of " << toString(**c.payloadType, nameMap);
        }
        return oss.str();
    }
    else if (const auto* app = type.asTypeApp())
    {
        std::ostringstream oss;
        oss << app->name << "<";
        for (size_t i = 0; i < app->args.size(); ++i)
        {
            if (i > 0)
                oss << ", ";
            oss << toString(*app->args[i], nameMap);
        }
        auto result = oss.str();
        if (!result.empty() && result.back() == '>')
            result += " >";
        else
            result += ">";
        return result;
    }
    return "?";
}

std::string toString(TypePtr const& type, TypeVarNameMap const& nameMap)
{
    if (!type)
        return "null";
    return toString(*type, nameMap);
}

std::string toString(TypeScheme const& scheme)
{
    if (scheme.quantifiedVars.empty())
        return toString(*scheme.type);

    std::ostringstream oss;
    oss << "forall";
    for (auto varId: scheme.quantifiedVars)
    {
        auto const letter = static_cast<char>('a' + (varId % 26));
        uint32_t suffix = varId / 26;
        oss << " ";
        if (suffix == 0)
            oss << letter;
        else
            oss << letter << suffix;
    }
    oss << ". " << toString(*scheme.type);
    return oss.str();
}

} // namespace endo
