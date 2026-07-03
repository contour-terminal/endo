// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <platform/ProcessProvider.hpp>

namespace endo::process_match
{

/// @brief Options controlling how running processes are matched against a pattern.
///
/// Shared by the pkill and pgrep builtins: both select processes by an
/// ECMAScript regular expression over ProcessEntry::command; only what they
/// do with the matches differs.
struct MatchOptions
{
    std::string pattern;                 ///< ECMAScript regex matched against ProcessEntry::command.
    bool exactMatch = false;             ///< Require a full (anchored) match instead of a substring search.
    bool caseInsensitive = false;        ///< Case-insensitive pattern matching.
    bool invert = false;                 ///< Select processes that do NOT match the pattern.
    std::vector<std::string> userFilter; ///< Only match processes owned by these users (empty: all users).
    bool newestOnly = false;             ///< Keep only the newest (highest PID) match.
    bool oldestOnly = false;             ///< Keep only the oldest (lowest PID) match.
    std::optional<int64_t> excludePid;   ///< PID excluded from matching (e.g. the shell itself).
};

/// @brief Splits a comma-separated list into individual tokens.
///
/// Empty entries are skipped. Used for `-u USER[,USER]` and `-o PID[,PID]`
/// style option values.
///
/// @param spec The comma-separated list (e.g. "alice,bob").
/// @return The individual tokens.
[[nodiscard]] std::vector<std::string> splitCommaList(std::string_view spec);

/// @brief Returns true if @p entry's owner is listed in @p users.
///
/// An empty @p users list disables the filter (match-all).
///
/// @param entry The process entry to test.
/// @param users The usernames to accept.
[[nodiscard]] bool matchesUserFilter(ProcessEntry const& entry, std::vector<std::string> const& users);

/// @brief Filters @p entries by the given match options.
///
/// The pattern is compiled as an ECMAScript regular expression and matched
/// against ProcessEntry::command — whatever the platform exposes most
/// reliably (argv[0] on Linux, the command string on Darwin/Windows).
/// With MatchOptions::exactMatch the pattern is anchored and must match the
/// whole field; otherwise a substring search is performed.
///
/// @param entries The process entries to filter.
/// @param opts The match options.
/// @return The matching entries in input order (reduced to a single entry
///         when newestOnly/oldestOnly is set), or an error message if the
///         pattern is not a valid regular expression.
[[nodiscard]] std::expected<std::vector<ProcessEntry>, std::string> filterProcesses(
    std::span<ProcessEntry const> entries, MatchOptions const& opts);

} // namespace endo::process_match
