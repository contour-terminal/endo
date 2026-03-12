// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <shell/completion/CompletionProvider.hpp>

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

  private:
    FSharpPersistentState const& _state;
};

} // namespace endo
