// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <shell/completion/CompletionProvider.hpp>

#include <string>
#include <vector>

#include <platform/EnvironmentProvider.hpp>

namespace endo
{

/// @brief Completion provider for environment and special variables.
class VariableCompleter: public CompletionProvider
{
  public:
    /// @brief Constructs a variable completer with access to environment.
    /// @param env The environment to query for variable names.
    explicit VariableCompleter(EnvironmentProvider const& env);

    [[nodiscard]] std::vector<CompletionItem> complete(CompletionContext const& context) override;
    [[nodiscard]] bool canHandle(CompletionContextType type) const override;

    [[nodiscard]] int priority() const override { return 80; }

  private:
    EnvironmentProvider const& _env;

    /// @brief Returns list of special shell variables.
    [[nodiscard]] static std::vector<CompletionItem> specialVariables();
};

} // namespace endo
