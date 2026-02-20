// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <shell/CompletionProvider.hpp>

#include <endo-language/codegen/IRGenerator.hpp>

#include <string>
#include <vector>

namespace endo
{

/// @brief Completion provider for F# let bindings (functions and values) persisted across REPL prompts.
class LetBindingCompleter: public CompletionProvider
{
  public:
    /// @brief Constructs a let binding completer with access to persisted F# state.
    /// @param state The persistent F# state containing function and value definitions.
    explicit LetBindingCompleter(FSharpPersistentState const& state);

    [[nodiscard]] std::vector<CompletionItem> complete(CompletionContext const& context) override;
    [[nodiscard]] bool canHandle(CompletionContextType type) const override;

    [[nodiscard]] int priority() const override { return 90; }

    /// @brief Formats a human-readable description for a persisted function.
    /// @param name The function name.
    /// @param func The persisted function metadata.
    /// @return Description string like "fn(x, y)" or "rec fn(n: int) -> int".
    [[nodiscard]] static std::string formatFunctionDescription(
        std::string const& name, FSharpPersistentState::PersistedFunction const& func);

    /// @brief Formats a human-readable description for a persisted value binding.
    /// @param binding The persisted value binding metadata.
    /// @return Description string like "value" or "mutable value".
    [[nodiscard]] static std::string formatValueDescription(
        FSharpPersistentState::PersistedValueBinding const& binding);

  private:
    FSharpPersistentState const& _state;
};

} // namespace endo
