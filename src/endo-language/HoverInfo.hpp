// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>
#include <string>

#include <endo-language/Lexer.hpp>

namespace endo
{

/// @brief 0-based source position (line and character).
struct SourcePosition
{
    int line = 0;
    int character = 0;
};

/// @brief 0-based source range (start and end positions).
struct SourceRange
{
    SourcePosition start;
    SourcePosition end;
};

/// @brief Hover information for a symbol at a given position.
struct HoverInfo
{
    std::string markdownText;         ///< Markdown-formatted hover content.
    std::optional<SourceRange> range; ///< Optional source range of the hovered token.
};

/// @brief Converts a lexer SourceLocationRange to a 0-based SourceRange.
/// The endo lexer uses 1-based columns; SourceRange uses 0-based.
/// @param loc The source location range from the endo lexer.
/// @return The corresponding 0-based SourceRange.
[[nodiscard]] inline SourceRange toSourceRange(SourceLocationRange const& loc)
{
    return SourceRange {
        .start = SourcePosition { .line = loc.begin.line,
                                  .character = loc.begin.column > 0 ? loc.begin.column - 1 : 0 },
        .end =
            SourcePosition { .line = loc.end.line, .character = loc.end.column > 0 ? loc.end.column - 1 : 0 },
    };
}

/// @brief Checks if a 0-based position falls within a lexer source location range.
/// The lexer uses 1-based columns, so we convert during comparison.
/// @param range The source location range from the endo lexer.
/// @param pos The 0-based position to test.
/// @return true if the position is within the range.
[[nodiscard]] inline bool containsPosition(SourceLocationRange const& range, SourcePosition pos)
{
    // Convert lexer 1-based columns to 0-based for comparison
    auto const beginCol = range.begin.column > 0 ? range.begin.column - 1 : 0;
    auto const endCol = range.end.column > 0 ? range.end.column - 1 : 0;

    // Check if position is on or after the start
    if (pos.line < range.begin.line)
        return false;
    if (pos.line == range.begin.line && pos.character < beginCol)
        return false;

    // Check if position is before the end
    if (pos.line > range.end.line)
        return false;
    if (pos.line == range.end.line && pos.character >= endCol)
        return false;

    return true;
}

} // namespace endo
