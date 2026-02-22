// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file CommandSafetyAnalyzer.hpp
/// @brief Static analysis of shell command strings for risk classification.

#include <span>
#include <string>
#include <string_view>

#include <agent/PermissionManager.hpp>

namespace endo::agent
{

/// Result of analyzing a shell command for safety.
struct CommandAnalysis
{
    ToolRisk risk;      ///< The classified risk level.
    std::string reason; ///< Human-readable explanation of the classification.
    bool isInteractive; ///< Whether the command requires interactive terminal input.
};

/// Stateless analyzer for classifying shell command risk.
///
/// Examines a command string against known pattern lists to determine
/// whether it is read-only, mutating, destructive, or interactive (blocked).
/// For piped/chained commands, returns the highest risk segment.
class CommandSafetyAnalyzer
{
  public:
    /// @brief Classifies a shell command's risk level.
    /// @param command The shell command string to analyze.
    /// @param extraBlockedPatterns Additional patterns to treat as blocked.
    /// @return The analysis result with risk level and explanation.
    [[nodiscard]] static auto classify(std::string_view command,
                                       std::span<std::string const> extraBlockedPatterns = {})
        -> CommandAnalysis;

    /// @brief Checks if a command requires interactive terminal input.
    /// @param command The shell command string to check.
    /// @return True if the command is interactive (e.g. vim, top).
    [[nodiscard]] static auto isInteractive(std::string_view command) -> bool;
};

} // namespace endo::agent
