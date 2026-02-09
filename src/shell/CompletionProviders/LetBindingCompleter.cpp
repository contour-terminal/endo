// SPDX-License-Identifier: Apache-2.0
#include "LetBindingCompleter.hpp"

#include <algorithm>

#include <endo-language/Type.hpp>
#include <tui/completer/FuzzyMatch.hpp>
#include <tui/completer/SmartCaseMatch.hpp>

namespace endo
{

LetBindingCompleter::LetBindingCompleter(FSharpPersistentState const& state): _state(state)
{
}

std::vector<CompletionItem> LetBindingCompleter::complete(CompletionContext const& context)
{
    std::vector<CompletionItem> results;
    auto const& prefix = context.prefix;

    // Configuration for fuzzy matching
    tui::FuzzyConfig fuzzyConfig;
    auto const minThreshold = fuzzyConfig.minMatchThreshold;

    // Helper lambda for adding a match
    auto addMatch = [&](std::string const& name, std::string const& description, int baseScore) {
        // Check if already added (avoid duplicates)
        for (auto const& existing: results)
        {
            if (existing.text == name)
                return;
        }

        // Check both prefix and fuzzy matches
        auto isPrefixMatch = tui::SmartCaseMatch::matchesPrefix(name, prefix);
        tui::FuzzyMatchResult fuzzyResult;
        auto isFuzzyMatch = false;

        if (!isPrefixMatch && !prefix.empty())
        {
            fuzzyResult = tui::FuzzyMatch::matchSmartCase(name, prefix);
            auto const textLen = tui::FuzzyMatch::countGraphemes(name);
            isFuzzyMatch = fuzzyResult.matches && fuzzyResult.quality(textLen) >= minThreshold;
        }

        if (!isPrefixMatch && !isFuzzyMatch)
            return;

        int score;
        std::vector<size_t> matchPositions;

        if (isPrefixMatch)
        {
            score = tui::SmartCaseMatch::adjustScore(baseScore, name, prefix);
            score += fuzzyConfig.prefixMatchBonus;
        }
        else
        {
            score = tui::FuzzyMatch::calculateScore(baseScore, name, prefix, fuzzyResult, fuzzyConfig);
            matchPositions = std::move(fuzzyResult.positions);
        }

        results.push_back(CompletionItem { .text = name,
                                           .displayText = name,
                                           .description = description,
                                           .score = score,
                                           .matchPositions = std::move(matchPositions) });
    };

    // Add persisted functions (higher base score)
    for (auto const& [name, func]: _state.functions)
        addMatch(name, formatFunctionDescription(name, func), 85);

    // Add persisted value bindings (lower base score)
    for (auto const& binding: _state.valueBindings)
        addMatch(binding.name, formatValueDescription(binding), 75);

    // Sort by score (descending), then alphabetically
    std::sort(results.begin(), results.end(), [](auto const& a, auto const& b) {
        if (a.score != b.score)
            return a.score > b.score;
        return a.text < b.text;
    });

    return results;
}

bool LetBindingCompleter::canHandle(CompletionContextType type) const
{
    return type == CompletionContextType::Command || type == CompletionContextType::Argument;
}

std::string LetBindingCompleter::formatFunctionDescription(
    std::string const& name, FSharpPersistentState::PersistedFunction const& func)
{
    std::string result;

    if (func.isRecursive)
        result += "rec ";

    result += name;
    result += '(';

    for (size_t i = 0; i < func.parameters.size(); ++i)
    {
        if (i > 0)
            result += ", ";

        result += func.parameters[i];

        if (i < func.parameterTypes.size() && func.parameterTypes[i].has_value())
        {
            result += ": ";
            result += toString(*func.parameterTypes[i]);
        }
    }

    result += ')';

    if (func.returnType.has_value())
    {
        result += " -> ";
        result += toString(*func.returnType);
    }

    return result;
}

std::string LetBindingCompleter::formatValueDescription(
    FSharpPersistentState::PersistedValueBinding const& binding)
{
    return binding.isMutable ? "mutable value" : "value";
}

} // namespace endo
