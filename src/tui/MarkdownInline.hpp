// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>
#include <string_view>

namespace tui
{

/// @brief Result of finding a CommonMark inline code span.
struct InlineCodeSpan
{
    std::string_view content; ///< The code content between backtick sequences.
    std::size_t endPos;       ///< Position in the text after the closing backtick sequence.
};

/// @brief Applies CommonMark space stripping to inline code content.
///
/// If the content has both a leading and trailing space, and is not entirely
/// spaces, strips one leading and one trailing space.
///
/// @param content The raw content between backtick sequences.
/// @return The content with CommonMark space stripping applied.
[[nodiscard]] constexpr auto stripInlineCodeSpaces(std::string_view content) noexcept -> std::string_view
{
    if (content.size() >= 2 && content.front() == ' ' && content.back() == ' ')
    {
        auto allSpaces = true;
        for (auto ch: content)
        {
            if (ch != ' ')
            {
                allSpaces = false;
                break;
            }
        }
        if (!allSpaces)
            return content.substr(1, content.size() - 2);
    }
    return content;
}

/// @brief Finds the end of a CommonMark inline code span starting at pos.
///
/// Counts the opening backtick sequence length N starting at pos, then searches
/// for a matching closing sequence of exactly N backticks.
///
/// @param text The full text being parsed.
/// @param pos Position of the first opening backtick.
/// @return The code span content and end position, or std::nullopt if no match found.
[[nodiscard]] constexpr auto findInlineCodeEnd(std::string_view text, std::size_t pos)
    -> std::optional<InlineCodeSpan>
{
    auto const tickStart = pos;
    while (pos < text.size() && text[pos] == '`')
        ++pos;
    auto const tickCount = pos - tickStart;

    auto searchPos = pos;
    while (searchPos < text.size())
    {
        auto const closeStart = text.find('`', searchPos);
        if (closeStart == std::string_view::npos)
            break;

        auto closeEnd = closeStart;
        while (closeEnd < text.size() && text[closeEnd] == '`')
            ++closeEnd;

        if (closeEnd - closeStart == tickCount)
        {
            auto const content = text.substr(pos, closeStart - pos);
            return InlineCodeSpan { .content = stripInlineCodeSpaces(content), .endPos = closeEnd };
        }
        searchPos = closeEnd;
    }

    return std::nullopt;
}

} // namespace tui
