// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <endo-language/lexer/Lexer.hpp>

#include <optional>
#include <string>

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

/// @brief Converts a lexer SourceLocationRange to a SourceRange.
/// Both use 0-based line and column indices.
/// @param loc The source location range from the endo lexer.
/// @return The corresponding SourceRange.
[[nodiscard]] inline SourceRange toSourceRange(SourceLocationRange const& loc)
{
    return SourceRange {
        .start = SourcePosition { .line = loc.begin.line, .character = loc.begin.column },
        .end = SourcePosition { .line = loc.end.line, .character = loc.end.column },
    };
}

/// @brief Checks if a 0-based position falls within a lexer source location range.
/// Both use 0-based line and column indices.
/// @param range The source location range from the endo lexer.
/// @param pos The 0-based position to test.
/// @return true if the position is within the range.
[[nodiscard]] inline bool containsPosition(SourceLocationRange const& range, SourcePosition pos)
{
    // Check if position is on or after the start
    if (pos.line < range.begin.line)
        return false;
    if (pos.line == range.begin.line && pos.character < range.begin.column)
        return false;

    // Check if position is before the end
    if (pos.line > range.end.line)
        return false;
    if (pos.line == range.end.line && pos.character >= range.end.column)
        return false;

    return true;
}

} // namespace endo
