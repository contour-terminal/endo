// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <shell/completion/CompletionProvider.hpp>
#include <shell/completion/PathCommandIndex.hpp>
#include <shell/history/History.hpp>

#include <string>
#include <vector>

#include <platform/EnvironmentProvider.hpp>

namespace endo
{

/// @brief Completion provider for command names (builtins and PATH executables).
class CommandCompleter: public CompletionProvider
{
  public:
    /// @brief Constructs a command completer.
    /// @param pathCommands Index of $PATH executables; must outlive this completer.
    /// @param env The environment to query for HOME (to shorten displayed paths).
    /// @param history The command history for recency-based scoring.
    CommandCompleter(PathCommandIndex const& pathCommands,
                     EnvironmentProvider const& env,
                     History const& history);

    [[nodiscard]] std::vector<CompletionItem> complete(CompletionContext const& context) override;
    [[nodiscard]] bool canHandle(CompletionContextType type) const override;

    [[nodiscard]] int priority() const override { return 100; }

  private:
    PathCommandIndex const& _pathCommands;
    EnvironmentProvider const& _env;
    History const& _history;
};

} // namespace endo
