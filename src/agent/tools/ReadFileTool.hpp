// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <agent/tools/AgentTool.hpp>

namespace endo::agent
{

/// Tool for reading file contents with optional line range.
///
/// Input: { path: string, offset?: int, limit?: int }
/// - path: Absolute or relative file path to read.
/// - offset: 1-based line offset to start reading from (default: 1).
/// - limit: Maximum number of lines to read (default: 2000).
///
/// Output: File contents with `cat -n` style line numbers.
/// Lines longer than 2000 characters are truncated.
class ReadFileTool final: public AgentTool
{
  public:
    [[nodiscard]] auto name() const noexcept -> std::string_view override;
    [[nodiscard]] auto definition() const -> ToolDefinition override;
    [[nodiscard]] auto execute(nlohmann::json const& arguments)
        -> std::expected<ToolResult, ToolError> override;
};

} // namespace endo::agent
