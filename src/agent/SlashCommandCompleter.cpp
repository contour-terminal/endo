// SPDX-License-Identifier: Apache-2.0
#include <tui/completer/FuzzyMatch.hpp>
#include <tui/completer/SmartCaseMatch.hpp>

#include <algorithm>
#include <string>

#include <agent/SlashCommandCompleter.hpp>
#include <agent/SlashCommandRegistry.hpp>

namespace endo::agent
{

SlashCommandCompleter::SlashCommandCompleter(SlashCommandRegistry const& registry): _registry(registry)
{
}

std::vector<tui::CompletionItem> SlashCommandCompleter::complete(std::string_view input,
                                                                 size_t cursorPosition)
{
    // Only complete when input starts with '/'
    if (input.empty() || input[0] != '/')
        return {};

    // Only complete the command name, not arguments after the first space
    auto const inputUpToCursor = input.substr(0, cursorPosition);
    if (inputUpToCursor.find(' ') != std::string_view::npos)
        return {};

    // Extract the prefix after '/' up to cursor
    auto const prefix = inputUpToCursor.substr(1);

    auto items = std::vector<tui::CompletionItem> {};

    for (auto const& cmd: _registry.commands())
    {
        auto const cmdName = cmd->name();
        auto const fullText = "/" + std::string(cmdName);

        // Try smart-case prefix match first
        if (tui::SmartCaseMatch::matchesPrefix(cmdName, prefix))
        {
            auto score = tui::SmartCaseMatch::adjustScore(100, cmdName, prefix);
            items.push_back(tui::CompletionItem {
                .text = fullText,
                .description = std::string(cmd->description()),
                .score = score,
            });
            continue;
        }

        // Fall back to fuzzy match
        auto const fuzzyResult = tui::FuzzyMatch::matchSmartCase(cmdName, prefix);
        if (fuzzyResult.matches)
        {
            auto const score = tui::FuzzyMatch::calculateScore(50, cmdName, prefix, fuzzyResult);
            // Adjust match positions to account for leading '/'
            auto positions = std::vector<size_t> {};
            positions.reserve(fuzzyResult.positions.size());
            for (auto pos: fuzzyResult.positions)
                positions.push_back(pos + 1); // +1 for the leading '/'
            items.push_back(tui::CompletionItem {
                .text = fullText,
                .description = std::string(cmd->description()),
                .score = score,
                .matchPositions = std::move(positions),
            });
        }
    }

    // Sort by score descending, then alphabetically
    std::ranges::sort(items, [](auto const& a, auto const& b) {
        if (a.score != b.score)
            return a.score > b.score;
        return a.text < b.text;
    });

    return items;
}

} // namespace endo::agent
