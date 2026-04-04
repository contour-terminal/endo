// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <shell/ui/PromptModule.hpp>

#include <tui/TerminalOutput.hpp>

#include <span>
#include <string_view>

namespace endo
{

/// @brief Produces gradient-colored prompt segments for the given text.
///
/// Iterates grapheme clusters and interpolates RGB color linearly from
/// start to end color across the text span.
///
/// @param start The starting RGB color.
/// @param end The ending RGB color.
/// @param text The text to apply the gradient to.
/// @return A vector of PromptSegments, one per grapheme cluster.
[[nodiscard]] PromptSegments gradient(tui::RgbColor start, tui::RgbColor end, std::string_view text);

/// @brief Produces gradient-colored prompt segments for the given text using N color stops.
///
/// Iterates grapheme clusters and interpolates RGB color across the color stops
/// using multiStopGradient(). Falls back to solid color for single-stop spans.
///
/// @param stops The color stops, evenly spaced along the text.
/// @param text The text to apply the gradient to.
/// @return A vector of PromptSegments, one per grapheme cluster.
[[nodiscard]] PromptSegments gradient(std::span<tui::RgbColor const> stops, std::string_view text);

/// @brief Interpolates across evenly-spaced color stops at parameter t in [0, 1].
///
/// When stops is empty, returns black. When a single stop, returns that stop for any t.
/// Otherwise, scales t across (stops.size()-1) segments and interpolates the two
/// bracketing stops with lerpColor().
///
/// @param stops The color stops, evenly spaced.
/// @param t Interpolation parameter in [0, 1].
/// @return The interpolated color.
[[nodiscard]] tui::RgbColor multiStopGradient(std::span<tui::RgbColor const> stops, float t) noexcept;

} // namespace endo
