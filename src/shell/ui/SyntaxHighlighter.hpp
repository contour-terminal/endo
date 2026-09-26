// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <endo-language/lexer/TokenClassification.hpp>

#include <core/tui/GenericSyntaxHighlighter.hpp>
#include <core/tui/TerminalOutput.hpp>
#include <core/tui/Theme.hpp>

#include <string_view>
#include <vector>

namespace endo
{

// Ensure TokenCategory and HighlightCategory ordinals stay in sync.
static_assert(static_cast<int>(TokenCategory::Function)
                  == static_cast<int>(core::tui::HighlightCategory::Function),
              "TokenCategory and HighlightCategory ordinals must match");

/// @brief Per-grapheme-cluster token category map for syntax highlighting.
///
/// One entry per grapheme cluster in the source text (0-indexed).
/// For ASCII input, the grapheme index equals the byte index.
using HighlightMap = std::vector<TokenCategory>;

/// @brief Tokenizes the source and produces a per-grapheme-cluster highlight map.
/// @param source The input text to highlight.
/// @return A vector of TokenCategory, one per grapheme cluster of source.
[[nodiscard]] HighlightMap computeHighlightMap(std::string_view source);

/// @brief Returns the display color for a given token category using the current theme.
/// @param category The token category.
/// @param theme The theme to read syntax colors from.
/// @return The RGB color to use for rendering.
[[nodiscard]] inline core::tui::RgbColor categoryColor(TokenCategory category,
                                                       core::tui::Theme const& theme) noexcept
{
    return core::tui::categoryColorFromIndex(static_cast<int>(category), theme.syntaxColors);
}

/// @brief Returns the display color for a given token category (using global theme).
/// @param category The token category.
/// @return The RGB color to use for rendering.
[[nodiscard]] inline core::tui::RgbColor categoryColor(TokenCategory category) noexcept
{
    return categoryColor(category, core::tui::currentTheme());
}

} // namespace endo
