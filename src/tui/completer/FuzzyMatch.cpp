// SPDX-License-Identifier: Apache-2.0
#include <tui/completer/FuzzyMatch.hpp>
#include <tui/completer/SmartCaseMatch.hpp>

#if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wold-style-cast"
#endif
#include <libunicode/utf8_grapheme_segmenter.h>
#if defined(__clang__)
    #pragma clang diagnostic pop
#endif

#include <algorithm>
#include <cctype>
#include <climits>
#include <optional>
#include <ranges>
#include <string>
#include <vector>

namespace tui
{

namespace
{
    /// @brief Case-fold a grapheme cluster for comparison.
    /// TODO: Replace with unicode::casefold() when available in libunicode.
    /// Currently uses ASCII-only tolower() as a fallback.
    /// This would need to handle cases like:
    /// - ß -> ss (German sharp s)
    /// - İ -> i (Turkish dotted I)
    /// - Σ -> σ (Greek sigma)
    std::string casefoldGrapheme(std::string_view grapheme)
    {
        std::string result;
        result.reserve(grapheme.size());
        for (char c: grapheme)
            result += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return result;
    }

    /// @brief Segment text into grapheme clusters.
    /// @param text The UTF-8 text to segment.
    /// @return Vector of grapheme cluster strings.
    std::vector<std::string> segmentGraphemes(std::string_view text)
    {
        std::vector<std::string> graphemes;
        auto segmenter = unicode::utf8_grapheme_segmenter(text);

        for (auto it = segmenter.begin(); it != segmenter.end(); ++it)
        {
            auto nextIt = it;
            ++nextIt;
            char const* start = it._clusterStart;
            char const* end =
                (nextIt != segmenter.end()) ? nextIt._clusterStart : (text.data() + text.size());
            graphemes.emplace_back(start, static_cast<size_t>(end - start));
        }
        return graphemes;
    }

    /// @brief Get byte offsets of each grapheme cluster start.
    std::vector<size_t> getGraphemeByteOffsets(std::string_view text)
    {
        std::vector<size_t> offsets;
        auto segmenter = unicode::utf8_grapheme_segmenter(text);

        for (auto it = segmenter.begin(); it != segmenter.end(); ++it)
        {
            offsets.push_back(static_cast<size_t>(it._clusterStart - text.data()));
        }
        return offsets;
    }

    /// @brief Compare two grapheme clusters with case sensitivity option.
    bool graphemesEqual(std::string_view a, std::string_view b, bool caseSensitive)
    {
        if (caseSensitive)
            return a == b;
        return casefoldGrapheme(a) == casefoldGrapheme(b);
    }

} // namespace

double FuzzyMatchResult::quality(size_t graphemeCount) const noexcept
{
    if (graphemeCount == 0 || !matches)
        return 0.0;
    return static_cast<double>(matchedChars) / static_cast<double>(graphemeCount);
}

bool FuzzyMatchResult::isContiguousSubstring() const noexcept
{
    // match() reports a contiguous block whenever the pattern occurs verbatim, so
    // the greedy subsequence path can only ever yield longestRun < matchedChars.
    // A full-length run therefore uniquely identifies a substring match.
    return matches && matchedChars > 0 && longestRun == matchedChars;
}

bool FuzzyMatch::isWordBoundary(char c) noexcept
{
    // Simple ASCII word boundaries.
    // TODO: Consider extending to Unicode word boundaries per UAX#29
    // (Unicode Text Segmentation). This would require integrating with
    // libunicode's word boundary detection or implementing WB rules.
    switch (c)
    {
        case '/':
        case '_':
        case '-':
        case ' ':
        case '\t':
        case '.': return true;
        default: return false;
    }
}

bool FuzzyMatch::isWordStart(std::string_view text,
                             size_t graphemeIndex,
                             std::vector<size_t> const& graphemeByteOffsets) noexcept
{
    // First grapheme is always a word start
    if (graphemeIndex == 0)
        return true;

    // Check if grapheme index is valid
    if (graphemeIndex >= graphemeByteOffsets.size())
        return false;

    // Get byte offset of this grapheme
    size_t byteOffset = graphemeByteOffsets[graphemeIndex];
    if (byteOffset == 0)
        return true;

    // Check if preceding character is a word boundary
    char prevChar = text[byteOffset - 1];
    return isWordBoundary(prevChar);
}

size_t FuzzyMatch::countGraphemes(std::string_view text) noexcept
{
    size_t count = 0;
    auto segmenter = unicode::utf8_grapheme_segmenter(text);
    for (auto it = segmenter.begin(); it != segmenter.end(); ++it)
        ++count;
    return count;
}

FuzzyMatchResult FuzzyMatch::match(std::string_view text,
                                   std::string_view pattern,
                                   bool caseSensitive) noexcept
{
    FuzzyMatchResult result;

    if (pattern.empty())
    {
        result.matches = true;
        return result;
    }

    if (text.empty())
        return result;

    auto textGraphemes = segmentGraphemes(text);
    auto patternGraphemes = segmentGraphemes(pattern);
    auto graphemeByteOffsets = getGraphemeByteOffsets(text);

    result.textGraphemeCount = textGraphemes.size();
    result.patternGraphemeCount = patternGraphemes.size();

    if (patternGraphemes.size() > textGraphemes.size())
        return result;

    // Substring-first matching: prefer a contiguous occurrence of the pattern
    // over a greedy subsequence.
    //
    // The greedy matcher below binds each pattern grapheme to its first available
    // occurrence, which scatters the match when an earlier occurrence of the
    // leading grapheme exists — e.g. matching "endo.exe" against
    // "./build/clangcl-debug/src/shell/endo.exe" would bind the leading 'e' to
    // "...clangcl-debug..." and highlight a stray 'e' plus "ndo.exe". When the
    // pattern occurs verbatim we instead report that contiguous block: it
    // highlights the run the user actually typed and ranks ahead of a scattered
    // match (single run, word-start aware). All fuzzy consumers — history search,
    // ghost text, completion, the fuzzy file finder — share this behavior.
    //
    // Case-fold each grapheme once rather than per comparison so the scan stays
    // cheap on the hot completion/history path — O(textLen + patternLen) folds
    // instead of O(textLen * patternLen). Case-sensitive matching needs no folds.
    auto const substringStart = [&]() -> std::optional<size_t> {
        auto const locate = [](std::vector<std::string> const& haystack,
                               std::vector<std::string> const& needle) -> std::optional<size_t> {
            auto const found = std::ranges::search(haystack, needle);
            if (found.empty())
                return std::nullopt;
            return static_cast<size_t>(std::ranges::distance(haystack.begin(), found.begin()));
        };
        if (caseSensitive)
            return locate(textGraphemes, patternGraphemes);
        auto const fold = [](std::vector<std::string> const& graphemes) {
            auto folded = std::vector<std::string> {};
            folded.reserve(graphemes.size());
            for (auto const& grapheme: graphemes)
                folded.push_back(casefoldGrapheme(grapheme));
            return folded;
        };
        return locate(fold(textGraphemes), fold(patternGraphemes));
    }();

    if (substringStart.has_value())
    {
        for (auto const offset: std::views::iota(size_t { 0 }, patternGraphemes.size()))
        {
            auto const textIdx = *substringStart + offset;
            result.positions.push_back(textIdx);
            if (isWordStart(text, textIdx, graphemeByteOffsets))
                result.wordStartMatches++;
        }
        result.matchedChars = patternGraphemes.size();
        result.longestRun = patternGraphemes.size();
        result.consecutiveRuns = 1;
        result.matches = true;
        return result;
    }

    size_t patternIdx = 0;
    size_t lastMatchIdx = SIZE_MAX;
    size_t currentRunLength = 0;

    for (size_t textIdx = 0; textIdx < textGraphemes.size() && patternIdx < patternGraphemes.size();
         ++textIdx)
    {
        if (graphemesEqual(textGraphemes[textIdx], patternGraphemes[patternIdx], caseSensitive))
        {
            result.positions.push_back(textIdx);
            result.matchedChars++;

            // Track consecutive runs
            if (lastMatchIdx != SIZE_MAX && textIdx == lastMatchIdx + 1)
            {
                currentRunLength++;
            }
            else
            {
                if (currentRunLength > 0)
                    result.consecutiveRuns++;
                currentRunLength = 1;
            }
            result.longestRun = std::max(result.longestRun, currentRunLength);

            // Track word start matches
            if (isWordStart(text, textIdx, graphemeByteOffsets))
                result.wordStartMatches++;

            lastMatchIdx = textIdx;
            patternIdx++;
        }
    }

    if (currentRunLength > 0)
        result.consecutiveRuns++;

    result.matches = (patternIdx == patternGraphemes.size());
    return result;
}

FuzzyMatchResult FuzzyMatch::matchSmartCase(std::string_view text, std::string_view pattern) noexcept
{
    bool caseSensitive = SmartCaseMatch::hasUppercase(pattern);
    return match(text, pattern, caseSensitive);
}

int FuzzyMatch::calculateScore(int baseScore,
                               [[maybe_unused]] std::string_view text,
                               [[maybe_unused]] std::string_view pattern,
                               FuzzyMatchResult const& result,
                               FuzzyConfig const& config) noexcept
{
    if (!result.matches)
        return baseScore;

    auto const textLen = result.textGraphemeCount;
    auto const patternLen = result.patternGraphemeCount;

    // Match percentage bonus: pattern coverage of text
    double matchPercent = textLen > 0 ? static_cast<double>(patternLen) / static_cast<double>(textLen) : 0.0;
    int percentBonus = static_cast<int>(matchPercent * config.maxMatchPercentBonus);

    // Consecutive run bonus
    int consecutiveScore = static_cast<int>(result.consecutiveRuns) * config.consecutiveBonus;

    // Word start bonus
    int wordStartScore = static_cast<int>(result.wordStartMatches) * config.wordStartBonus;

    // Note: We intentionally do NOT add prefixMatchBonus here for fuzzy matches.
    // The prefixMatchBonus is reserved for actual prefix matches (handled by the caller).
    // Fuzzy matches starting at position 0 already benefit from wordStartBonus.

    return baseScore + percentBonus + consecutiveScore + wordStartScore;
}

} // namespace tui
