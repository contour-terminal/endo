// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <shell/completion/CompletionProvider.hpp>

#include <core/platform/ProcessEnvironment.hpp>

#include <string>
#include <vector>

namespace endo
{

/// @brief Completion provider for environment and special variables.
class VariableCompleter: public CompletionProvider
{
  public:
    /// @brief Constructs a variable completer with access to environment.
    /// @param env The environment to query for variable names.
    explicit VariableCompleter(core::platform::ProcessEnvironment const& env);

    [[nodiscard]] std::vector<CompletionItem> complete(CompletionContext const& context) override;
    [[nodiscard]] bool canHandle(CompletionContextType type) const override;

    [[nodiscard]] int priority() const override { return 80; }

  private:
    core::platform::ProcessEnvironment const& _env;

    /// @brief Returns list of special shell variables.
    [[nodiscard]] static std::vector<CompletionItem> specialVariables();
};

} // namespace endo
