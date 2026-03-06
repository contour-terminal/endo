// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <endo-language/types/Type.hpp>

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace endo
{

// Forward declaration
class TypeEnv;
using TypeEnvPtr = std::shared_ptr<TypeEnv>;

/// Type environment: maps variable names to their type schemes.
///
/// Implements lexical scoping through a parent chain. Each scope
/// (function body, let block, etc.) creates a new TypeEnv with the
/// enclosing scope as its parent.
///
/// For Hindley-Milner type inference:
/// - Variables are bound to TypeScheme (possibly polymorphic)
/// - Type variable generator provides fresh type variables
/// - Generalization creates polymorphic types from monomorphic ones
class TypeEnv: public std::enable_shared_from_this<TypeEnv>
{
  public:
    /// Create a new top-level environment
    TypeEnv();

    /// Create a child environment with the given parent
    explicit TypeEnv(TypeEnvPtr parent);

    /// Look up a variable's type scheme in this scope or any parent
    std::optional<TypeScheme> lookup(std::string const& name) const;

    /// Bind a variable to a type scheme in the current scope
    void bind(std::string const& name, TypeScheme scheme);

    /// Bind a variable to a monomorphic type (convenience method)
    void bindMono(std::string const& name, TypePtr type);

    /// Check if a variable is defined in the current scope (not parents)
    bool isDefinedLocally(std::string const& name) const;

    /// Check if a variable is defined in this scope or any parent
    bool isDefined(std::string const& name) const;

    /// Generate a fresh type variable ID
    TypeVarId freshTypeVar();

    /// Generate a fresh type variable as a Type
    TypePtr freshTypeVarType();

    /// Generalize a type to a type scheme.
    /// Type variables that are free in the type but not in the environment
    /// become universally quantified.
    TypeScheme generalize(TypePtr const& type) const;

    /// Instantiate a type scheme with fresh type variables.
    /// Creates a new monomorphic type by replacing all quantified
    /// type variables with fresh ones.
    TypePtr instantiate(TypeScheme const& scheme);

    /// Get all free type variables in the environment
    std::vector<TypeVarId> freeTypeVars() const;

    /// Get all bindings in the current scope (not parents)
    std::unordered_map<std::string, TypeScheme> const& localBindings() const;

    /// Create a child scope
    TypeEnvPtr childScope() const;

    /// Get parent environment (or nullptr if top-level)
    TypeEnvPtr parent() const { return _parent; }

  private:
    TypeEnvPtr _parent;
    std::unordered_map<std::string, TypeScheme> _bindings;
    TypeVarId _nextTypeVarId = 0;

    /// Collect free type variables in a type
    static void collectFreeVars(TypePtr const& type, std::vector<TypeVarId>& vars);

    /// Collect free type variables in a type scheme (excluding quantified ones)
    static void collectFreeVars(TypeScheme const& scheme, std::vector<TypeVarId>& vars);
};

/// Type definitions registry: maps type names to their definitions.
///
/// Separate from TypeEnv because type definitions have different
/// scoping rules (typically module-level, not block-level).
class TypeRegistry
{
  public:
    TypeRegistry();

    /// Register a record type definition
    void registerRecord(std::string const& name, RecordType record);

    /// Register a union type definition
    void registerUnion(std::string const& name, UnionType unionDef);

    /// Look up a record type by name
    std::optional<RecordType> lookupRecord(std::string const& name) const;

    /// Look up a union type by name
    std::optional<UnionType> lookupUnion(std::string const& name) const;

    /// Look up any type definition by name (record or union)
    std::optional<TypePtr> lookupType(std::string const& name) const;

    /// Check if a type name is registered
    bool isRegistered(std::string const& name) const;

    /// Register built-in types (Option, Result, Error, etc.)
    void registerBuiltins();

  private:
    std::unordered_map<std::string, RecordType> _records;
    std::unordered_map<std::string, UnionType> _unions;
};

/// Create a standard type environment with built-in function types.
///
/// Includes types for:
/// - Arithmetic: (+), (-), (*), (/), (%)
/// - Comparison: (==), (!=), (<), (<=), (>), (>=)
/// - List operations: head, tail, length, map, filter, fold, etc.
/// - Option/Result: Some, None, Ok, Error, isOk, isNone, etc.
TypeEnvPtr createStandardTypeEnv();

/// Create a standard type registry with built-in types.
std::shared_ptr<TypeRegistry> createStandardTypeRegistry();

} // namespace endo
