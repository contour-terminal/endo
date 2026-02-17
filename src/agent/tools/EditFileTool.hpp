// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <agent/tools/AgentTool.hpp>

namespace endo::agent
{

/// Tool for performing exact string replacements in files.
///
/// Input: { path: string, old_string: string, new_string: string, replace_all?: bool }
/// - Reads the file, finds old_string, replaces with new_string.
/// - Fails if old_string is not found.
/// - Fails if old_string is ambiguous (>1 match) unless replace_all is true.
class EditFileTool final: public AgentTool
{
  public:
    [[nodiscard]] auto name() const noexcept -> std::string_view override;
    [[nodiscard]] auto definition() const -> ToolDefinition override;
    [[nodiscard]] auto execute(nlohmann::json const& arguments)
        -> std::expected<ToolResult, ToolError> override;
};

} // namespace endo::agent
