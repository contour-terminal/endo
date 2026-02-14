// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <endo-language/TokenClassification.hpp>

#include <tui/TerminalOutput.hpp>
#include <tui/Theme.hpp>

#include <string_view>
#include <vector>

namespace endo
{

/// @brief Per-byte token category map for syntax highlighting.
using HighlightMap = std::vector<TokenCategory>;

/// @brief Tokenizes the source and produces a per-byte highlight map.
/// @param source The input text to highlight.
/// @return A vector of TokenCategory, one per byte of source.
[[nodiscard]] HighlightMap computeHighlightMap(std::string_view source);

/// @brief Returns the display color for a given token category using the current theme.
/// @param category The token category.
/// @param theme The theme to read syntax colors from.
/// @return The RGB color to use for rendering.
[[nodiscard]] inline tui::RgbColor categoryColor(TokenCategory category, tui::Theme const& theme) noexcept
{
    auto const& c = theme.syntaxColors;
    using enum TokenCategory;
    switch (category)
    {
        case Keyword: return c.keyword;
        case Number: return c.number;
        case String: return c.string;
        case Operator: return c.op;
        case Variable: return c.variable;
        case Constructor: return c.constructor;
        case Punctuation: return c.punctuation;
        case Comment: return c.comment;
        case Type: return c.type;
        case Function: return c.function;
        case Default: return c.defaultText;
    }
    return c.defaultText;
}

/// @brief Returns the display color for a given token category (using global theme).
/// @param category The token category.
/// @return The RGB color to use for rendering.
[[nodiscard]] inline tui::RgbColor categoryColor(TokenCategory category) noexcept
{
    return categoryColor(category, tui::currentTheme());
}

} // namespace endo
