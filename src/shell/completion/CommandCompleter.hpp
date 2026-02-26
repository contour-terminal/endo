// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <shell/completion/CompletionProvider.hpp>
#include <shell/history/History.hpp>

#include <string>
#include <utility>
#include <vector>

#include <platform/EnvironmentProvider.hpp>

namespace endo
{

/// @brief Completion provider for command names (builtins and PATH executables).
class CommandCompleter: public CompletionProvider
{
  public:
    /// @brief Constructs a command completer with access to environment for PATH and history for recency.
    /// @param env The environment to query for PATH.
    /// @param history The command history for recency-based scoring.
    CommandCompleter(EnvironmentProvider const& env, History const& history);

    [[nodiscard]] std::vector<CompletionItem> complete(CompletionContext const& context) override;
    [[nodiscard]] bool canHandle(CompletionContextType type) const override;

    [[nodiscard]] int priority() const override { return 100; }

    /// @brief Invalidates the cached command list (call when PATH changes).
    void invalidateCache();

  private:
    EnvironmentProvider const& _env;
    History const& _history;

    // Cached PATH scan results: (command name, full path)
    mutable std::vector<std::pair<std::string, std::string>> _cachedCommands;
    mutable std::string _cachedPath;

    void refreshCacheIfNeeded() const;
    [[nodiscard]] std::vector<std::pair<std::string, std::string>> scanPath() const;
    [[nodiscard]] static std::vector<std::string> builtinNames();
};

} // namespace endo
