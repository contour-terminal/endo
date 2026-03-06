// SPDX-License-Identifier: Apache-2.0
#include "History.hpp"

#include <tui/completer/FuzzyMatch.hpp>
#include <tui/completer/SmartCaseMatch.hpp>

#include <algorithm>

namespace endo
{

InMemoryHistory::InMemoryHistory(size_t maxSize): _maxSize(maxSize)
{
    _entries.reserve(std::min(maxSize, size_t { 256 }));
}

void InMemoryHistory::add(std::string entry)
{
    trimInPlace(entry);
    if (entry.empty())
        return;

    // Don't add duplicates of the most recent entry
    if (!_entries.empty() && _entries.back() == entry)
        return;

    // Remove oldest entries if at capacity
    if (_entries.size() >= _maxSize)
        _entries.erase(_entries.begin());

    _entries.push_back(std::move(entry));
}

std::vector<std::string> const& InMemoryHistory::entries() const
{
    return _entries;
}

size_t InMemoryHistory::size() const
{
    return _entries.size();
}

size_t InMemoryHistory::maxSize() const
{
    return _maxSize;
}

void InMemoryHistory::clear()
{
    _entries.clear();
}

std::vector<std::string_view> InMemoryHistory::search(std::string_view prefix, size_t maxResults) const
{
    std::vector<std::string_view> results;
    results.reserve(std::min(maxResults, _entries.size()));

    // Search from newest to oldest (reverse order)
    for (auto it = _entries.rbegin(); it != _entries.rend() && results.size() < maxResults; ++it)
    {
        if (tui::SmartCaseMatch::matchesPrefix(*it, prefix))
        {
            // Avoid duplicates in results
            bool isDuplicate = false;
            for (auto const& existing: results)
            {
                if (existing == *it)
                {
                    isDuplicate = true;
                    break;
                }
            }
            if (!isDuplicate)
                results.emplace_back(*it);
        }
    }

    return results;
}

std::vector<History::FuzzySearchResult> InMemoryHistory::searchFuzzy(std::string_view prefix,
                                                                     size_t maxResults) const
{
    std::vector<FuzzySearchResult> results;
    results.reserve(std::min(maxResults * 2, _entries.size())); // Reserve extra for sorting

    tui::FuzzyConfig fuzzyConfig;
    double const minThreshold = fuzzyConfig.minMatchThreshold;

    // Track seen entries to avoid duplicates
    std::vector<std::string_view> seen;
    seen.reserve(std::min(maxResults * 2, _entries.size()));

    // First pass: collect all matches from newest to oldest
    int recencyBonus = static_cast<int>(_entries.size());
    for (auto it = _entries.rbegin(); it != _entries.rend(); ++it, --recencyBonus)
    {
        // Check for duplicates
        bool isDuplicate = false;
        for (auto const& existing: seen)
        {
            if (existing == *it)
            {
                isDuplicate = true;
                break;
            }
        }
        if (isDuplicate)
            continue;
        seen.emplace_back(*it);

        // Check prefix match first
        bool isPrefixMatch = tui::SmartCaseMatch::matchesPrefix(*it, prefix);
        tui::FuzzyMatchResult fuzzyResult;
        bool isFuzzyMatch = false;

        if (!isPrefixMatch && !prefix.empty())
        {
            fuzzyResult = tui::FuzzyMatch::matchSmartCase(*it, prefix);
            size_t textLen = tui::FuzzyMatch::countGraphemes(*it);
            isFuzzyMatch =
                fuzzyResult.matches
                && (fuzzyResult.quality(textLen) >= minThreshold || fuzzyResult.isContiguousSubstring());
        }

        if (!isPrefixMatch && !isFuzzyMatch)
            continue;

        auto score = 0;
        std::vector<size_t> matchPositions;

        if (isPrefixMatch)
        {
            score = tui::SmartCaseMatch::adjustScore(100, *it, prefix);
            score += fuzzyConfig.prefixMatchBonus + recencyBonus; // Prefix + recency bonus
        }
        else
        {
            score = tui::FuzzyMatch::calculateScore(50, *it, prefix, fuzzyResult, fuzzyConfig);
            score += recencyBonus; // Recency bonus
            matchPositions = std::move(fuzzyResult.positions);
        }

        results.push_back(FuzzySearchResult { .entry = *it,
                                              .positions = std::move(matchPositions),
                                              .score = score,
                                              .isPrefixMatch = isPrefixMatch });
    }

    // Sort by score descending
    std::ranges::sort(results, [](auto const& a, auto const& b) { return a.score > b.score; });

    // Trim to maxResults
    if (results.size() > maxResults)
        results.resize(maxResults);

    return results;
}

} // namespace endo
