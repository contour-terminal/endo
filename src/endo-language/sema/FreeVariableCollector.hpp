// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <endo-language/ast/AST.hpp>

#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

namespace endo
{

/// Queries whether a given name is accessible in the current variable scope.
using ScopeQuery = std::function<bool(std::string const&)>;

/// Queries whether a given name is a known function definition.
using FunctionQuery = std::function<bool(std::string const&)>;

/// Collects free variable names referenced in @p body that are not in @p boundNames,
/// are not known function definitions, and are accessible in the variable scope.
///
/// This is a pure AST analysis — it does not depend on IR generation state.
/// The caller is responsible for mapping returned names to their storage (e.g., CoreVM::Value*).
///
/// @param body         The expression body to analyze.
/// @param boundNames   Names already bound (e.g., function parameters).
/// @param isInScope    Returns true if a name is accessible in the current variable scope.
/// @param isKnownFunction Returns true if a name is a registered function definition.
/// @return Set of free variable names found in @p body.
[[nodiscard]] std::unordered_set<std::string> collectFreeVariableNames(
    ast::Expr const* body,
    std::vector<std::string> const& boundNames,
    ScopeQuery const& isInScope,
    FunctionQuery const& isKnownFunction);

} // namespace endo
