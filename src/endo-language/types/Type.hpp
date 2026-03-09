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
                 UnionType>
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

    // Mutable accessors
    TypeVar* asTypeVar() { return std::get_if<TypeVar>(&node); }

    FunctionType* asFunction() { return std::get_if<FunctionType>(&node); }

    ListType* asList() { return std::get_if<ListType>(&node); }

    TupleType* asTuple() { return std::get_if<TupleType>(&node); }

    OptionType* asOption() { return std::get_if<OptionType>(&node); }

    ResultType* asResult() { return std::get_if<ResultType>(&node); }

    RecordType* asRecord() { return std::get_if<RecordType>(&node); }

    UnionType* asUnion() { return std::get_if<UnionType>(&node); }
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

    // Type scheme (for polymorphism)
    TypeScheme scheme(std::vector<TypeVarId> quantified, TypePtr type);
    TypeScheme monomorphic(TypePtr type); // No quantified variables

} // namespace types

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
