// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tui/TerminalOutput.hpp>

#include <string>

namespace tui
{

/// Builds a complete SGR (Select Graphic Rendition) escape sequence for the given style.
///
/// Encodes bold, dim, italic, underline (with extended styles), inverse, strikethrough,
/// foreground/background colors (256-color and truecolor), and underline color (SGR 58).
/// Returns an empty string if the style is fully default.
///
/// Underline color is emitted as a separate SGR sequence to avoid exceeding the
/// 16-element parameter array limit in some terminals.
///
/// @param style The visual style to encode.
/// @return The SGR escape sequence string, or empty if no attributes are set.
[[nodiscard]] std::string buildSgrSequence(Style const& style);

} // namespace tui
