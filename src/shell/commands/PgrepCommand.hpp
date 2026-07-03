// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <expected>
#include <span>
#include <string>
#include <vector>

namespace endo::pgrep_cmd
{

/// @brief Options parsed from pgrep command-line arguments.
struct PgrepOptions
{
    std::string pattern;                 ///< Regex pattern to match against process name / command line.
    std::vector<std::string> userFilter; ///< -u USER[,USER]: only match processes owned by these users.
    std::string delimiter = "\n";        ///< -d DELIM: delimiter between printed PIDs.
    bool fullMatch = false;              ///< -f: match against full command line instead of short name.
    bool exactMatch = false;             ///< -x: require full (anchored) match, not substring.
    bool caseInsensitive = false;        ///< -i: case-insensitive pattern match.
    bool invert = false;                 ///< -v: select processes that do NOT match the pattern.
    bool countOnly = false;              ///< -c: print only the count of matched processes.
    bool listName = false;               ///< -l: print the process name along with the PID.
    bool newestOnly = false;             ///< -n: match only the newest (highest PID) process.
    bool oldestOnly = false;             ///< -o: match only the oldest (lowest PID) process.
    bool showHelp = false;               ///< -h / --help: show help text.
};

/// @brief Parses pgrep command-line arguments.
///
/// Supports:
/// - `pgrep PATTERN` (print PIDs of matching processes)
/// - `pgrep -f PATTERN` (match full command line)
/// - `pgrep -x PATTERN` (exact anchored match)
/// - `pgrep -i PATTERN` (case-insensitive)
/// - `pgrep -v PATTERN` (invert the match)
/// - `pgrep -c PATTERN` (print only the count of matches)
/// - `pgrep -l PATTERN` (print `pid name` per match)
/// - `pgrep -n PATTERN` (newest-only) / `-o PATTERN` (oldest-only)
/// - `pgrep -u USER[,USER] PATTERN` (restrict to owners)
/// - `pgrep -d DELIM PATTERN` (output delimiter, default newline)
/// - `pgrep -h` / `pgrep --help`
///
/// @param args The arguments to parse (excluding the "pgrep" program name).
/// @return Parsed options, or an error message.
[[nodiscard]] std::expected<PgrepOptions, std::string> parsePgrepArgs(std::span<std::string const> args);

} // namespace endo::pgrep_cmd
