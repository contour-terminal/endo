// SPDX-License-Identifier: Apache-2.0
#include <tui/completer/FuzzyMatch.hpp>
#include <tui/completer/SmartCaseMatch.hpp>

#include <algorithm>
#include <string>

#include <agent/commands/FilePathCompleter.hpp>

namespace endo::agent
{

void FilePathCompleter::setFilePaths(std::vector<std::string> paths)
{
    auto lock = std::scoped_lock(_mutex);
    _filePaths = std::move(paths);
}

size_t FilePathCompleter::findAtPosition(std::string_view input, size_t cursorPosition)
{
    auto const upToCursor = input.substr(0, cursorPosition);

    // Search backward from cursor for '@'
    auto const atPos = upToCursor.rfind('@');
    if (atPos == std::string_view::npos)
        return std::string_view::npos;

    // '@' must be at position 0 or preceded by whitespace
    if (atPos > 0 && !std::isspace(static_cast<unsigned char>(input[atPos - 1])))
        return std::string_view::npos;

    // No whitespace between '@' and cursor
    auto const afterAt = upToCursor.substr(atPos + 1);
    if (afterAt.find_first_of(" \t\n") != std::string_view::npos)
        return std::string_view::npos;

    return atPos;
}

std::vector<tui::CompletionItem> FilePathCompleter::complete(std::string_view input, size_t cursorPosition)
{
    auto const atPos = findAtPosition(input, cursorPosition);
    if (atPos == std::string_view::npos)
        return {};

    // Extract query after '@'
    auto const query = input.substr(atPos + 1, cursorPosition - atPos - 1);

    auto lock = std::scoped_lock(_mutex);

    auto items = std::vector<tui::CompletionItem> {};

    for (auto const& filePath: _filePaths)
    {
        auto const fullText = "@" + filePath;
        auto const isDir = filePath.ends_with('/');
        auto const description = std::string(isDir ? "directory" : "file");

        if (query.empty())
        {
            // '@' alone: return all entries
            items.push_back(tui::CompletionItem {
                .text = fullText,
                .displayText = filePath,
                .description = description,
                .score = 50,
            });
            continue;
        }

        // Try smart-case prefix match first
        if (tui::SmartCaseMatch::matchesPrefix(filePath, query))
        {
            auto const score = tui::SmartCaseMatch::adjustScore(100, filePath, query);
            items.push_back(tui::CompletionItem {
                .text = fullText,
                .displayText = filePath,
                .description = description,
                .score = score,
            });
            continue;
        }

        // Fall back to fuzzy match
        auto const fuzzyResult = tui::FuzzyMatch::matchSmartCase(filePath, query);
        if (fuzzyResult.matches)
        {
            auto const score = tui::FuzzyMatch::calculateScore(50, filePath, query, fuzzyResult);
            // Adjust match positions to account for leading '@'
            auto positions = std::vector<size_t> {};
            positions.reserve(fuzzyResult.positions.size());
            for (auto pos: fuzzyResult.positions)
                positions.push_back(pos + 1); // +1 for the leading '@'
            items.push_back(tui::CompletionItem {
                .text = fullText,
                .displayText = filePath,
                .description = description,
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
