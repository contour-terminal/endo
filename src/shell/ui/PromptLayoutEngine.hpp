// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <shell/ui/PromptConfig.hpp>
#include <shell/ui/PromptModule.hpp>

#include <vector>

namespace tui
{
class Canvas;
struct Theme;
} // namespace tui

namespace endo
{

/// @brief Layout engine that renders prompt modules into a Canvas.
///
/// Supports four layout kinds (SingleLine, TwoLine, Boxed, Powerline) and
/// various separator styles. Modules are evaluated externally and passed as
/// pre-computed segment lists.
class PromptLayoutEngine
{
  public:
    /// @brief Renders the prompt chrome (info line, separators, indicator) into a canvas.
    ///
    /// @param canvas The canvas to render into.
    /// @param config The prompt configuration.
    /// @param infoModules Evaluated segments for info-line modules.
    /// @param rightModules Evaluated segments for right-aligned modules.
    /// @param theme The current theme.
    /// @return The number of rows consumed by the prompt chrome.
    static int render(tui::Canvas& canvas,
                      PromptConfig const& config,
                      std::vector<PromptSegments> const& infoModules,
                      std::vector<PromptSegments> const& rightModules,
                      tui::Theme const& theme);

    /// @brief Returns the preferred height for the given layout configuration.
    /// @param config The prompt configuration.
    /// @return The number of rows the prompt chrome will consume.
    [[nodiscard]] static int preferredHeight(PromptConfig const& config);

  private:
    static int renderSingleLine(tui::Canvas& canvas,
                                PromptConfig const& config,
                                std::vector<PromptSegments> const& infoModules,
                                tui::Theme const& theme);

    static int renderTwoLine(tui::Canvas& canvas,
                             PromptConfig const& config,
                             std::vector<PromptSegments> const& infoModules,
                             std::vector<PromptSegments> const& rightModules,
                             tui::Theme const& theme);

    static int renderBoxed(tui::Canvas& canvas,
                           PromptConfig const& config,
                           std::vector<PromptSegments> const& infoModules,
                           tui::Theme const& theme);

    static int renderPowerline(tui::Canvas& canvas,
                               PromptConfig const& config,
                               std::vector<PromptSegments> const& infoModules,
                               tui::Theme const& theme);

    /// @brief Renders segments onto a canvas at the given position.
    /// @return The number of columns consumed.
    static int renderSegments(tui::Canvas& canvas, int row, int col, PromptSegments const& segments);

    /// @brief Calculates the display width of segments.
    [[nodiscard]] static int segmentsWidth(PromptSegments const& segments);

    /// @brief Calculates display width of a string.
    [[nodiscard]] static int displayWidth(std::string_view text);
};

} // namespace endo
