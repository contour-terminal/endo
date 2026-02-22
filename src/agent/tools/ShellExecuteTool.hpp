// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <chrono>
#include <functional>
#include <string>

#include <agent/tools/AgentTool.hpp>

namespace endo::agent
{

/// Result of executing a shell command.
struct ShellExecResult
{
    std::string output;    ///< Combined stdout and stderr.
    int exitCode = 0;      ///< Process exit code.
    bool timedOut = false; ///< Whether the command timed out.
};

/// Callback type for executing shell commands.
/// @param command The command string to execute.
/// @param timeout Maximum execution time.
/// @return The command execution result.
using ShellExecuteCallback =
    std::function<ShellExecResult(std::string const& command, std::chrono::milliseconds timeout)>;

/// Tool for executing shell commands.
///
/// Input: { command: string, timeout_ms?: int }
/// - command: The shell command to execute.
/// - timeout_ms: Maximum execution time in milliseconds (default: 120000, max: 600000).
///
/// Delegates execution to a callback provided at construction time.
/// Output is truncated at 30KB.
class ShellExecuteTool final: public AgentTool
{
  public:
    /// @brief Constructs a shell execution tool with the given callback.
    /// @param executeCallback Callback that performs the actual command execution.
    explicit ShellExecuteTool(ShellExecuteCallback executeCallback);

    [[nodiscard]] auto name() const noexcept -> std::string_view override;
    [[nodiscard]] auto definition() const -> ToolDefinition override;
    [[nodiscard]] auto execute(nlohmann::json const& arguments)
        -> std::expected<ToolResult, ToolError> override;
    [[nodiscard]] auto classifyRisk(nlohmann::json const& arguments) const -> ToolRisk override;

  private:
    ShellExecuteCallback _executeCallback;
};

} // namespace endo::agent
