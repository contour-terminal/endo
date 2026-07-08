// SPDX-License-Identifier: Apache-2.0
#include "CompletionAdapter.hpp"

#include <tui/completer/FuzzyMatch.hpp>
#include <tui/completer/SmartCaseMatch.hpp>

namespace endo
{

namespace
{
    /// @brief Score floor added to prefix matches so they always outrank fuzzy-only
    /// matches, regardless of the per-run/word-start bonuses fuzzy scoring accumulates.
    ///
    /// Without a tier separation, a scattered subsequence match against a long
    /// hyphenated candidate can outscore a genuine prefix match: e.g. completing
    /// `plasma-` against a package list, `perl-Lingua-Stem-Snowball-Da` matches the
    /// subsequence p·l·a·s·m·a·- and earns enough consecutive/word-start bonus to rank
    /// above `plasma-activities`. Users (and every other shell) expect prefix matches
    /// first; fuzzy matches are a fallback for when few or no prefixes match. This tier
    /// guarantees that ordering while leaving the intra-tier fuzzy ranking intact.
    ///
    /// The value dwarfs the largest reachable fuzzy bonus (base score plus
    /// maxMatchPercentBonus + N*consecutiveBonus + N*wordStartBonus for any realistic
    /// candidate length), so no fuzzy match can cross into the prefix tier.
    constexpr int PrefixTierOffset = 100000;
} // namespace

std::vector<tui::CompletionItem> applyFuzzyScoring(std::vector<CompletionCandidate> const& candidates,
                                                   std::string_view prefix,
                                                   int baseScore)
{
    std::vector<tui::CompletionItem> results;

    tui::FuzzyConfig fuzzyConfig;
    auto const minThreshold = fuzzyConfig.minMatchThreshold;

    for (auto const& candidate: candidates)
    {
        auto const& name = candidate.text;

        // Check both prefix and fuzzy matches
        auto const isPrefixMatch = tui::SmartCaseMatch::matchesPrefix(name, prefix);
        tui::FuzzyMatchResult fuzzyResult;
        auto isFuzzyMatch = false;

        if (!isPrefixMatch && !prefix.empty())
        {
            fuzzyResult = tui::FuzzyMatch::matchSmartCase(name, prefix);
            auto const textLen = tui::FuzzyMatch::countGraphemes(name);
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
            score = tui::SmartCaseMatch::adjustScore(baseScore, name, prefix);
            if (isPrefixMatch)
            {
                // Lift genuine prefix matches into a dedicated tier above every fuzzy
                // match, so a scattered subsequence hit on a long candidate can never
                // displace a shorter prefix match under the result cap.
                score += fuzzyConfig.prefixMatchBonus + PrefixTierOffset;
            }
        }
        else
        {
            score = tui::FuzzyMatch::calculateScore(baseScore, name, prefix, fuzzyResult, fuzzyConfig);
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

        results.push_back(tui::CompletionItem {
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
