// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <endo-language/ast/AST.hpp>
#include <endo-language/ast/Pattern.hpp>
#include <endo-language/types/Type.hpp>
#include <endo-language/types/TypeEnv.hpp>
#include <endo-language/types/Unification.hpp>

#include <expected>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace endo
{

/// Inferred type information for a single function.
struct InferredFunctionType
{
    std::vector<TypePtr> paramTypes;   ///< Inferred types for each parameter
    std::optional<TypePtr> returnType; ///< Inferred return type
};

/// Result of running type inference on a program.
struct InferenceResult
{
    std::unordered_map<std::string, InferredFunctionType> functions; ///< Function name → inferred types
    std::vector<std::string> errors;                                 ///< Type errors encountered

    /// Check if inference succeeded without errors.
    [[nodiscard]] bool hasErrors() const noexcept { return !errors.empty(); }
};

/// Hindley-Milner type inferencer for the Endo language.
///
/// Runs as a separate pre-pass before IR generation, walking the AST
/// using Algorithm W. Produces an InferenceResult mapping function names
/// to their inferred parameter and return types.
class TypeInferencer
{
  public:
    /// Construct a type inferencer with the given base environment.
    explicit TypeInferencer(TypeEnvPtr env);

    /// Infer types for all top-level definitions in a statement.
    [[nodiscard]] InferenceResult inferProgram(ast::Statement const& root);

  private:
    using InferResult = std::expected<std::pair<TypePtr, Substitution>, std::string>;

    /// Infer the type of an expression under the given environment and substitution.
    [[nodiscard]] InferResult inferExpr(ast::Expr const& expr, TypeEnvPtr const& env, Substitution subst);

    /// Infer types from a statement, updating the environment.
    /// Returns the updated substitution or an error.
    [[nodiscard]] std::expected<Substitution, std::string> inferStmt(ast::Statement const& stmt,
                                                                     TypeEnvPtr const& env,
                                                                     Substitution subst);

    /// Infer the type constrained by a pattern, returning bindings to add to the environment.
    struct PatternResult
    {
        TypePtr type;                                          ///< Type that the pattern matches
        std::vector<std::pair<std::string, TypePtr>> bindings; ///< Variable bindings introduced
        Substitution subst;                                    ///< Updated substitution
    };

    [[nodiscard]] std::expected<PatternResult, std::string> inferPattern(pattern::Pattern const& pat,
                                                                         TypeEnvPtr const& env,
                                                                         Substitution subst);

    /// Get the type of a binary operator given its operands.
    [[nodiscard]] InferResult inferBinaryOp(ast::BinaryOp op,
                                            ast::Expr const& left,
                                            ast::Expr const& right,
                                            TypeEnvPtr const& env,
                                            Substitution subst);

    /// Check if an expression contains float literals (for operator overload resolution).
    [[nodiscard]] static bool containsFloatLiteral(ast::Expr const& expr);

    /// Check if an expression contains string literals (for + overload resolution).
    [[nodiscard]] static bool containsStringLiteral(ast::Expr const& expr);

    /// Record an inferred function type in the result.
    void recordFunction(std::string const& name, InferredFunctionType type);

    /// Record a type error.
    void recordError(std::string error);

    /// Helper: unify two types and compose with existing substitution.
    [[nodiscard]] std::expected<Substitution, std::string> unifyAndCompose(TypePtr const& t1,
                                                                           TypePtr const& t2,
                                                                           Substitution const& subst);

    TypeEnvPtr _env;         ///< Root type environment
    InferenceResult _result; ///< Accumulated inference results
};

} // namespace endo
