// SPDX-License-Identifier: Apache-2.0
#include "HistoryCompleter.hpp"

namespace endo
{

HistoryCompleter::HistoryCompleter(History const& history): _history(history)
{
}

std::vector<CompletionItem> HistoryCompleter::complete(CompletionContext const& context)
{
    std::vector<CompletionItem> results;

    // For history completion, we match against the full input, not just the current word
    // This is because history entries are complete command lines
    // Use fuzzy search to find both prefix and fuzzy matches
    auto matches = _history.searchFuzzy(context.fullInput, 10);

    for (auto const& match: matches)
    {
        // Don't suggest the exact current input
        if (match.entry == context.fullInput)
            continue;

        results.push_back(CompletionItem { .text = std::string(match.entry),
                                           .displayText = std::string(match.entry),
                                           .description = "history",
                                           .score = match.score,
                                           .matchPositions = match.positions });
    }

    return results;
}

bool HistoryCompleter::canHandle(CompletionContextType type) const
{
    // History completion is primarily for command position (fish-style autosuggestion)
    return type == CompletionContextType::Command;
}

} // namespace endo
