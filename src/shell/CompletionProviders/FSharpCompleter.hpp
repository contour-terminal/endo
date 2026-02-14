// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <shell/CompletionProvider.hpp>

#include <endo-language/IRGenerator.hpp>

#include <string>
#include <vector>

namespace endo
{

/// @brief Completion provider for F# dot-access patterns.
///
/// Handles three completion scenarios:
/// - Module access: `Option.map`, `Option.bind`, `Option.defaultValue`
/// - Underscore field access: `_.pid`, `_.user` (record field placeholders)
/// - Value method/field access: `myVar.map`, `myVar.pid` (both Option methods and record fields)
class FSharpCompleter: public CompletionProvider
{
  public:
    /// @brief Constructs an F# completer with access to persisted F# state.
    /// @param state The persistent F# state containing record type metadata.
    explicit FSharpCompleter(FSharpPersistentState const& state);

    [[nodiscard]] std::vector<CompletionItem> complete(CompletionContext const& context) override;
    [[nodiscard]] bool canHandle(CompletionContextType type) const override;

    [[nodiscard]] int priority() const override { return 95; }

  private:
    FSharpPersistentState const& _state;

    /// @brief Generates completions for dot-access patterns (object.member).
    /// @param objectPart The part before the last dot (e.g., "Option", "_", "myVar").
    /// @param memberPrefix The part after the last dot (may be empty).
    /// @param fullPrefix The full prefix string for matching.
    /// @return List of completion items.
    [[nodiscard]] std::vector<CompletionItem> completeDotAccess(std::string const& objectPart,
                                                                std::string const& memberPrefix,
                                                                std::string const& fullPrefix) const;
};

} // namespace endo
