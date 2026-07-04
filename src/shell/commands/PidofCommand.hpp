// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <platform/ProcessProvider.hpp>

namespace endo::pidof_cmd
{

/// @brief Options parsed from pidof command-line arguments.
struct PidofOptions
{
    std::vector<std::string> programNames; ///< Program names to look up (at least one).
    std::vector<int64_t> omitPids;         ///< -o PID[,PID]: PIDs to exclude from the result.
    std::string separator = " ";           ///< -S/-d SEP: separator between printed PIDs.
    bool singleShot = false;               ///< -s: print at most one PID.
    bool quiet = false;                    ///< -q: print nothing, only set the exit status.
    bool showHelp = false;                 ///< -h / --help: show help text.
};

/// @brief How process names are compared against requested program names.
///
/// The native policy comes from the platform layer; see
/// platform::nativeProcessNameMatchPolicy().
using NameMatchPolicy = platform::ProcessNameMatchPolicy;

/// @brief Parses pidof command-line arguments.
///
/// Supports:
/// - `pidof PROGRAM...` (one or more program names)
/// - `pidof -s PROGRAM` (single shot: at most one PID)
/// - `pidof -q PROGRAM` (quiet: exit status only)
/// - `pidof -S SEP PROGRAM` / `pidof -d SEP PROGRAM` (output separator)
/// - `pidof -o PID[,PID] PROGRAM` (omit PIDs; repeatable)
/// - `pidof -h` / `pidof --help`
///
/// @param args The arguments to parse (excluding the "pidof" program name).
/// @return Parsed options, or an error message.
[[nodiscard]] std::expected<PidofOptions, std::string> parsePidofArgs(std::span<std::string const> args);

/// @brief Returns true if @p entry runs the program @p name.
///
/// Matches @p name against ProcessEntry::command verbatim and against its
/// path basename, applying the relaxations selected by @p policy.
///
/// @param entry The process entry to test.
/// @param name The requested program name.
/// @param policy Platform-dependent comparison relaxations.
[[nodiscard]] bool matchesProgramName(ProcessEntry const& entry,
                                      std::string_view name,
                                      NameMatchPolicy policy);

/// @brief Returns the PIDs of all entries matching any requested program name.
///
/// Applies PidofOptions::omitPids, sorts the result descending by PID
/// (newest first, like Linux pidof), and truncates it to a single PID when
/// PidofOptions::singleShot is set.
///
/// @param entries The process entries to search.
/// @param opts Parsed pidof options.
/// @param policy Platform-dependent name comparison relaxations.
[[nodiscard]] std::vector<int64_t> findPids(std::span<ProcessEntry const> entries,
                                            PidofOptions const& opts,
                                            NameMatchPolicy policy);

} // namespace endo::pidof_cmd
