// SPDX-License-Identifier: Apache-2.0

#include <shell/completion/CompletionAdapter.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

using endo::applyFuzzyScoring;
using endo::CompletionCandidate;
using endo::PrefixRanking;

namespace
{

CompletionCandidate cand(std::string text)
{
    return CompletionCandidate { .text = std::move(text) };
}

/// Score of the item whose text equals @p text, or -1 if absent.
int scoreOf(std::vector<tui::CompletionItem> const& items, std::string_view text)
{
    auto const it = std::ranges::find(items, text, &tui::CompletionItem::text);
    return it == items.end() ? -1 : it->score;
}

} // namespace

TEST_CASE("applyFuzzyScoring.tiered_prefix_outranks_fuzzy")
{
    // Completing "plasma-" against a list where a long hyphenated name is a scattered
    // subsequence match (p·l·a·s·m·a·-) but only "plasma-desktop" is a genuine prefix
    // match. Tiered ranking (the default) must place the prefix match strictly above the
    // fuzzy one, regardless of the fuzzy match's accumulated run/word-start bonuses.
    std::vector<CompletionCandidate> candidates {
        cand("perl-Lingua-Stem-Snowball-Da"), // subsequence match for "plasma-"
        cand("plasma-desktop"),               // genuine prefix match
    };

    auto const results = applyFuzzyScoring(candidates, "plasma-", 60);

    auto const prefixScore = scoreOf(results, "plasma-desktop");
    auto const fuzzyScore = scoreOf(results, "perl-Lingua-Stem-Snowball-Da");
    REQUIRE(prefixScore >= 0);
    // The fuzzy candidate may be filtered out entirely; if present it must rank below.
    if (fuzzyScore >= 0)
        CHECK(prefixScore > fuzzyScore);
}

TEST_CASE("applyFuzzyScoring.additive_lets_caller_bonus_reorder")
{
    // Additive ranking keeps prefix and fuzzy matches on the same numeric scale, so a
    // caller's post-hoc bonus (modeling CommandCompleter's history recency) can promote a
    // frequently-used fuzzy match above a never-used prefix match. With the Tiered mode
    // the +100000 tier offset would make that impossible.
    std::vector<CompletionCandidate> candidates {
        cand("grep"),    // prefix match for "gr"
        cand("gnu-tar"), // fuzzy: g·r as a scattered subsequence (g[nu-ta]r), not a prefix
    };

    auto tiered = applyFuzzyScoring(candidates, "gr", 100, PrefixRanking::Tiered);
    auto additive = applyFuzzyScoring(candidates, "gr", 100, PrefixRanking::Additive);

    // Both candidates must be present in both modes for the comparison to be meaningful.
    REQUIRE(scoreOf(tiered, "grep") >= 0);
    REQUIRE(scoreOf(tiered, "gnu-tar") >= 0);
    REQUIRE(scoreOf(additive, "grep") >= 0);
    REQUIRE(scoreOf(additive, "gnu-tar") >= 0);

    // Under Tiered, the prefix match "grep" outranks the fuzzy "gnu-tar" by a margin no
    // realistic recency bonus (<= a few hundred) could close.
    CHECK(scoreOf(tiered, "grep") - scoreOf(tiered, "gnu-tar") > 1000);

    // Under Additive the gap is small, so a modest recency bonus applied to the fuzzy
    // match can overtake the prefix match — the behavior CommandCompleter relies on.
    constexpr auto RecencyBonus = 200;
    CHECK(scoreOf(additive, "gnu-tar") + RecencyBonus > scoreOf(additive, "grep"));
}

TEST_CASE("applyFuzzyScoring.empty_prefix_preserves_relative_order")
{
    // With an empty prefix every candidate matches and gets the same tier treatment, so
    // relative ordering must be stable (alphabetical after the equal scores).
    std::vector<CompletionCandidate> candidates { cand("bravo"), cand("alpha"), cand("charlie") };

    auto const results = applyFuzzyScoring(candidates, "", 50);

    REQUIRE(results.size() == 3);
    CHECK(scoreOf(results, "alpha") == scoreOf(results, "bravo"));
    CHECK(scoreOf(results, "bravo") == scoreOf(results, "charlie"));
}
