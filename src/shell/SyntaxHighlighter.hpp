// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string_view>
#include <vector>

#include <endo-language/TokenClassification.hpp>
#include <tui/TerminalOutput.hpp>

namespace endo
{

/// @brief Per-byte token category map for syntax highlighting.
using HighlightMap = std::vector<TokenCategory>;

/// @brief Tokenizes the source and produces a per-byte highlight map.
/// @param source The input text to highlight.
/// @return A vector of TokenCategory, one per byte of source.
[[nodiscard]] HighlightMap computeHighlightMap(std::string_view source);

/// @brief Returns the display color for a given token category (dark theme).
/// @param category The token category.
/// @return The RGB color to use for rendering.
[[nodiscard]] constexpr tui::RgbColor categoryColor(TokenCategory category) noexcept
{
    using enum TokenCategory;
    switch (category)
    {
        case Keyword: return { .r = 198, .g = 120, .b = 221 };     // Purple
        case Number: return { .r = 209, .g = 154, .b = 102 };      // Warm orange
        case String: return { .r = 152, .g = 195, .b = 121 };      // Green
        case Operator: return { .r = 86, .g = 182, .b = 194 };     // Cyan
        case Variable: return { .r = 224, .g = 108, .b = 117 };    // Soft red
        case Constructor: return { .r = 229, .g = 192, .b = 123 }; // Yellow
        case Punctuation: return { .r = 171, .g = 178, .b = 191 }; // Subtle gray
        case Comment: return { .r = 127, .g = 132, .b = 142 };     // Dim gray
        case Type: return { .r = 229, .g = 192, .b = 123 };        // Yellow (same as Constructor)
        case Default: return { .r = 220, .g = 220, .b = 220 };     // Default text color
    }
    return { .r = 220, .g = 220, .b = 220 };
}

} // namespace endo
