// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <endo-language/sema/BuiltinDescriptors.hpp>
#include <endo-language/sema/ScopeManager.hpp>
#include <endo-language/sema/TypeRegistry.hpp>

#include <optional>
#include <string>

namespace endo
{

/// Facade composing the semantic analysis components used by IRGenerator.
///
/// Owns the TypeDefinitionRegistry, BuiltinDescriptorRegistry, and ScopeManager
/// that were extracted from IRGenerator. This establishes a clean boundary between
/// semantic analysis (name resolution, type tracking, scope management, validation)
/// and IR emission.
class SemanticAnalyzer
{
  public:
    SemanticAnalyzer();

    /// Access the type definition registry (record types, union types, constructors).
    [[nodiscard]] TypeDefinitionRegistry& types() noexcept { return _types; }

    [[nodiscard]] TypeDefinitionRegistry const& types() const noexcept { return _types; }

    /// Access the builtin descriptor registry (function and property metadata).
    [[nodiscard]] BuiltinDescriptorRegistry const& builtins() const noexcept { return _builtins; }

    /// Access the variable scope manager.
    [[nodiscard]] ScopeManager& scopes() noexcept { return _scopes; }

    [[nodiscard]] ScopeManager const& scopes() const noexcept { return _scopes; }

    /// Validates a mutation assignment target name.
    /// Detects `r.value <- x` where `r` is a ref cell and returns an error message
    /// suggesting `r <- x` instead. Returns std::nullopt if no issue detected.
    [[nodiscard]] std::optional<std::string> validateMutAssignTarget(std::string const& name) const;

  private:
    TypeDefinitionRegistry _types;
    BuiltinDescriptorRegistry _builtins;
    ScopeManager _scopes;
};

} // namespace endo
