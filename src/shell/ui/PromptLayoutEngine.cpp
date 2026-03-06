// SPDX-License-Identifier: Apache-2.0
#include "PromptLayoutEngine.hpp"

#include <tui/Canvas.hpp>
#include <tui/Theme.hpp>
#include <tui/Unicode.hpp>

#if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wold-style-cast"
#endif
#include <libunicode/utf8_grapheme_segmenter.h>
#if defined(__clang__)
    #pragma clang diagnostic pop
#endif

namespace endo
{

int PromptLayoutEngine::render(tui::Canvas& canvas,
                               PromptConfig const& config,
                               std::vector<PromptSegments> const& infoModules,
                               std::vector<PromptSegments> const& rightModules,
                               tui::Theme const& theme)
{
    switch (config.layout)
    {
        case PromptLayoutKind::SingleLine: return renderSingleLine(canvas, config, infoModules, theme);
        case PromptLayoutKind::TwoLine:
            return renderTwoLine(canvas, config, infoModules, rightModules, theme);
        case PromptLayoutKind::Boxed: return renderBoxed(canvas, config, infoModules, theme);
        case PromptLayoutKind::Powerline: return renderPowerline(canvas, config, infoModules, theme);
    }
    return 0;
}

int PromptLayoutEngine::preferredHeight(PromptConfig const& config)
{
    switch (config.layout)
    {
        case PromptLayoutKind::SingleLine: return 1;
        case PromptLayoutKind::TwoLine: return 2;
        case PromptLayoutKind::Boxed: return 4; // top border + content + bottom border + indicator line
        case PromptLayoutKind::Powerline: return 2;
    }
    return 1;
}

// =============================================================================
// SingleLine layout: [modules...] indicator
// =============================================================================

int PromptLayoutEngine::renderSingleLine(tui::Canvas& canvas,
                                         PromptConfig const& config,
                                         std::vector<PromptSegments> const& infoModules,
                                         tui::Theme const& theme)
{
    auto col = 0;

    // Render all info modules with space separation
    for (std::size_t i = 0; i < infoModules.size(); ++i)
    {
        if (i > 0)
        {
            canvas.put(0, col, " ", tui::Style {});
            ++col;
        }
        col += renderSegments(canvas, 0, col, infoModules[i]);
    }

    // Space before indicator
    if (!infoModules.empty())
    {
        canvas.put(0, col, " ", tui::Style {});
        ++col;
    }

    // Indicator
    auto indicatorStyle = tui::Style {};
    indicatorStyle.fg = theme.promptColors.indicator;
    canvas.putString(0, col, config.indicator, indicatorStyle);

    return 1;
}

// =============================================================================
// TwoLine layout: separator + info on line 1, separator + indicator on line 2
// =============================================================================

int PromptLayoutEngine::renderTwoLine(tui::Canvas& canvas,
                                      PromptConfig const& config,
                                      std::vector<PromptSegments> const& infoModules,
                                      std::vector<PromptSegments> const& rightModules,
                                      tui::Theme const& theme)
{
    constexpr auto HorizontalMargin = 1;
    auto const canvasWidth = canvas.width();
    auto const contentWidth = canvasWidth - (2 * HorizontalMargin);

    // Background fill for both lines
    auto bgStyle = tui::Style {};
    bgStyle.bg = theme.promptColors.background;

    canvas.fill(
        tui::Rect { .x = HorizontalMargin, .y = 0, .width = contentWidth, .height = 1 }, ' ', bgStyle);
    canvas.fill(
        tui::Rect { .x = HorizontalMargin, .y = 1, .width = contentWidth, .height = 1 }, ' ', bgStyle);

    // Line 1: separator + modules
    auto col = HorizontalMargin;

    // Draw separator
    if (config.separator == SeparatorStyle::Bar)
    {
        auto sepStyle = tui::Style {};
        sepStyle.fg = theme.promptColors.separator;
        sepStyle.bg = theme.promptColors.background;
        col += canvas.putString(0, col, "\xe2\x96\x8e", sepStyle); // U+258E LEFT ONE QUARTER BLOCK
        canvas.put(0, col, " ", bgStyle);
        ++col;
    }
    else if (config.separator == SeparatorStyle::Rounded)
    {
        auto sepStyle = tui::Style {};
        sepStyle.fg = theme.promptColors.separator;
        sepStyle.bg = theme.promptColors.background;
        col += canvas.putString(0, col, "\xe2\x95\xad", sepStyle); // U+256D ╭
        col += canvas.putString(0, col, "\xe2\x94\x80", sepStyle); // U+2500 ─
        canvas.put(0, col, " ", bgStyle);
        ++col;
    }

    // Info modules with space separation (or │ for Rounded)
    for (std::size_t i = 0; i < infoModules.size(); ++i)
    {
        if (i > 0)
        {
            if (config.separator == SeparatorStyle::Rounded)
            {
                tui::Style dimPipeStyle;
                dimPipeStyle.fg = theme.promptColors.separator;
                dimPipeStyle.bg = theme.promptColors.background;
                dimPipeStyle.dim = true;
                canvas.put(0, col, " ", bgStyle);
                ++col;
                col += canvas.putString(0, col, "\xe2\x94\x82", dimPipeStyle); // U+2502 │
                canvas.put(0, col, " ", bgStyle);
                ++col;
            }
            else
            {
                canvas.put(0, col, " ", bgStyle);
                ++col;
            }
        }
        // Apply background to each segment
        for (auto seg: infoModules[i])
        {
            seg.style.bg = theme.promptColors.background;
            col += canvas.putString(0, col, seg.text, seg.style);
        }
    }

    // Right-aligned modules
    if (!rightModules.empty())
    {
        auto rightWidth = 0;
        for (auto const& mod: rightModules)
            rightWidth += segmentsWidth(mod) + 1; // +1 for space
        if (rightWidth > 0)
            --rightWidth; // Remove trailing space

        auto rightCol = canvasWidth - HorizontalMargin - rightWidth;
        if (rightCol > col + 2) // Ensure at least 2 chars gap
        {
            for (std::size_t i = 0; i < rightModules.size(); ++i)
            {
                if (i > 0)
                {
                    canvas.put(0, rightCol, " ", bgStyle);
                    ++rightCol;
                }
                for (auto seg: rightModules[i])
                {
                    seg.style.bg = theme.promptColors.background;
                    rightCol += canvas.putString(0, rightCol, seg.text, seg.style);
                }
            }
        }
    }

    // Line 2: separator + indicator
    col = HorizontalMargin;
    if (config.separator == SeparatorStyle::Bar)
    {
        auto sepStyle = tui::Style {};
        sepStyle.fg = theme.promptColors.separator;
        sepStyle.bg = theme.promptColors.background;
        col += canvas.putString(1, col, "\xe2\x96\x8e", sepStyle); // U+258E
        canvas.put(1, col, " ", bgStyle);
        ++col;
    }
    else if (config.separator == SeparatorStyle::Rounded)
    {
        auto sepStyle = tui::Style {};
        sepStyle.fg = theme.promptColors.separator;
        sepStyle.bg = theme.promptColors.background;
        col += canvas.putString(1, col, "\xe2\x95\xb0", sepStyle); // U+2570 ╰
        col += canvas.putString(1, col, "\xe2\x94\x80", sepStyle); // U+2500 ─
        canvas.put(1, col, " ", bgStyle);
        ++col;
    }

    return 2;
}

// =============================================================================
// Boxed layout: box-drawn frame around modules
// =============================================================================

int PromptLayoutEngine::renderBoxed(tui::Canvas& canvas,
                                    PromptConfig const& config,
                                    std::vector<PromptSegments> const& infoModules,
                                    tui::Theme const& theme)
{
    auto const canvasWidth = canvas.width();
    auto sepStyle = tui::Style {};
    sepStyle.fg = theme.promptColors.separator;

    // Top border: ┌──────...──┐
    auto topBorder = std::string("\xe2\x94\x8c"); // ┌
    for (int i = 1; i < canvasWidth - 1; ++i)
        topBorder += "\xe2\x94\x80"; // ─
    topBorder += "\xe2\x94\x90";     // ┐
    canvas.putString(0, 0, topBorder, sepStyle);

    // Content line: │ modules... │
    canvas.putString(1, 0, "\xe2\x94\x82", sepStyle); // │
    auto col = 2;
    for (std::size_t i = 0; i < infoModules.size(); ++i)
    {
        if (i > 0)
        {
            canvas.put(1, col, " ", tui::Style {});
            ++col;
        }
        col += renderSegments(canvas, 1, col, infoModules[i]);
    }
    canvas.putString(1, canvasWidth - 1, "\xe2\x94\x82", sepStyle); // │

    // Bottom border: └──────...──┘
    auto botBorder = std::string("\xe2\x94\x94"); // └
    for (int i = 1; i < canvasWidth - 1; ++i)
        botBorder += "\xe2\x94\x80"; // ─
    botBorder += "\xe2\x94\x98";     // ┘
    canvas.putString(2, 0, botBorder, sepStyle);

    // Indicator line (line 3)
    auto indicatorStyle = tui::Style {};
    indicatorStyle.fg = theme.promptColors.indicator;
    canvas.putString(3, 0, config.indicator, indicatorStyle);

    return 4;
}

// =============================================================================
// Powerline layout: segments with arrow separators
// =============================================================================

int PromptLayoutEngine::renderPowerline(tui::Canvas& canvas,
                                        PromptConfig const& config,
                                        std::vector<PromptSegments> const& infoModules,
                                        tui::Theme const& theme)
{
    auto col = 0;

    // Each module gets a background segment with powerline arrow between
    auto const moduleBg = theme.promptColors.background;

    for (std::size_t i = 0; i < infoModules.size(); ++i)
    {
        // Leading edge
        if (i == 0)
        {
            auto edgeStyle = tui::Style {};
            edgeStyle.fg = moduleBg;
            col += canvas.putString(0, col, "\xe2\x96\x8c", edgeStyle); // U+258C left half block
        }

        // Module content with background
        for (auto seg: infoModules[i])
        {
            seg.style.bg = moduleBg;
            col += canvas.putString(0, col, " " + seg.text + " ", seg.style);
        }

        // Powerline arrow separator
        if (i + 1 < infoModules.size())
        {
            auto arrowStyle = tui::Style {};
            arrowStyle.fg = moduleBg;
            col += canvas.putString(0, col, "\xee\x82\xb0", arrowStyle); // U+E0B0
        }
        else
        {
            auto arrowStyle = tui::Style {};
            arrowStyle.fg = moduleBg;
            col += canvas.putString(0, col, "\xee\x82\xb0", arrowStyle); // U+E0B0
        }
    }

    // Line 2: indicator
    auto indicatorStyle = tui::Style {};
    indicatorStyle.fg = theme.promptColors.indicator;
    canvas.putString(1, 0, config.indicator, indicatorStyle);

    return 2;
}

// =============================================================================
// Helpers
// =============================================================================

int PromptLayoutEngine::renderSegments(tui::Canvas& canvas, int row, int col, PromptSegments const& segments)
{
    auto totalWidth = 0;
    for (auto const& seg: segments)
    {
        auto const w = canvas.putString(row, col + totalWidth, seg.text, seg.style);
        totalWidth += w;
    }
    return totalWidth;
}

int PromptLayoutEngine::segmentsWidth(PromptSegments const& segments)
{
    auto total = 0;
    for (auto const& seg: segments)
        total += displayWidth(seg.text);
    return total;
}

int PromptLayoutEngine::displayWidth(std::string_view text)
{
    auto width = 0;
    auto segmenter = unicode::utf8_grapheme_segmenter(text);
    for (auto const& cluster: segmenter)
        width += tui::graphemeClusterWidth(cluster);
    return width;
}

} // namespace endo
