// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <shell/completion/CompletionProvider.hpp>
#include <shell/history/History.hpp>

#include <core/platform/EnvironmentProvider.hpp>
#include <core/platform/FileSystem.hpp>

#include <vector>

namespace endo
{

/// @brief Completion provider based on command history.
///
/// Uses the supplied environment provider to read the current working directory
/// on each completion request so `searchFuzzy` can boost entries whose stored
/// CWD matches or ancestors the current CWD. Entries whose `requiredPaths` no
/// longer exist are suppressed.
class HistoryCompleter: public CompletionProvider
{
  public:
    /// @brief Constructs a history completer.
    /// @param history The history to search.
    /// @param env     Environment provider (for current CWD and $HOME).
    /// @param fs      Filesystem used for required-paths validation.
    HistoryCompleter(History const& history,
                     core::platform::EnvironmentProvider const& env,
                     core::platform::FileSystem const& fs);

    [[nodiscard]] std::vector<CompletionItem> complete(CompletionContext const& context) override;
    [[nodiscard]] bool canHandle(CompletionContextType type) const override;

    [[nodiscard]] int priority() const override { return 30; }

  private:
    History const& _history;
    core::platform::EnvironmentProvider const& _env;
    core::platform::FileSystem const& _fs;
};

} // namespace endo
