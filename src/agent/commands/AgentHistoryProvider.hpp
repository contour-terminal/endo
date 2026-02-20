// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tui/completer/CompletionProvider.hpp>

#include <string>
#include <vector>

namespace endo::agent
{

/// @brief Completion provider that suggests from previous agent queries.
///
/// Provides both prefix and fuzzy matching against stored user queries,
/// scored by recency (most recent = highest score). Used for ghost text
/// suggestions and completion popup in agent mode.
class AgentHistoryProvider final: public tui::CompletionProvider
{
  public:
    AgentHistoryProvider() = default;

    /// @brief Adds a query to the history (deduplicates, keeping most recent).
    /// @param entry The user query string to add.
    void addEntry(std::string entry);

    /// @brief Bulk-sets the history entries (e.g. from persisted data).
    /// @param entries The entries to set (order is preserved, last = most recent).
    void setEntries(std::vector<std::string> entries);

    /// @brief Returns the stored entries (oldest first, most recent last).
    [[nodiscard]] auto entries() const noexcept -> std::vector<std::string> const& { return _entries; }

    /// @brief Generates completions from history for the given input.
    [[nodiscard]] auto complete(std::string_view input, size_t cursorPosition)
        -> std::vector<tui::CompletionItem> override;

    /// @brief Returns priority below slash commands (100) but above default (0).
    [[nodiscard]] int priority() const override { return 50; }

  private:
    std::vector<std::string> _entries; ///< Stored queries, oldest first.
};

} // namespace endo::agent
