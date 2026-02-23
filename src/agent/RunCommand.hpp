// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file RunCommand.hpp
/// @brief Argument parsing for the `endo agent run` subcommand.

#include <cstddef>
#include <expected>
#include <optional>
#include <span>
#include <string>

namespace endo::agent
{

/// Parsed options for `endo agent run`.
struct AgentRunOptions
{
    std::string prompt;                  ///< The user prompt text.
    bool jsonOutput = false;             ///< Emit structured JSON to stdout.
    size_t maxTurns = 25;                ///< Max tool loop iterations.
    bool autoApprove = false;            ///< Skip permission prompts (TrustAll).
    std::optional<std::string> provider; ///< Override active provider.
    std::optional<std::string> model;    ///< Override model.
};

/// Parses arguments after "endo agent run".
/// @param args Arguments starting after "run" (e.g., {"--json", "prompt text"}).
/// @return Parsed options, or error string.
[[nodiscard]] auto parseAgentRunArgs(std::span<char const* const> args)
    -> std::expected<AgentRunOptions, std::string>;

} // namespace endo::agent
