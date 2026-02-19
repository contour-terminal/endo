// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <agent/tools/AgentTool.hpp>

namespace endo::agent
{

/// Tool for listing directory contents.
///
/// Input: { path: string (required), show_hidden?: bool, long_format?: bool }
/// - path: Directory path to list.
/// - show_hidden: Include dotfiles (default: false).
/// - long_format: Show sizes, dates, and types (default: false).
///
/// Returns an alphabetically sorted listing with directories suffixed by '/'.
/// Capped at 1000 entries.
class ListDirectoryTool final: public AgentTool
{
  public:
    [[nodiscard]] auto name() const noexcept -> std::string_view override;
    [[nodiscard]] auto definition() const -> ToolDefinition override;
    [[nodiscard]] auto execute(nlohmann::json const& arguments)
        -> std::expected<ToolResult, ToolError> override;
};

} // namespace endo::agent
