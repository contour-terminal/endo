// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace endo
{

/// Matches a filename against a shell glob pattern (supports *, ?, []).
[[nodiscard]] bool globMatchFilename(std::string_view filename, std::string_view pattern);

/// Expands a glob pattern to matching file paths. Returns empty if no matches.
[[nodiscard]] std::vector<std::string> expandGlobPattern(std::string_view pattern);

/// Expands a recursive glob pattern (containing **) to matching file paths.
[[nodiscard]] std::vector<std::string> expandRecursiveGlob(std::string_view pattern);

/// Matches text against a shell glob pattern (for parameter expansion).
[[nodiscard]] bool globMatch(std::string_view text, std::string_view pattern);

/// Finds all prefix lengths of text that match the pattern.
[[nodiscard]] std::vector<size_t> findPrefixMatches(std::string_view text, std::string_view pattern);

/// Finds all suffix start positions of text that match the pattern.
[[nodiscard]] std::vector<size_t> findSuffixMatches(std::string_view text, std::string_view pattern);

/// Finds the length of the first match of the pattern at the start of text.
[[nodiscard]] std::optional<size_t> findPatternMatchLength(std::string_view text, std::string_view pattern);

} // namespace endo
