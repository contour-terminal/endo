// SPDX-License-Identifier: Apache-2.0
#include <tui/completer/FuzzyMatch.hpp>
#include <tui/completer/SmartCaseMatch.hpp>

#include <algorithm>

#include <agent/AgentHistoryProvider.hpp>

namespace endo::agent
{

void AgentHistoryProvider::addEntry(std::string entry)
{
    if (entry.empty())
        return;

    // Remove existing duplicate (keep most recent at end).
    std::erase(_entries, entry);
    _entries.push_back(std::move(entry));
}

void AgentHistoryProvider::setEntries(std::vector<std::string> entries)
{
    _entries = std::move(entries);
}

auto AgentHistoryProvider::complete(std::string_view input, size_t cursorPosition)
    -> std::vector<tui::CompletionItem>
{
    if (input.empty() || cursorPosition == 0)
        return {};

    // Don't complete slash commands — leave that to SlashCommandCompleter.
    if (input.starts_with("/"))
        return {};

    auto const query = input.substr(0, cursorPosition);
    auto items = std::vector<tui::CompletionItem> {};

    // Score entries by recency (most recent = highest base score).
    auto const entryCount = static_cast<int>(_entries.size());
    for (auto i = 0; i < entryCount; ++i)
    {
        auto const& entry = _entries[static_cast<size_t>(i)];
        if (entry == query)
            continue; // Don't suggest what's already typed.

        auto const recencyScore = i + 1; // 1..N, most recent = highest

        // Try prefix match first.
        if (tui::SmartCaseMatch::matchesPrefix(entry, query))
        {
            auto score = tui::SmartCaseMatch::adjustScore(recencyScore, entry, query);
            items.push_back(tui::CompletionItem {
                .text = entry,
                .displayText = entry,
                .description = "history",
                .score = score,
            });
            continue;
        }

        // Try fuzzy match.
        auto const fuzzyResult = tui::FuzzyMatch::matchSmartCase(entry, query);
        if (fuzzyResult.matches)
        {
            auto const score = tui::FuzzyMatch::calculateScore(recencyScore, entry, query, fuzzyResult);
            items.push_back(tui::CompletionItem {
                .text = entry,
                .displayText = entry,
                .description = "history",
                .score = score,
                .matchPositions = fuzzyResult.positions,
            });
        }
    }

    // Sort by score descending.
    std::ranges::sort(items, [](auto const& a, auto const& b) { return a.score > b.score; });

    return items;
}

} // namespace endo::agent
