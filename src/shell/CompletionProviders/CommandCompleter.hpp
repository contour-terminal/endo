// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <shell/CompletionProvider.hpp>
#include <shell/platform/EnvironmentProvider.hpp>

#include <string>
#include <utility>
#include <vector>

namespace endo
{

/// @brief Completion provider for command names (builtins and PATH executables).
class CommandCompleter: public CompletionProvider
{
  public:
    /// @brief Constructs a command completer with access to environment for PATH.
    /// @param env The environment to query for PATH.
    explicit CommandCompleter(EnvironmentProvider const& env);

    [[nodiscard]] std::vector<CompletionItem> complete(CompletionContext const& context) override;
    [[nodiscard]] bool canHandle(CompletionContextType type) const override;

    [[nodiscard]] int priority() const override { return 100; }

    /// @brief Invalidates the cached command list (call when PATH changes).
    void invalidateCache();

  private:
    EnvironmentProvider const& _env;

    // Cached PATH scan results: (command name, full path)
    mutable std::vector<std::pair<std::string, std::string>> _cachedCommands;
    mutable std::string _cachedPath;

    void refreshCacheIfNeeded() const;
    [[nodiscard]] std::vector<std::pair<std::string, std::string>> scanPath() const;
    [[nodiscard]] static std::vector<std::string> builtinNames();
};

} // namespace endo
