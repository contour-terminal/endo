// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tui/TerminalOutput.hpp>

#include <cstdint>
#include <string>
#include <string_view>

namespace endo
{

/// Describes the visual decoration for a file entry in table output.
struct FileDecoration
{
    std::string_view icon; ///< Nerd Font glyph (UTF-8 encoded, single character).
    tui::Style style;      ///< SGR style for the name cell (foreground color, bold, dim, etc.).
};

/// Returns the visual decoration (icon and color) for a file based on its properties.
///
/// Priority chain: directory > executable > extension match > fallback.
/// Hidden files (names starting with '.') additionally get the dim attribute.
///
/// @param name  File name (used for extension matching and hidden-file detection).
/// @param isDir Whether the file is a directory.
/// @param mode  POSIX permission bits (0777 mask); used to detect executables.
/// @return FileDecoration with an icon glyph and an SGR style.
[[nodiscard]] FileDecoration getFileDecoration(std::string_view name, bool isDir, int64_t mode);

/// Builds an SGR escape sequence string for the given style.
///
/// Returns an empty string if the style has no attributes set (all defaults).
/// The returned string includes the leading ESC[ and trailing 'm'.
///
/// @param style The terminal style to encode.
/// @return SGR escape sequence string (e.g., "\033[1;38;2;92;122;255m").
[[nodiscard]] std::string sgrSequence(tui::Style const& style);

} // namespace endo
