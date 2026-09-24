// SPDX-License-Identifier: Apache-2.0
#include "CompletionAdapter.hpp"

#include <core/tui/completer/FuzzyMatch.hpp>
#include <core/tui/completer/SmartCaseMatch.hpp>

namespace endo
{

std::vector<core::tui::completer::CompletionItem> applyFuzzyScoring(
    std::vector<CompletionCandidate> const& candidates, std::string_view prefix, int baseScore)
{
    std::vector<core::tui::completer::CompletionItem> results;

    core::tui::completer::FuzzyConfig fuzzyConfig;
    auto const minThreshold = fuzzyConfig.minMatchThreshold;

    for (auto const& candidate: candidates)
    {
        auto const& name = candidate.text;

        // Check both prefix and fuzzy matches
        auto const isPrefixMatch = core::tui::completer::SmartCaseMatch::matchesPrefix(name, prefix);
        core::tui::completer::FuzzyMatchResult fuzzyResult;
        auto isFuzzyMatch = false;

        if (!isPrefixMatch && !prefix.empty())
        {
            fuzzyResult = core::tui::completer::FuzzyMatch::matchSmartCase(name, prefix);
            auto const textLen = core::tui::completer::FuzzyMatch::countGraphemes(name);
            isFuzzyMatch =
                fuzzyResult.matches
                && (fuzzyResult.quality(textLen) >= minThreshold || fuzzyResult.isContiguousSubstring());
        }

        // Empty prefix matches everything
        if (!prefix.empty() && !isPrefixMatch && !isFuzzyMatch)
            continue;

        int score = 0;
        std::vector<size_t> matchPositions;

        if (isPrefixMatch || prefix.empty())
        {
            score = core::tui::completer::SmartCaseMatch::adjustScore(baseScore, name, prefix);
            if (isPrefixMatch)
                score += fuzzyConfig.prefixMatchBonus;
        }
        else
        {
            score = core::tui::completer::FuzzyMatch::calculateScore(
                baseScore, name, prefix, fuzzyResult, fuzzyConfig);
            matchPositions = std::move(fuzzyResult.positions);
        }

        // Avoid duplicates
        auto isDuplicate = false;
        for (auto const& existing: results)
        {
            if (existing.text == name)
            {
                isDuplicate = true;
                break;
            }
        }
        if (isDuplicate)
            continue;

        results.push_back(core::tui::completer::CompletionItem {
            .text = name,
            .displayText = candidate.displayText.empty() ? name : candidate.displayText,
            .description = candidate.description,
            .detail = candidate.detail,
            .score = score,
            .matchPositions = std::move(matchPositions),
        });
    }

    return results;
}

} // namespace endo
