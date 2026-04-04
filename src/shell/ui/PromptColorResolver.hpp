// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <shell/ui/PromptConfig.hpp>

#include <tui/Theme.hpp>

#include <optional>
#include <string>
#include <string_view>

namespace endo
{

/// @brief Resolved prompt colors — merges color overrides with theme defaults.
///
/// Each field is a fully resolved ColorSpec (solid or gradient). The background
/// field uses tui::Color which may be std::monostate for transparent backgrounds.
struct ResolvedPromptColors
{
    ColorSpec path;           ///< Path module text color.
    ColorSpec gitClean;       ///< Git branch name when clean.
    ColorSpec gitDirty;       ///< Git branch name when dirty.
    ColorSpec gitStaged;      ///< Git indicator when staged.
    ColorSpec indicator;      ///< Input line indicator.
    ColorSpec indicatorError; ///< Indicator color when last command failed.
    ColorSpec exitCode;       ///< Exit code badge color.
    ColorSpec duration;       ///< Duration badge color.
    ColorSpec hostname;       ///< Hostname text color.
    tui::Color background;    ///< Prompt background (may be monostate for transparent).
    ColorSpec separator;      ///< Separator/bar color.
    ColorSpec badge;          ///< Badge background color.
    ColorSpec badgeText;      ///< Badge text color.
    ColorSpec clock;          ///< Clock text color.
};

/// @brief Resolves effective prompt colors by merging config overrides with theme defaults.
///
/// For each color field: if the override is set, it takes priority; otherwise the
/// theme default is wrapped in a single-color ColorSpec.
/// For background: transparentBackground flag produces std::monostate (no bg emitted),
/// otherwise the override color or theme default is used.
///
/// @param overrides The per-color overrides from PromptConfig.
/// @param themeColors The theme's prompt color palette.
/// @return Fully resolved colors ready for rendering.
[[nodiscard]] ResolvedPromptColors resolvePromptColors(PromptColorOverrides const& overrides,
                                                       tui::Theme::PromptColorPalette const& themeColors);

/// @brief Parses a color spec string into a ColorSpec.
///
/// Accepts:
/// - "#RRGGBB" — solid color
/// - "0xRRGGBB" — solid color (alternative hex format)
/// - "#RRGGBB:#RRGGBB:..." — gradient with colon-separated color stops
///
/// @param str The color spec string to parse.
/// @return The parsed ColorSpec, or std::nullopt on parse failure.
[[nodiscard]] std::optional<ColorSpec> parseColorSpec(std::string_view str);

/// @brief Formats a ColorSpec as a string representation.
///
/// Single color: "#RRGGBB"
/// Gradient: "#RRGGBB:#RRGGBB:..."
///
/// @param spec The color spec to format.
/// @return String representation.
[[nodiscard]] std::string formatColorSpec(ColorSpec const& spec);

} // namespace endo
