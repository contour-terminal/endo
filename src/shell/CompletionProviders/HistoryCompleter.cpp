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
    auto matches = _history.search(context.fullInput, 10);

    int score = 100; // Recency ordering: first match is most recent
    for (auto const& match: matches)
    {
        // Don't suggest the exact current input
        if (match == context.fullInput)
            continue;

        results.push_back(CompletionItem { .text = std::string(match),
                                           .displayText = std::string(match),
                                           .description = "history",
                                           .score = score-- });
    }

    return results;
}

bool HistoryCompleter::canHandle(CompletionContextType type) const
{
    // History completion is primarily for command position (fish-style autosuggestion)
    return type == CompletionContextType::Command;
}

} // namespace endo
