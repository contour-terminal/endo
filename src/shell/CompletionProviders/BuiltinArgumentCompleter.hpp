// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <shell/CompletionProvider.hpp>

#include <vector>

namespace endo
{

/// @brief Completion provider for builtin command arguments with enumerated values.
///
/// Handles argument completion for builtins like `set_prompt_preset`, `set_prompt_layout`,
/// etc. that accept a known set of string values. Delegates to `builtinArgumentCandidates()`
/// from the shared completion engine.
class BuiltinArgumentCompleter: public CompletionProvider
{
  public:
    [[nodiscard]] std::vector<CompletionItem> complete(CompletionContext const& context) override;
    [[nodiscard]] bool canHandle(CompletionContextType type) const override;

    [[nodiscard]] int priority() const override { return 92; }
};

} // namespace endo
