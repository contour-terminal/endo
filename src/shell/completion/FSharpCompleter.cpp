// SPDX-License-Identifier: Apache-2.0
#include "FSharpCompleter.hpp"

#include <endo-language/ide/CompletionCandidates.hpp>

#include <tui/completer/FuzzyMatch.hpp>
#include <tui/completer/SmartCaseMatch.hpp>

#include <algorithm>

namespace endo
{

FSharpCompleter::FSharpCompleter(FSharpPersistentState const& state): _state(state)
{
}

std::vector<CompletionItem> FSharpCompleter::complete(CompletionContext const& context)
{
    auto const& prefix = context.prefix;

    // Find last dot in prefix
    auto const dotPos = prefix.rfind('.');
    if (dotPos == std::string::npos || dotPos == 0)
        return {};

    auto const objectPart = prefix.substr(0, dotPos);
    auto const memberPrefix = prefix.substr(dotPos + 1);

    return completeDotAccess(objectPart, memberPrefix, prefix, context.fullInput);
}

bool FSharpCompleter::canHandle(CompletionContextType type) const
{
    return type == CompletionContextType::Command || type == CompletionContextType::Argument;
}

std::vector<CompletionItem> FSharpCompleter::completeDotAccess(std::string const& objectPart,
                                                               std::string const& memberPrefix,
                                                               std::string const& /*fullPrefix*/,
                                                               std::string const& fullInput) const
{
    // Resolve pipeline element type for underscore completions
    auto const pipelineType = (objectPart == "_" || objectPart.starts_with("_."))
                                  ? resolvePipelineSourceType(fullInput, _state.commandOutputTypes)
                                  : std::string {};

    // Delegate to shared completion engine for candidate generation (already prefix-filtered)
    auto candidates = dotAccessCandidates(
        objectPart, memberPrefix, _state.recordTypeFields, {}, pipelineType, _state.moduleFunctions);

    // Convert to tui::CompletionItem with fuzzy scoring on the member name (not full text)
    std::vector<CompletionItem> results;
    tui::FuzzyConfig fuzzyConfig;
    auto const minThreshold = fuzzyConfig.minMatchThreshold;

    for (auto const& candidate: candidates)
    {
        // Extract member name from "object.member" for scoring
        auto const candidateDotPos = candidate.text.rfind('.');
        auto const memberName = candidateDotPos != std::string::npos
                                    ? candidate.text.substr(candidateDotPos + 1)
                                    : candidate.text;

        auto const isPrefixMatch = tui::SmartCaseMatch::matchesPrefix(memberName, memberPrefix);
        tui::FuzzyMatchResult fuzzyResult;
        auto isFuzzyMatch = false;

        if (!isPrefixMatch && !memberPrefix.empty())
        {
            fuzzyResult = tui::FuzzyMatch::matchSmartCase(memberName, memberPrefix);
            auto const textLen = tui::FuzzyMatch::countGraphemes(memberName);
            isFuzzyMatch = fuzzyResult.matches && fuzzyResult.quality(textLen) >= minThreshold;
        }

        if (!memberPrefix.empty() && !isPrefixMatch && !isFuzzyMatch)
            continue;

        int score;
        std::vector<size_t> matchPositions;
        int const baseScore = (objectPart == "Option") ? 90 : (objectPart == "_") ? 85 : 80;

        if (isPrefixMatch || memberPrefix.empty())
        {
            score = tui::SmartCaseMatch::adjustScore(baseScore, memberName, memberPrefix);
            if (isPrefixMatch)
                score += fuzzyConfig.prefixMatchBonus;
        }
        else
        {
            score = tui::FuzzyMatch::calculateScore(
                baseScore, memberName, memberPrefix, fuzzyResult, fuzzyConfig);
            matchPositions = std::move(fuzzyResult.positions);
        }

        results.push_back(CompletionItem { .text = candidate.text,
                                           .displayText = candidate.text,
                                           .description = candidate.description,
                                           .detail = candidate.detail,
                                           .score = score,
                                           .matchPositions = std::move(matchPositions) });
    }

    // Sort by score (descending), then alphabetically
    std::sort(results.begin(), results.end(), [](auto const& a, auto const& b) {
        if (a.score != b.score)
            return a.score > b.score;
        return a.text < b.text;
    });

    return results;
}

} // namespace endo
