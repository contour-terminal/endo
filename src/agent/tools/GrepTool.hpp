// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <agent/tools/AgentTool.hpp>

namespace endo::agent
{

/// Tool for searching file contents using regular expressions.
///
/// Input: { pattern: string, path?: string, glob?: string, context?: int }
/// - pattern: Regex pattern to search for.
/// - path: Directory to search in (default: current working directory).
/// - glob: Glob pattern to filter files (e.g. "*.cpp").
/// - context: Number of context lines before and after each match (default: 0).
///
/// Returns matches in ripgrep-style format: path:line_number: content.
/// Skips binary files. Limited to 500 matches.
class GrepTool final: public AgentTool
{
  public:
    [[nodiscard]] auto name() const noexcept -> std::string_view override;
    [[nodiscard]] auto definition() const -> ToolDefinition override;
    [[nodiscard]] auto execute(nlohmann::json const& arguments)
        -> std::expected<ToolResult, ToolError> override;
};

} // namespace endo::agent
