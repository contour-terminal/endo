// SPDX-License-Identifier: Apache-2.0
#include "FSharpCompleter.hpp"

#include <algorithm>
#include <set>

#include <tui/completer/FuzzyMatch.hpp>
#include <tui/completer/SmartCaseMatch.hpp>

namespace endo
{

namespace
{
    /// @brief Static table of Option module methods with descriptions.
    struct OptionMethod
    {
        std::string_view name;
        std::string_view description;
    };

    constexpr std::array optionMethods = {
        OptionMethod { "map", "Option.map f opt -> option" },
        OptionMethod { "bind", "Option.bind f opt -> option" },
        OptionMethod { "defaultValue", "Option.defaultValue d opt -> value" },
    };
} // namespace

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

    return completeDotAccess(objectPart, memberPrefix, prefix);
}

bool FSharpCompleter::canHandle(CompletionContextType type) const
{
    return type == CompletionContextType::Command || type == CompletionContextType::Argument;
}

std::vector<CompletionItem> FSharpCompleter::completeDotAccess(std::string const& objectPart,
                                                               std::string const& memberPrefix,
                                                               std::string const& fullPrefix) const
{
    std::vector<CompletionItem> results;

    tui::FuzzyConfig fuzzyConfig;
    auto const minThreshold = fuzzyConfig.minMatchThreshold;

    // Helper: add a candidate if it matches the member prefix
    auto addCandidate = [&](std::string const& completionText,
                            std::string const& memberName,
                            std::string const& description,
                            int baseScore) {
        // Check both prefix and fuzzy matches against the member name
        auto isPrefixMatch = tui::SmartCaseMatch::matchesPrefix(memberName, memberPrefix);
        tui::FuzzyMatchResult fuzzyResult;
        auto isFuzzyMatch = false;

        if (!isPrefixMatch && !memberPrefix.empty())
        {
            fuzzyResult = tui::FuzzyMatch::matchSmartCase(memberName, memberPrefix);
            auto const textLen = tui::FuzzyMatch::countGraphemes(memberName);
            isFuzzyMatch = fuzzyResult.matches && fuzzyResult.quality(textLen) >= minThreshold;
        }

        // Empty member prefix matches everything (user typed "Option." or "_.")
        if (!memberPrefix.empty() && !isPrefixMatch && !isFuzzyMatch)
            return;

        int score;
        std::vector<size_t> matchPositions;

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

        // Avoid duplicates
        for (auto const& existing: results)
            if (existing.text == completionText)
                return;

        results.push_back(CompletionItem { .text = completionText,
                                           .displayText = completionText,
                                           .description = description,
                                           .score = score,
                                           .matchPositions = std::move(matchPositions) });
    };

    if (objectPart == "Option")
    {
        // Static Option module methods
        for (auto const& method: optionMethods)
            addCandidate("Option." + std::string(method.name),
                         std::string(method.name),
                         std::string(method.description),
                         90);
    }
    else if (objectPart == "_")
    {
        // Underscore field access: offer all record fields with deduplication
        std::set<std::string> seen;
        for (auto const& [typeName, fields]: _state.recordTypeFields)
        {
            for (auto const& fieldName: fields)
            {
                if (seen.insert(fieldName).second)
                    addCandidate("_." + fieldName, fieldName, typeName + " field", 85);
            }
        }
    }
    else
    {
        // Generic value: offer both Option methods and record fields
        for (auto const& method: optionMethods)
            addCandidate(objectPart + "." + std::string(method.name),
                         std::string(method.name),
                         std::string(method.description),
                         80);

        std::set<std::string> seen;
        for (auto const& [typeName, fields]: _state.recordTypeFields)
        {
            for (auto const& fieldName: fields)
            {
                if (seen.insert(fieldName).second)
                    addCandidate(objectPart + "." + fieldName, fieldName, typeName + " field", 75);
            }
        }
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
