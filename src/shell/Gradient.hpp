// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <shell/PromptModule.hpp>

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

/// @brief Linearly interpolates between two RGB colors.
///
/// @param a The start color.
/// @param b The end color.
/// @param t Interpolation parameter in [0, 1].
/// @return The interpolated color.
[[nodiscard]] constexpr tui::RgbColor lerpColor(tui::RgbColor a, tui::RgbColor b, float t) noexcept
{
    auto const lerp = [](std::uint8_t x, std::uint8_t y, float s) noexcept -> std::uint8_t {
        auto const val = static_cast<float>(x) + (static_cast<float>(y) - static_cast<float>(x)) * s;
        return static_cast<std::uint8_t>(val < 0.0f ? 0.0f : (val > 255.0f ? 255.0f : val));
    };
    return { .r = lerp(a.r, b.r, t), .g = lerp(a.g, b.g, t), .b = lerp(a.b, b.b, t) };
}

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
