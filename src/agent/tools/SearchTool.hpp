// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <agent/tools/AgentTool.hpp>

namespace endo::agent
{

/// Unified search tool combining file finding (glob) and content search (regex).
///
/// Modes:
/// - "files":              Find files matching a glob pattern.
/// - "content":            Search file contents with regex, showing matching lines.
/// - "files_with_matches": List files containing at least one match.
/// - "count":              Count matches per file, sorted by count descending.
///
/// @param pattern   Required. Glob pattern (files mode) or regex (content modes).
/// @param mode      Output mode (default: "content").
/// @param path      Base directory to search in (default: cwd).
/// @param glob      Filename filter glob for content modes (e.g. "*.cpp").
/// @param type      File type shorthand (e.g. "cpp", "py", "js", "rust").
/// @param case_insensitive  Case-insensitive regex matching (default: false).
/// @param context_before    Lines before each match in content mode (default: 0).
/// @param context_after     Lines after each match in content mode (default: 0).
/// @param context           Symmetric context lines, overridden by before/after.
/// @param multiline         Cross-line regex matching (default: false).
/// @param limit             Max result entries (default: 500 content, 1000 files).
/// @param offset            Skip first N entries (default: 0).
class SearchTool final: public AgentTool
{
  public:
    [[nodiscard]] auto name() const noexcept -> std::string_view override;
    [[nodiscard]] auto definition() const -> ToolDefinition override;
    [[nodiscard]] auto execute(nlohmann::json const& arguments)
        -> std::expected<ToolResult, ToolError> override;
};

} // namespace endo::agent
