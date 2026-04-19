// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <expected>
#include <span>
#include <string>
#include <vector>

namespace endo::pkill_cmd
{

/// Options parsed from pkill command-line arguments.
struct PkillOptions
{
    int signal = 15;                     ///< Signal to send (default SIGTERM)
    std::string pattern;                 ///< Regex pattern to match against process name / command line
    std::vector<std::string> userFilter; ///< -u USER[,USER]: only match processes owned by these users
    bool fullMatch = false;              ///< -f: match against full command line instead of short name
    bool exactMatch = false;             ///< -x: require full (anchored) match, not substring
    bool caseInsensitive = false;        ///< -i: case-insensitive pattern match
    bool countOnly = false;              ///< -c: print count of matched processes
    bool listOnly = false;               ///< -l: list matches (pid name) and do not signal
    bool newestOnly = false;             ///< -n: match only the newest (highest PID) process
    bool oldestOnly = false;             ///< -o: match only the oldest (lowest PID) process
    bool showHelp = false;               ///< -h / --help: show help text
};

/// Parses pkill command-line arguments.
///
/// Supports:
/// - `pkill PATTERN` (default SIGTERM)
/// - `pkill -SIGNAL PATTERN` (e.g., `-9`, `-TERM`, `-SIGKILL`)
/// - `pkill -s SIGNAL PATTERN` (POSIX style)
/// - `pkill -f PATTERN` (match full command line)
/// - `pkill -x PATTERN` (exact anchored match)
/// - `pkill -i PATTERN` (case-insensitive)
/// - `pkill -c PATTERN` (print count and still signal)
/// - `pkill -l PATTERN` (list matches without signalling)
/// - `pkill -n PATTERN` (newest-only) / `-o PATTERN` (oldest-only)
/// - `pkill -u USER[,USER] PATTERN` (restrict to owners)
/// - `pkill -h` / `pkill --help`
///
/// @param args The arguments to parse (excluding the "pkill" program name).
/// @return Parsed options, or an error message.
[[nodiscard]] std::expected<PkillOptions, std::string> parsePkillArgs(std::span<std::string const> args);

} // namespace endo::pkill_cmd
