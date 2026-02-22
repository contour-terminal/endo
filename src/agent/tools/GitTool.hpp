// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <agent/tools/AgentTool.hpp>
#include <agent/tools/ShellExecuteTool.hpp>

namespace endo::agent
{

/// Tool for executing git operations with safety guardrails.
///
/// Input: { subcommand: string, args?: [string] }
/// - subcommand: The git subcommand (e.g. "status", "diff", "log").
/// - args: Additional arguments for the subcommand.
///
/// Read-only subcommands are auto-approved. Dangerous patterns
/// (push --force, reset --hard, clean -f) are blocked.
class GitTool final: public AgentTool
{
  public:
    /// @brief Constructs a git tool using the given shell execution callback.
    /// @param executeCallback Callback that executes shell commands.
    explicit GitTool(ShellExecuteCallback executeCallback);

    [[nodiscard]] auto name() const noexcept -> std::string_view override;
    [[nodiscard]] auto definition() const -> ToolDefinition override;
    [[nodiscard]] auto execute(nlohmann::json const& arguments)
        -> std::expected<ToolResult, ToolError> override;
    [[nodiscard]] auto classifyRisk(nlohmann::json const& arguments) const -> ToolRisk override;

  private:
    ShellExecuteCallback _executeCallback;
};

} // namespace endo::agent
