// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace endo
{

// Forward declarations
struct Type;
using TypePtr = std::shared_ptr<Type>;

// Type variable identifier for polymorphism
using TypeVarId = uint32_t;

// Primitive type kinds
enum class PrimitiveType // NOLINT(performance-enum-size)
{
    Int,   // 64-bit signed integer
    Float, // 64-bit floating point
    Str,   // UTF-8 string
    Bool,  // Boolean
    Unit,  // No value (like void)
};

// A type variable (for polymorphism)
struct TypeVar
{
    TypeVarId id;

    bool operator==(TypeVar const& other) const { return id == other.id; }
};

// Primitive type
struct PrimitiveTypeNode
{
    PrimitiveType kind;

    bool operator==(PrimitiveTypeNode const& other) const { return kind == other.kind; }
};

// Function type: T1 -> T2 (curried, so multi-arg is T1 -> T2 -> T3)
struct FunctionType
{
    TypePtr paramType;
    TypePtr returnType;

    bool operator==(FunctionType const& other) const;
};

// List type: list<T>
struct ListType
{
    TypePtr elementType;

    bool operator==(ListType const& other) const;
};

// Tuple type: (T1, T2, ..., Tn)
struct TupleType
{
    std::vector<TypePtr> elementTypes;

    bool operator==(TupleType const& other) const;
};

// Option type: option<T> = Some T | None
struct OptionType
{
    TypePtr innerType;

    bool operator==(OptionType const& other) const;
};

// Result type: result<T, E> = Ok T | Error E
struct ResultType
{
    TypePtr okType;
    TypePtr errorType;

    bool operator==(ResultType const& other) const;
};

// Record field
struct RecordField
{
    std::string name;
    TypePtr type;

    bool operator==(RecordField const& other) const;
};

// Record type: { field1: T1, field2: T2, ... }
struct RecordType
{
    std::string name; // Optional type name (empty for anonymous records)
    std::vector<RecordField> fields;

    bool operator==(RecordType const& other) const;

    // Find a field by name
    [[nodiscard]] std::optional<TypePtr> fieldType(std::string const& fieldName) const;
};

// Union case (variant)
struct UnionCase
{
    std::string name;
    std::optional<TypePtr> payloadType; // None for cases without data (e.g., Point)

    bool operator==(UnionCase const& other) const;
};

// Discriminated union type: type Shape = Circle of float | Rectangle of (float, float) | Point
struct UnionType
{
    std::string name;
    std::vector<UnionCase> cases;

    bool operator==(UnionType const& other) const;

    // Find a case by name
    [[nodiscard]] std::optional<UnionCase const*> findCase(std::string const& caseName) const;
};

/// Type application: a named generic type applied to concrete type arguments.
/// E.g., Tree<int>, Pair<string, bool>. Avoids infinite recursion for recursive generics.
struct TypeApp
{
    std::string name;
    std::vector<TypePtr> args;

    bool operator==(TypeApp const& other) const;
};

// Type scheme for let-polymorphism: forall a b c. T
// Represents a polymorphic type with quantified type variables
struct TypeScheme
{
    std::vector<TypeVarId> quantifiedVars; // Universally quantified type variables
    TypePtr type;                          // The underlying type

    // Instantiate the scheme with fresh type variables
    TypePtr instantiate(std::function<TypeVarId()> const& freshVarGen) const;

    bool operator==(TypeScheme const& other) const;
};

// The main Type variant
struct Type
{
    std::variant<TypeVar,
                 PrimitiveTypeNode,
                 FunctionType,
                 ListType,
                 TupleType,
                 OptionType,
                 ResultType,
                 RecordType,
                 UnionType,
                 TypeApp>
        node;

    bool operator==(Type const& other) const { return node == other.node; }

    // Type predicates
    [[nodiscard]] bool isTypeVar() const { return std::holds_alternative<TypeVar>(node); }

    [[nodiscard]] bool isPrimitive() const { return std::holds_alternative<PrimitiveTypeNode>(node); }

    [[nodiscard]] bool isFunction() const { return std::holds_alternative<FunctionType>(node); }

    [[nodiscard]] bool isList() const { return std::holds_alternative<ListType>(node); }

    [[nodiscard]] bool isTuple() const { return std::holds_alternative<TupleType>(node); }

    [[nodiscard]] bool isOption() const { return std::holds_alternative<OptionType>(node); }

    [[nodiscard]] bool isResult() const { return std::holds_alternative<ResultType>(node); }

    [[nodiscard]] bool isRecord() const { return std::holds_alternative<RecordType>(node); }

    [[nodiscard]] bool isUnion() const { return std::holds_alternative<UnionType>(node); }

    [[nodiscard]] bool isTypeApp() const { return std::holds_alternative<TypeApp>(node); }

    // Accessors (return nullptr if wrong type)
    [[nodiscard]] TypeVar const* asTypeVar() const { return std::get_if<TypeVar>(&node); }

    [[nodiscard]] PrimitiveTypeNode const* asPrimitive() const
    {
        return std::get_if<PrimitiveTypeNode>(&node);
    }

    [[nodiscard]] FunctionType const* asFunction() const { return std::get_if<FunctionType>(&node); }

    [[nodiscard]] ListType const* asList() const { return std::get_if<ListType>(&node); }

    [[nodiscard]] TupleType const* asTuple() const { return std::get_if<TupleType>(&node); }

    [[nodiscard]] OptionType const* asOption() const { return std::get_if<OptionType>(&node); }

    [[nodiscard]] ResultType const* asResult() const { return std::get_if<ResultType>(&node); }

    [[nodiscard]] RecordType const* asRecord() const { return std::get_if<RecordType>(&node); }

    [[nodiscard]] UnionType const* asUnion() const { return std::get_if<UnionType>(&node); }

    [[nodiscard]] TypeApp const* asTypeApp() const { return std::get_if<TypeApp>(&node); }

    // Mutable accessors
    TypeVar* asTypeVar() { return std::get_if<TypeVar>(&node); }

    FunctionType* asFunction() { return std::get_if<FunctionType>(&node); }

    ListType* asList() { return std::get_if<ListType>(&node); }

    TupleType* asTuple() { return std::get_if<TupleType>(&node); }

    OptionType* asOption() { return std::get_if<OptionType>(&node); }

    ResultType* asResult() { return std::get_if<ResultType>(&node); }

    RecordType* asRecord() { return std::get_if<RecordType>(&node); }

    UnionType* asUnion() { return std::get_if<UnionType>(&node); }

    TypeApp* asTypeApp() { return std::get_if<TypeApp>(&node); }
};

// Factory functions for creating types
namespace types
{
    // Primitives
    TypePtr intType();
    TypePtr floatType();
    TypePtr strType();
    TypePtr boolType();
    TypePtr unitType();

    // Type variable
    TypePtr typeVar(TypeVarId id);

    // Function type: param -> return
    TypePtr function(TypePtr param, TypePtr ret);

    // Multi-argument function (curried): a -> b -> c -> result
    TypePtr function(std::vector<TypePtr> const& params, TypePtr ret);

    // List type: list<element>
    TypePtr list(TypePtr element);

    // Tuple type: (T1, T2, ...)
    TypePtr tuple(std::vector<TypePtr> elements);

    // Option type: option<T>
    TypePtr option(TypePtr inner);

    // Result type: result<T, E>
    TypePtr result(TypePtr ok, TypePtr error);

    // Record type: { field1: T1, field2: T2, ... }
    TypePtr record(std::string name, std::vector<RecordField> fields);
    TypePtr anonymousRecord(std::vector<RecordField> fields);

    // Union type
    TypePtr unionType(std::string name, std::vector<UnionCase> cases);

    // Type application (named generic type with arguments)
    TypePtr typeApp(std::string name, std::vector<TypePtr> args);

    // Type scheme (for polymorphism)
    TypeScheme scheme(std::vector<TypeVarId> quantified, TypePtr type);
    TypeScheme monomorphic(TypePtr type); // No quantified variables

} // namespace types

/// Recursively transforms a type tree. The visitor is called for each node;
/// if it returns a non-null TypePtr, that replaces the subtree (no further recursion).
/// Otherwise the node is reconstructed with transformed children.
/// @param type    The type tree to transform.
/// @param visitor Function called on each node. Returns replacement or nullptr to recurse.
/// @return The transformed type tree.
TypePtr transformType(TypePtr const& type, std::function<TypePtr(TypePtr const&)> const& visitor);

/// Folds over all nodes in a type tree, accumulating a result.
/// The fold function is called on the current node first, then children are recursed into.
/// @param type The type tree to fold over.
/// @param init Initial accumulator value.
/// @param f    Fold function: (accumulator, current node) -> new accumulator.
/// @return The final accumulator value.
template <typename Acc, typename F>
Acc foldType(TypePtr const& type, Acc init, F const& f)
{
    auto acc = f(std::move(init), type);

    if (auto const* fn = type->asFunction())
    {
        acc = foldType<Acc>(fn->paramType, std::move(acc), f);
        acc = foldType<Acc>(fn->returnType, std::move(acc), f);
    }
    else if (auto const* lst = type->asList())
    {
        acc = foldType<Acc>(lst->elementType, std::move(acc), f);
    }
    else if (auto const* tup = type->asTuple())
    {
        for (auto const& elem: tup->elementTypes)
            acc = foldType<Acc>(elem, std::move(acc), f);
    }
    else if (auto const* opt = type->asOption())
    {
        acc = foldType<Acc>(opt->innerType, std::move(acc), f);
    }
    else if (auto const* res = type->asResult())
    {
        acc = foldType<Acc>(res->okType, std::move(acc), f);
        acc = foldType<Acc>(res->errorType, std::move(acc), f);
    }
    else if (auto const* rec = type->asRecord())
    {
        for (auto const& field: rec->fields)
            acc = foldType<Acc>(field.type, std::move(acc), f);
    }
    else if (auto const* un = type->asUnion())
    {
        for (auto const& c: un->cases)
            if (c.payloadType)
                acc = foldType<Acc>(*c.payloadType, std::move(acc), f);
    }
    else if (auto const* app = type->asTypeApp())
    {
        for (auto const& arg: app->args)
            acc = foldType<Acc>(arg, std::move(acc), f);
    }

    return acc;
}

/// Map from TypeVarId to display name (e.g., for parser-assigned type variables).
using TypeVarNameMap = std::unordered_map<TypeVarId, std::string>;

// Pretty-printing
std::string toString(Type const& type);
std::string toString(TypePtr const& type);
std::string toString(PrimitiveType prim);
std::string toString(TypeScheme const& scheme);

/// Pretty-print a type, using the given name overrides for type variables.
std::string toString(Type const& type, TypeVarNameMap const& nameMap);
std::string toString(TypePtr const& type, TypeVarNameMap const& nameMap);

} // namespace endo
