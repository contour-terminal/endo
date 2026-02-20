// SPDX-License-Identifier: Apache-2.0
#include <shell/util/Suggestions.hpp>

#include <algorithm>
#include <format>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

namespace endo
{

size_t SuggestionGenerator::levenshteinDistance(std::string_view a, std::string_view b)
{
    auto const m = a.size();
    auto const n = b.size();

    // Handle edge cases
    if (m == 0)
        return n;
    if (n == 0)
        return m;

    // Use two rows for space efficiency
    std::vector<size_t> previousRow(n + 1);
    std::vector<size_t> currentRow(n + 1);

    // Initialize first row
    for (size_t j = 0; j <= n; ++j)
        previousRow[j] = j;

    // Fill the matrix row by row
    for (size_t i = 1; i <= m; ++i)
    {
        currentRow[0] = i;

        for (size_t j = 1; j <= n; ++j)
        {
            auto const cost = (a[i - 1] == b[j - 1]) ? 0 : 1;

            currentRow[j] = std::min({ previousRow[j] + 1,           // deletion
                                       currentRow[j - 1] + 1,        // insertion
                                       previousRow[j - 1] + cost }); // substitution
        }

        std::swap(previousRow, currentRow);
    }

    return previousRow[n];
}

std::vector<std::string> SuggestionGenerator::suggestCommand(std::string_view input,
                                                             std::span<std::string_view const> builtins,
                                                             size_t maxDistance)
{
    struct Candidate
    {
        std::string name;
        size_t distance;
    };

    std::vector<Candidate> candidates;

    for (auto const& builtin: builtins)
    {
        auto const dist = levenshteinDistance(input, builtin);
        if (dist <= maxDistance && dist > 0) // Exclude exact matches
            candidates.push_back({ std::string(builtin), dist });
    }

    // Sort by distance (closest first)
    std::ranges::sort(candidates, {}, &Candidate::distance);

    std::vector<std::string> result;
    result.reserve(candidates.size());
    for (auto const& c: candidates)
        result.push_back(c.name);

    return result;
}

std::vector<std::string> SuggestionGenerator::suggestSyntaxFix(Token expected, Token got)
{
    std::vector<std::string> suggestions;

    // Provide specific suggestions based on common mistakes
    if (expected == Token::Semicolon && got == Token::LineFeed)
        suggestions.emplace_back("Add ';' before the newline if continuing on the same logical line");

    if (expected == Token::Identifier && got == Token::Number)
        suggestions.emplace_back("Variable names cannot start with a number");

    if (got == Token::EndOfInput)
        suggestions.emplace_back("Check for unclosed quotes, parentheses, or control structures");

    return suggestions;
}

std::string SuggestionGenerator::formatDidYouMean(std::string_view suggestion)
{
    return std::format("Did you mean '{}'?", suggestion);
}

} // namespace endo
