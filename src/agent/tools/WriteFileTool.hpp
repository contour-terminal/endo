// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <agent/tools/AgentTool.hpp>

namespace endo::agent
{

/// Tool for writing or creating files.
///
/// Input: { path: string, content: string }
/// Creates parent directories as needed.
/// Returns confirmation with bytes written.
class WriteFileTool final: public AgentTool
{
  public:
    [[nodiscard]] auto name() const noexcept -> std::string_view override;
    [[nodiscard]] auto definition() const -> ToolDefinition override;
    [[nodiscard]] auto execute(nlohmann::json const& arguments)
        -> std::expected<ToolResult, ToolError> override;
};

} // namespace endo::agent
