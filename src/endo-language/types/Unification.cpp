// SPDX-License-Identifier: Apache-2.0
#include <endo-language/types/Unification.hpp>

#include <algorithm>
#include <format>
#include <sstream>

namespace endo
{

// Substitution implementation

Substitution Substitution::single(TypeVarId varId, TypePtr type)
{
    Substitution s;
    s.add(varId, std::move(type));
    return s;
}

bool Substitution::contains(TypeVarId varId) const
{
    return _mappings.find(varId) != _mappings.end();
}

std::optional<TypePtr> Substitution::lookup(TypeVarId varId) const
{
    auto it = _mappings.find(varId);
    if (it != _mappings.end())
        return it->second;
    return std::nullopt;
}

void Substitution::add(TypeVarId varId, TypePtr type)
{
    _mappings[varId] = std::move(type);
}

TypePtr Substitution::apply(TypePtr const& type) const
{
    if (_mappings.empty())
        return type;

    return transformType(type, [this](TypePtr const& t) -> TypePtr {
        if (auto const* tv = t->asTypeVar())
        {
            if (auto it = _mappings.find(tv->id); it != _mappings.end())
                return apply(it->second);
        }
        return nullptr;
    });
}

TypeScheme Substitution::apply(TypeScheme const& scheme) const
{
    if (_mappings.empty())
        return scheme;

    // Create a substitution that excludes quantified variables
    Substitution filtered;
    for (auto const& [varId, type]: _mappings)
    {
        if (std::ranges::find(scheme.quantifiedVars, varId) == scheme.quantifiedVars.end())
        {
            filtered.add(varId, type);
        }
    }

    return types::scheme(scheme.quantifiedVars, filtered.apply(scheme.type));
}

Substitution Substitution::compose(Substitution const& other) const
{
    // (s1 compose s2)(t) = s1(s2(t))
    // So result contains: s2 with s1 applied to all values, plus s1's mappings
    Substitution result;

    // Apply s1 to all of s2's values
    for (auto const& [varId, type]: other._mappings)
    {
        result.add(varId, apply(type));
    }

    // Add s1's mappings (overwriting if already present)
    for (auto const& [varId, type]: _mappings)
    {
        result.add(varId, type);
    }

    return result;
}

std::string Substitution::toString() const
{
    if (_mappings.empty())
        return "{}";

    std::ostringstream oss;
    oss << "{";
    bool first = true;
    for (auto const& [varId, type]: _mappings)
    {
        if (!first)
            oss << ", ";
        first = false;
        auto const letter = static_cast<char>('a' + (varId % 26));
        oss << letter << " -> " << endo::toString(type);
    }
    oss << "}";
    return oss.str();
}

// TypeError implementation

TypeError TypeError::mismatch(TypePtr const& expected, TypePtr const& actual)
{
    return TypeError { .kind = Kind::Mismatch,
                       .message = std::format(
                           "Type mismatch: expected {}, got {}", toString(expected), toString(actual)),
                       .expected = expected,
                       .actual = actual };
}

TypeError TypeError::occursCheck(TypeVarId varId, TypePtr const& type)
{
    auto const letter = static_cast<char>('a' + (varId % 26));
    return TypeError { .kind = Kind::OccursCheck,
                       .message = std::format("Infinite type: {} occurs in {}", letter, toString(type)),
                       .expected = types::typeVar(varId),
                       .actual = type };
}

TypeError TypeError::arityMismatch(size_t expected, size_t actual)
{
    return TypeError { .kind = Kind::ArityMismatch,
                       .message =
                           std::format("Arity mismatch: expected {} elements, got {}", expected, actual),
                       .expected = nullptr,
                       .actual = nullptr };
}

TypeError TypeError::fieldMismatch(std::string const& expected, std::string const& actual)
{
    return TypeError { .kind = Kind::FieldMismatch,
                       .message = std::format("Field mismatch: expected '{}', got '{}'", expected, actual),
                       .expected = nullptr,
                       .actual = nullptr };
}

TypeError TypeError::caseMismatch(std::string const& expected, std::string const& actual)
{
    return TypeError { .kind = Kind::CaseMismatch,
                       .message = std::format("Case mismatch: expected '{}', got '{}'", expected, actual),
                       .expected = nullptr,
                       .actual = nullptr };
}

TypeError TypeError::unboundTypeVar(TypeVarId varId)
{
    auto const letter = static_cast<char>('a' + (varId % 26));
    return TypeError { .kind = Kind::UnboundTypeVar,
                       .message = std::format("Unbound type variable: {}", letter),
                       .expected = types::typeVar(varId),
                       .actual = nullptr };
}

// Unification implementation

bool occursIn(TypeVarId varId, TypePtr const& type)
{
    return foldType<bool>(type, false, [varId](bool found, TypePtr const& t) {
        return found || (t->isTypeVar() && t->asTypeVar()->id == varId);
    });
}

std::vector<TypeVarId> collectTypeVars(TypePtr const& type)
{
    return foldType<std::vector<TypeVarId>>(type, {}, [](std::vector<TypeVarId> acc, TypePtr const& t) {
        if (auto const* tv = t->asTypeVar())
        {
            if (std::ranges::find(acc, tv->id) == acc.end())
                acc.push_back(tv->id);
        }
        return acc;
    });
}

// Helper for unifying type variables
static UnifyResult unifyVar(TypeVarId varId, TypePtr const& type)
{
    // If the type is the same type variable, trivially unifies
    if (auto* tv = type->asTypeVar())
    {
        if (tv->id == varId)
            return Substitution {}; // Empty substitution
    }

    // Occurs check: prevent infinite types
    if (occursIn(varId, type))
        return std::unexpected(TypeError::occursCheck(varId, type));

    // Return singleton substitution
    return Substitution::single(varId, type);
}

UnifyResult unify(TypePtr const& t1, TypePtr const& t2)
{
    // If both are type variables
    if (auto* tv1 = t1->asTypeVar())
    {
        return unifyVar(tv1->id, t2);
    }
    if (auto* tv2 = t2->asTypeVar())
    {
        return unifyVar(tv2->id, t1);
    }

    // Both are primitives
    if (const auto* p1 = t1->asPrimitive())
    {
        if (const auto* p2 = t2->asPrimitive())
        {
            if (p1->kind == p2->kind)
                return Substitution {};
        }
        return std::unexpected(TypeError::mismatch(t1, t2));
    }

    // Both are functions
    if (auto* fn1 = t1->asFunction())
    {
        if (auto* fn2 = t2->asFunction())
        {
            // Unify parameter types
            auto paramResult = unify(fn1->paramType, fn2->paramType);
            if (!paramResult)
                return paramResult;

            // Apply substitution and unify return types
            auto returnResult =
                unify(paramResult->apply(fn1->returnType), paramResult->apply(fn2->returnType));
            if (!returnResult)
                return returnResult;

            // Compose substitutions
            return returnResult->compose(*paramResult);
        }
        return std::unexpected(TypeError::mismatch(t1, t2));
    }

    // Both are lists
    if (auto* lst1 = t1->asList())
    {
        if (auto* lst2 = t2->asList())
        {
            return unify(lst1->elementType, lst2->elementType);
        }
        return std::unexpected(TypeError::mismatch(t1, t2));
    }

    // Both are tuples
    if (auto* tup1 = t1->asTuple())
    {
        if (auto* tup2 = t2->asTuple())
        {
            if (tup1->elementTypes.size() != tup2->elementTypes.size())
                return std::unexpected(
                    TypeError::arityMismatch(tup1->elementTypes.size(), tup2->elementTypes.size()));

            Substitution result;
            for (size_t i = 0; i < tup1->elementTypes.size(); ++i)
            {
                auto elemResult =
                    unify(result.apply(tup1->elementTypes[i]), result.apply(tup2->elementTypes[i]));
                if (!elemResult)
                    return elemResult;
                result = elemResult->compose(result);
            }
            return result;
        }
        return std::unexpected(TypeError::mismatch(t1, t2));
    }

    // Both are options
    if (auto* opt1 = t1->asOption())
    {
        if (auto* opt2 = t2->asOption())
        {
            return unify(opt1->innerType, opt2->innerType);
        }
        return std::unexpected(TypeError::mismatch(t1, t2));
    }

    // Both are refs
    if (auto* ref1 = t1->asRef())
    {
        if (auto* ref2 = t2->asRef())
        {
            return unify(ref1->innerType, ref2->innerType);
        }
        return std::unexpected(TypeError::mismatch(t1, t2));
    }

    // Both are results
    if (auto* res1 = t1->asResult())
    {
        if (auto* res2 = t2->asResult())
        {
            auto okResult = unify(res1->okType, res2->okType);
            if (!okResult)
                return okResult;

            auto errResult = unify(okResult->apply(res1->errorType), okResult->apply(res2->errorType));
            if (!errResult)
                return errResult;

            return errResult->compose(*okResult);
        }
        return std::unexpected(TypeError::mismatch(t1, t2));
    }

    // Both are type applications
    if (auto* app1 = t1->asTypeApp())
    {
        if (auto* app2 = t2->asTypeApp())
        {
            if (app1->name != app2->name)
                return std::unexpected(TypeError::mismatch(t1, t2));

            if (app1->args.size() != app2->args.size())
                return std::unexpected(TypeError::arityMismatch(app1->args.size(), app2->args.size()));

            Substitution result;
            for (size_t i = 0; i < app1->args.size(); ++i)
            {
                auto argResult = unify(result.apply(app1->args[i]), result.apply(app2->args[i]));
                if (!argResult)
                    return argResult;
                result = argResult->compose(result);
            }
            return result;
        }
        // TypeApp vs UnionType/RecordType: nominal match by name
        if (auto* un2 = t2->asUnion())
        {
            if (app1->name == un2->name)
                return Substitution {};
        }
        if (auto* rec2 = t2->asRecord())
        {
            if (app1->name == rec2->name)
                return Substitution {};
        }
        return std::unexpected(TypeError::mismatch(t1, t2));
    }

    // Both are records
    if (auto* rec1 = t1->asRecord())
    {
        if (auto* rec2 = t2->asRecord())
        {
            // Named records must have matching names (or both be anonymous)
            if (rec1->name != rec2->name)
                return std::unexpected(TypeError::mismatch(t1, t2));

            if (rec1->fields.size() != rec2->fields.size())
                return std::unexpected(TypeError::arityMismatch(rec1->fields.size(), rec2->fields.size()));

            Substitution result;
            for (size_t i = 0; i < rec1->fields.size(); ++i)
            {
                if (rec1->fields[i].name != rec2->fields[i].name)
                    return std::unexpected(
                        TypeError::fieldMismatch(rec1->fields[i].name, rec2->fields[i].name));

                auto fieldResult =
                    unify(result.apply(rec1->fields[i].type), result.apply(rec2->fields[i].type));
                if (!fieldResult)
                    return fieldResult;
                result = fieldResult->compose(result);
            }
            return result;
        }
        // RecordType vs TypeApp: nominal match by name
        if (auto* app2 = t2->asTypeApp())
        {
            if (rec1->name == app2->name)
                return Substitution {};
        }
        return std::unexpected(TypeError::mismatch(t1, t2));
    }

    // Both are unions
    if (auto* un1 = t1->asUnion())
    {
        if (auto* un2 = t2->asUnion())
        {
            if (un1->name != un2->name)
                return std::unexpected(TypeError::mismatch(t1, t2));

            if (un1->cases.size() != un2->cases.size())
                return std::unexpected(TypeError::arityMismatch(un1->cases.size(), un2->cases.size()));

            Substitution result;
            for (size_t i = 0; i < un1->cases.size(); ++i)
            {
                if (un1->cases[i].name != un2->cases[i].name)
                    return std::unexpected(TypeError::caseMismatch(un1->cases[i].name, un2->cases[i].name));

                if (un1->cases[i].payloadType.has_value() != un2->cases[i].payloadType.has_value())
                    return std::unexpected(TypeError::mismatch(t1, t2));

                if (un1->cases[i].payloadType)
                {
                    auto caseResult = unify(result.apply(*un1->cases[i].payloadType),
                                            result.apply(*un2->cases[i].payloadType));
                    if (!caseResult)
                        return caseResult;
                    result = caseResult->compose(result);
                }
            }
            return result;
        }
        // UnionType vs TypeApp: nominal match by name
        if (auto* app2 = t2->asTypeApp())
        {
            if (un1->name == app2->name)
                return Substitution {};
        }
        return std::unexpected(TypeError::mismatch(t1, t2));
    }

    // Different type constructors
    return std::unexpected(TypeError::mismatch(t1, t2));
}

UnifyResult unifyWithSubst(TypePtr const& t1, TypePtr const& t2, Substitution const& subst)
{
    auto result = unify(subst.apply(t1), subst.apply(t2));
    if (result)
        return result->compose(subst);
    return result;
}

bool typesEqual(TypePtr const& t1, TypePtr const& t2, Substitution const& subst)
{
    auto applied1 = subst.apply(t1);
    auto applied2 = subst.apply(t2);
    return *applied1 == *applied2;
}

} // namespace endo
