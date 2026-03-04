// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace endo::timeout
{

/// Options parsed from timeout command-line arguments.
struct TimeoutOptions
{
    double durationSeconds = 0.0;     ///< Timeout duration in seconds
    int signal = 15;                  ///< Signal to send on timeout (default SIGTERM)
    double killAfterSeconds = 0.0;    ///< Grace period before SIGKILL (0 = disabled)
    bool preserveStatus = false;      ///< Return command's exit status on timeout
    bool foreground = false;          ///< Don't create separate process group
    bool verbose = false;             ///< Diagnose to stderr when signal sent
    bool showHelp = false;            ///< Show help text
    std::vector<std::string> command; ///< Sub-command and its arguments
};

/// Parses a duration string (e.g., "5", "1.5s", "2m", "1h", "0.5d").
///
/// @param str The duration string to parse.
/// @return The duration in seconds, or an error message.
[[nodiscard]] std::expected<double, std::string> parseDuration(std::string_view str);

/// Parses a signal specification (e.g., "TERM", "SIGKILL", "9", "15").
///
/// @param str The signal specification to parse.
/// @return The signal number, or an error message.
[[nodiscard]] std::expected<int, std::string> parseSignalSpec(std::string_view str);

/// Parses timeout command-line arguments.
///
/// @param args The arguments to parse (excluding the "timeout" program name).
/// @return Parsed options, or an error message.
[[nodiscard]] std::expected<TimeoutOptions, std::string> parseTimeoutArgs(std::span<std::string const> args);

} // namespace endo::timeout
