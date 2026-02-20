// SPDX-License-Identifier: Apache-2.0
#include <endo-language/types/Type.hpp>

#include <algorithm>
#include <format>
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

bool TypeScheme::operator==(TypeScheme const& other) const
{
    if (quantifiedVars.size() != other.quantifiedVars.size())
        return false;
    // Note: We compare the structure, not the actual variable IDs
    // Two schemes are equal if they have the same shape
    return *type == *other.type;
}

TypePtr TypeScheme::instantiate(std::function<TypeVarId()> freshVarGen) const
{
    if (quantifiedVars.empty())
        return type;

    // Create a mapping from old type variable IDs to fresh ones
    std::unordered_map<TypeVarId, TypeVarId> substitution;
    for (auto varId: quantifiedVars)
    {
        substitution[varId] = freshVarGen();
    }

    // Helper to substitute type variables recursively
    std::function<TypePtr(TypePtr const&)> substitute = [&](TypePtr const& t) -> TypePtr {
        if (auto* tv = t->asTypeVar())
        {
            auto it = substitution.find(tv->id);
            if (it != substitution.end())
                return types::typeVar(it->second);
            return t;
        }
        else if (auto* fn = t->asFunction())
        {
            return types::function(substitute(fn->paramType), substitute(fn->returnType));
        }
        else if (auto* lst = t->asList())
        {
            return types::list(substitute(lst->elementType));
        }
        else if (auto* tup = t->asTuple())
        {
            std::vector<TypePtr> newElements;
            for (auto const& elem: tup->elementTypes)
                newElements.push_back(substitute(elem));
            return types::tuple(std::move(newElements));
        }
        else if (auto* opt = t->asOption())
        {
            return types::option(substitute(opt->innerType));
        }
        else if (auto* res = t->asResult())
        {
            return types::result(substitute(res->okType), substitute(res->errorType));
        }
        else if (auto* rec = t->asRecord())
        {
            std::vector<RecordField> newFields;
            for (auto const& field: rec->fields)
                newFields.push_back({ field.name, substitute(field.type) });
            return types::record(rec->name, std::move(newFields));
        }
        else if (auto* un = t->asUnion())
        {
            std::vector<UnionCase> newCases;
            for (auto const& c: un->cases)
            {
                if (c.payloadType)
                    newCases.push_back({ c.name, substitute(*c.payloadType) });
                else
                    newCases.push_back({ c.name, std::nullopt });
            }
            return types::unionType(un->name, std::move(newCases));
        }
        // Primitive types don't contain type variables
        return t;
    };

    return substitute(type);
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
        return std::make_shared<Type>(Type { FunctionType { std::move(param), std::move(ret) } });
    }

    TypePtr function(std::vector<TypePtr> const& params, TypePtr ret)
    {
        if (params.empty())
            return ret;

        // Build curried function type right-to-left: a -> b -> c -> ret
        TypePtr result = ret;
        for (auto it = params.rbegin(); it != params.rend(); ++it)
        {
            result = function(*it, result);
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
        return std::make_shared<Type>(Type { ResultType { std::move(ok), std::move(error) } });
    }

    TypePtr record(std::string name, std::vector<RecordField> fields)
    {
        return std::make_shared<Type>(Type { RecordType { std::move(name), std::move(fields) } });
    }

    TypePtr anonymousRecord(std::vector<RecordField> fields)
    {
        return record("", std::move(fields));
    }

    TypePtr unionType(std::string name, std::vector<UnionCase> cases)
    {
        return std::make_shared<Type>(Type { UnionType { std::move(name), std::move(cases) } });
    }

    TypeScheme scheme(std::vector<TypeVarId> quantified, TypePtr type)
    {
        return TypeScheme { std::move(quantified), std::move(type) };
    }

    TypeScheme monomorphic(TypePtr type)
    {
        return TypeScheme { {}, std::move(type) };
    }

} // namespace types

// Pretty-printing

std::string toString(PrimitiveType prim)
{
    switch (prim)
    {
        case PrimitiveType::Int: return "int";
        case PrimitiveType::Float: return "float";
        case PrimitiveType::Str: return "str";
        case PrimitiveType::Bool: return "bool";
        case PrimitiveType::Unit: return "unit";
    }
    return "?";
}

std::string toString(Type const& type)
{
    if (auto* tv = type.asTypeVar())
    {
        // Use lowercase letters for type variables: a, b, c, ...
        // For ids >= 26, use a1, b1, etc.
        char letter = 'a' + static_cast<char>(tv->id % 26);
        uint32_t suffix = tv->id / 26;
        if (suffix == 0)
            return std::string(1, letter);
        else
            return std::format("{}{}", letter, suffix);
    }
    else if (auto* prim = type.asPrimitive())
    {
        return toString(prim->kind);
    }
    else if (auto* fn = type.asFunction())
    {
        std::string paramStr = toString(*fn->paramType);
        // Add parentheses around function parameter types for clarity
        if (fn->paramType->isFunction())
            paramStr = "(" + paramStr + ")";
        return paramStr + " -> " + toString(*fn->returnType);
    }
    else if (auto* lst = type.asList())
    {
        return "list<" + toString(*lst->elementType) + ">";
    }
    else if (auto* tup = type.asTuple())
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
    else if (auto* opt = type.asOption())
    {
        return "option<" + toString(*opt->innerType) + ">";
    }
    else if (auto* res = type.asResult())
    {
        return "result<" + toString(*res->okType) + ", " + toString(*res->errorType) + ">";
    }
    else if (auto* rec = type.asRecord())
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
    else if (auto* un = type.asUnion())
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
    return "?";
}

std::string toString(TypePtr const& type)
{
    if (!type)
        return "null";
    return toString(*type);
}

std::string toString(TypeScheme const& scheme)
{
    if (scheme.quantifiedVars.empty())
        return toString(*scheme.type);

    std::ostringstream oss;
    oss << "forall";
    for (auto varId: scheme.quantifiedVars)
    {
        char letter = 'a' + static_cast<char>(varId % 26);
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
