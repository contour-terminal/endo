// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string_view>

#include <shell/PromptModule.hpp>
#include <tui/TerminalOutput.hpp>

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

} // namespace endo
