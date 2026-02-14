// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <endo-language/CompletionItem.hpp>

#include <tui/completer/CompletionItem.hpp>

#include <string_view>
#include <vector>

namespace endo
{

/// @brief Applies fuzzy scoring to CompletionCandidate items and converts to tui::CompletionItem.
///
/// This bridges the shared completion engine (which returns unscored CompletionCandidate)
/// with the shell's UI (which requires scored tui::CompletionItem with fuzzy match positions).
///
/// @param candidates The unscored candidates from the shared completion engine.
/// @param prefix The current prefix for fuzzy matching.
/// @param baseScore Base score for scoring (higher = more relevant category).
/// @return Scored tui::CompletionItem list, filtered to only matching items.
[[nodiscard]] std::vector<tui::CompletionItem> applyFuzzyScoring(
    std::vector<CompletionCandidate> const& candidates, std::string_view prefix, int baseScore = 50);

} // namespace endo
