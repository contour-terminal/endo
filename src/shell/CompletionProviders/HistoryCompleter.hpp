// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <shell/CompletionProvider.hpp>
#include <shell/History.hpp>

#include <string>
#include <vector>

namespace endo
{

/// @brief Completion provider based on command history.
class HistoryCompleter: public CompletionProvider
{
  public:
    /// @brief Constructs a history completer.
    /// @param history The history to search.
    explicit HistoryCompleter(History const& history);

    [[nodiscard]] std::vector<CompletionItem> complete(CompletionContext const& context) override;
    [[nodiscard]] bool canHandle(CompletionContextType type) const override;

    [[nodiscard]] int priority() const override { return 30; }

  private:
    History const& _history;
};

} // namespace endo
