// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <platform/GlobMatch.hpp>

namespace endo
{

/// Expands a glob pattern to matching file paths. Returns empty if no matches.
[[nodiscard]] std::vector<std::string> expandGlobPattern(std::string_view pattern);

/// Expands a recursive glob pattern (containing **) to matching file paths.
[[nodiscard]] std::vector<std::string> expandRecursiveGlob(std::string_view pattern);

/// Matches text against a shell glob pattern (for parameter expansion).
[[nodiscard]] inline bool globMatch(std::string_view text, std::string_view pattern)
{
    auto ti = size_t { 0 };
    auto pi = size_t { 0 };
    auto starIdx = std::string_view::npos;
    auto matchIdx = size_t { 0 };

    while (ti < text.size())
    {
        if (pi < pattern.size() && (pattern[pi] == '?' || pattern[pi] == text[ti]))
        {
            ++ti;
            ++pi;
        }
        else if (pi < pattern.size() && pattern[pi] == '*')
        {
            starIdx = pi;
            matchIdx = ti;
            ++pi;
        }
        else if (starIdx != std::string_view::npos)
        {
            pi = starIdx + 1;
            ++matchIdx;
            ti = matchIdx;
        }
        else
        {
            return false;
        }
    }

    while (pi < pattern.size() && pattern[pi] == '*')
        ++pi;

    return pi == pattern.size();
}

/// Finds all prefix lengths of text that match the pattern.
[[nodiscard]] std::vector<size_t> findPrefixMatches(std::string_view text, std::string_view pattern);

/// Finds all suffix start positions of text that match the pattern.
[[nodiscard]] std::vector<size_t> findSuffixMatches(std::string_view text, std::string_view pattern);

/// Finds the length of the first match of the pattern at the start of text.
[[nodiscard]] std::optional<size_t> findPatternMatchLength(std::string_view text, std::string_view pattern);

} // namespace endo
