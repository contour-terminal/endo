// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <expected>
#include <string>
#include <unordered_map>
#include <vector>

#include "Type.hpp"

namespace endo
{

/// Substitution: mapping from type variable IDs to types.
///
/// Used during unification to track the solved type variables.
/// Supports composition and application to types.
class Substitution
{
  public:
    Substitution() = default;

    /// Create a singleton substitution: varId -> type
    static Substitution single(TypeVarId varId, TypePtr type);

    /// Check if a type variable is in the substitution
    bool contains(TypeVarId varId) const;

    /// Look up a type variable
    std::optional<TypePtr> lookup(TypeVarId varId) const;

    /// Add a mapping (overwrites if exists)
    void add(TypeVarId varId, TypePtr type);

    /// Apply substitution to a type, returning a new type
    TypePtr apply(TypePtr const& type) const;

    /// Apply substitution to a type scheme
    TypeScheme apply(TypeScheme const& scheme) const;

    /// Compose two substitutions: (s1 compose s2) means apply s2 first, then s1
    /// Result: s1(s2(t)) for any type t
    Substitution compose(Substitution const& other) const;

    /// Get all mappings
    std::unordered_map<TypeVarId, TypePtr> const& mappings() const { return _mappings; }

    /// Check if empty
    bool empty() const { return _mappings.empty(); }

    /// Pretty-print the substitution
    std::string toString() const;

  private:
    std::unordered_map<TypeVarId, TypePtr> _mappings;
};

/// Type error for unification failures
struct TypeError
{
    enum class Kind
    {
        Mismatch,       // Types don't match (e.g., int vs str)
        OccursCheck,    // Infinite type (type variable occurs in its own solution)
        ArityMismatch,  // Different number of elements (tuples, records)
        FieldMismatch,  // Record field name mismatch
        CaseMismatch,   // Union case mismatch
        UnboundTypeVar, // Reference to unbound type variable
    };

    Kind kind;
    std::string message;
    TypePtr expected; // The type we expected
    TypePtr actual;   // The type we got

    static TypeError mismatch(TypePtr expected, TypePtr actual);
    static TypeError occursCheck(TypeVarId varId, TypePtr type);
    static TypeError arityMismatch(size_t expected, size_t actual);
    static TypeError fieldMismatch(std::string const& expected, std::string const& actual);
    static TypeError caseMismatch(std::string const& expected, std::string const& actual);
    static TypeError unboundTypeVar(TypeVarId varId);
};

/// Result of unification: either a substitution or a type error
using UnifyResult = std::expected<Substitution, TypeError>;

/// Unify two types, returning a substitution that makes them equal.
///
/// The unification algorithm finds a substitution S such that S(t1) = S(t2).
///
/// Examples:
/// - unify(int, int) = {} (empty substitution)
/// - unify(a, int) = {a -> int}
/// - unify(a -> b, int -> str) = {a -> int, b -> str}
/// - unify(int, str) = Error (type mismatch)
/// - unify(a, list<a>) = Error (occurs check - infinite type)
UnifyResult unify(TypePtr const& t1, TypePtr const& t2);

/// Unify two types with an existing substitution.
/// Applies the substitution to both types before unifying.
UnifyResult unifyWithSubst(TypePtr const& t1, TypePtr const& t2, Substitution const& subst);

/// Check if a type variable occurs in a type (for occurs check)
bool occursIn(TypeVarId varId, TypePtr const& type);

/// Collect all type variables in a type
std::vector<TypeVarId> collectTypeVars(TypePtr const& type);

/// Check if two types are equal (after applying substitution)
bool typesEqual(TypePtr const& t1, TypePtr const& t2, Substitution const& subst = {});

} // namespace endo
