// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <endo-language/ide/CompletionItem.hpp>

#include <tui/completer/CompletionItem.hpp>

#include <cstdint>
#include <string_view>
#include <vector>

namespace endo
{

/// @brief Whether prefix matches are lifted into a dedicated score tier above every
///        fuzzy (subsequence) match.
enum class PrefixRanking : std::uint8_t
{
    /// Prefix matches get a large tier offset so they always outrank fuzzy matches,
    /// regardless of the per-run/word-start bonuses fuzzy scoring accumulates. Correct
    /// for enumerated candidate sets (package lists, spec options, bindings) where a
    /// scattered subsequence hit on a long name must never displace a shorter prefix
    /// match under the result cap.
    Tiered,

    /// Prefix matches get only the small additive @c prefixMatchBonus, staying on the
    /// same numeric scale as fuzzy matches. Use when the caller adds its own post-hoc
    /// bonus (e.g. history recency in CommandCompleter) that must be able to reorder
    /// prefix and fuzzy matches relative to each other.
    Additive,
};

/// @brief Applies fuzzy scoring to CompletionCandidate items and converts to tui::CompletionItem.
///
/// This bridges the shared completion engine (which returns unscored CompletionCandidate)
/// with the shell's UI (which requires scored tui::CompletionItem with fuzzy match positions).
///
/// @param candidates The unscored candidates from the shared completion engine.
/// @param prefix The current prefix for fuzzy matching.
/// @param baseScore Base score for scoring (higher = more relevant category).
/// @param prefixRanking Whether prefix matches are tiered above fuzzy matches (default)
///                      or kept additive so a caller's own bonus can reorder them.
/// @return Scored tui::CompletionItem list, filtered to only matching items.
[[nodiscard]] std::vector<tui::CompletionItem> applyFuzzyScoring(
    std::vector<CompletionCandidate> const& candidates,
    std::string_view prefix,
    int baseScore = 50,
    PrefixRanking prefixRanking = PrefixRanking::Tiered);

} // namespace endo
