// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <agent/tools/AgentTool.hpp>

namespace endo::agent
{

/// Tool for file pattern matching using glob patterns.
///
/// Input: { pattern: string, path?: string }
/// - pattern: Glob pattern supporting *, **, and ? wildcards.
/// - path: Base directory to search in (default: current working directory).
///
/// Returns matching file paths sorted by modification time (newest first),
/// limited to 1000 results.
class GlobTool final: public AgentTool
{
  public:
    [[nodiscard]] auto name() const noexcept -> std::string_view override;
    [[nodiscard]] auto definition() const -> ToolDefinition override;
    [[nodiscard]] auto execute(nlohmann::json const& arguments)
        -> std::expected<ToolResult, ToolError> override;

    [[nodiscard]] auto riskLevel() const noexcept -> ToolRisk override { return ToolRisk::ReadOnly; }
};

} // namespace endo::agent
