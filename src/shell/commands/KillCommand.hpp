// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <expected>
#include <span>
#include <string>
#include <vector>

namespace endo::kill_cmd
{

/// Options parsed from kill command-line arguments.
struct KillOptions
{
    int signal = 15;                  ///< Signal to send (default SIGTERM)
    bool listSignals = false;         ///< -l: list available signal names
    bool showHelp = false;            ///< -h/--help: show help text
    std::vector<std::string> targets; ///< PIDs or %job_ids
};

/// Parses kill command-line arguments.
///
/// Supports:
/// - `kill PID...` (default SIGTERM)
/// - `kill -SIGNAL PID...` (numeric or named: -9, -TERM, -SIGKILL)
/// - `kill -s SIGNAL PID...` (POSIX style)
/// - `kill %N` (job ID)
/// - `kill -l` (list signals)
/// - `kill -h` / `kill --help`
///
/// @param args The arguments to parse (excluding the "kill" program name).
/// @return Parsed options, or an error message.
[[nodiscard]] std::expected<KillOptions, std::string> parseKillArgs(std::span<std::string const> args);

} // namespace endo::kill_cmd
