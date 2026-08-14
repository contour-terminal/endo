// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tui/TerminalOutput.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace endo
{

/// @brief Layout style for the prompt.
enum class PromptLayoutKind : std::uint8_t
{
    SingleLine, ///< All modules and indicator on a single line.
    TwoLine,    ///< Info line on top, indicator on bottom line.
    Boxed,      ///< Box-drawn frame around modules.
    Powerline,  ///< Powerline-style segments with arrow separators.
};

/// @brief Separator style between prompt sections or lines.
enum class SeparatorStyle : std::uint8_t
{
    None,      ///< No separator.
    Bar,       ///< Left quarter block (U+258E) vertical bar.
    Powerline, ///< Powerline arrow (U+E0B0) between segments.
    Rounded,   ///< Rounded corners (U+256D/U+2570) with horizontal lines.
    Boxed,     ///< Box drawing characters (U+250C/U+2514/U+2500/U+2502).
};

/// @brief Transient prompt mode for scrollback compression.
enum class TransientMode : std::uint8_t
{
    Off,     ///< No transient prompt (keep full prompt in scrollback).
    Minimal, ///< Replace with minimal indicator after command execution.
    Arrow,   ///< Replace with simple arrow indicator.
};

/// @brief A color specification: solid color or multi-stop gradient.
///
/// When a single color is provided, the text is rendered with that solid color.
/// When multiple colors are provided, the text is rendered with a gradient
/// interpolated across the color stops.
struct ColorSpec
{
    std::vector<tui::RgbColor> colors; ///< 1 color = solid, 2+ = gradient stops.

    /// @brief Returns whether this color spec represents a gradient.
    [[nodiscard]] bool isGradient() const noexcept { return colors.size() > 1; }

    /// @brief Returns the solid color (first stop), or black if empty.
    [[nodiscard]] tui::RgbColor solid() const noexcept
    {
        return colors.empty() ? tui::RgbColor {} : colors.front();
    }
};

/// @brief Optional per-color overrides for prompt theming.
///
/// Each field, when set, overrides the corresponding value from the theme's
/// PromptColorPalette. When unset (nullopt), the theme default applies.
/// Color specs may be solid colors or gradients (multi-stop).
struct PromptColorOverrides
{
    std::optional<ColorSpec> path;           ///< Path module text color.
    std::optional<ColorSpec> gitClean;       ///< Git branch name when clean.
    std::optional<ColorSpec> gitDirty;       ///< Git branch name when dirty.
    std::optional<ColorSpec> gitStaged;      ///< Git indicator when staged.
    std::optional<ColorSpec> indicator;      ///< Input line indicator.
    std::optional<ColorSpec> indicatorError; ///< Indicator color when last command failed.
    std::optional<ColorSpec> exitCode;       ///< Exit code badge color.
    std::optional<ColorSpec> duration;       ///< Duration badge color.
    std::optional<ColorSpec> hostname;       ///< Hostname text color.
    std::optional<ColorSpec> username;       ///< Username text color.
    std::optional<ColorSpec>
        background; ///< Prompt background (solid only; gradient bg uses auroraBackground).
    std::optional<ColorSpec> separator; ///< Separator/bar color.
    std::optional<ColorSpec> badge;     ///< Badge background color.
    std::optional<ColorSpec> badgeText; ///< Badge text color.
    std::optional<ColorSpec> clock;     ///< Clock text color.
    bool transparentBackground = false; ///< When true, background = terminal default (no bg color emitted).

    /// @brief Per-color dynamic resolvers (F# function names, without the "fsharp." prefix).
    ///
    /// Keys are the public color names (e.g. "path", "indicator", "git_clean"). At each
    /// prompt render, PromptComponent invokes the named function; the returned string is
    /// parsed via `parseColorSpec` and overrides the corresponding ColorSpec field for
    /// that frame only. This map is orthogonal to the static ColorSpec fields: assigning
    /// a string to a color property clears its entry here, and assigning a function sets it.
    std::unordered_map<std::string, std::string> colorFns;
};

/// @brief Configuration for prompt rendering.
///
/// Specifies which modules to display, the layout style, separator style,
/// and other visual options. Can be populated from presets or user configuration.
struct PromptConfig
{
    std::string_view name;                               ///< Optional name of the preset or config.
    PromptLayoutKind layout = PromptLayoutKind::TwoLine; ///< Layout style.
    SeparatorStyle separator = SeparatorStyle::Bar;      ///< Separator style.
    TransientMode transient = TransientMode::Off;        ///< Transient prompt mode.
    std::string indicator = "> ";                        ///< Indicator character(s) on the input line.
    /// Optional name of a user-defined F# function () -> string that produces the indicator
    /// dynamically at each prompt render. When set, overrides the static `indicator` string.
    /// The name is stored without any "fsharp." compiler prefix.
    std::optional<std::string> indicatorFn;
    std::vector<std::string> infoLineModules = { "path", "git" }; ///< Modules for the info line.
    std::vector<std::string> rightPromptModules;                  ///< Modules for the right-aligned section.
    int promptSpacing = 1;              ///< Number of blank lines above and below the prompt (0 or 1).
    int64_t durationThresholdMs = 2000; ///< Min duration (ms) to show duration module.
    std::vector<tui::RgbColor> auroraBackground; ///< Multi-stop background gradient (empty = flat bg).
    bool enableSixelFade = false;                ///< Enable sixel aurora fade above prompt.
    int64_t exitConfirmTimeoutMs =
        1000; ///< Timeout (ms) for double Ctrl+D exit confirmation (0 = immediate exit).
    PromptColorOverrides colorOverrides; ///< Per-color overrides (empty = use theme defaults).
};

} // namespace endo
