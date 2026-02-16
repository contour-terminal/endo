// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tui/TerminalOutput.hpp>

#include <cstdint>
#include <string>
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
    std::vector<std::string> infoLineModules = { "path", "git" }; ///< Modules for the info line.
    std::vector<std::string> rightPromptModules;                  ///< Modules for the right-aligned section.
    int promptSpacing = 1;              ///< Number of blank lines above and below the prompt (0 or 1).
    int64_t durationThresholdMs = 2000; ///< Min duration (ms) to show duration module.
    bool useGradientPath = false;       ///< Enable gradient coloring for path module.
    tui::RgbColor gradientStart {};     ///< Gradient start color.
    tui::RgbColor gradientEnd {};       ///< Gradient end color.
    std::vector<tui::RgbColor> auroraBackground {}; ///< Multi-stop background gradient (empty = flat bg).
    bool enableSixelFade = false;                   ///< Enable sixel aurora fade above prompt.
};

} // namespace endo
