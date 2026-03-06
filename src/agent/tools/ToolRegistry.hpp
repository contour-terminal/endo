// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <functional>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <agent/Types.hpp>
#include <agent/tools/AgentTool.hpp>

namespace endo::agent
{

/// Registry for agent tools.
///
/// Manages a collection of tools by name. Provides lookup and dispatch
/// for tool calls received from the LLM.
/// Predicate for filtering tools by name.
using ToolFilter = std::function<bool(std::string_view toolName)>;

class ToolRegistry
{
  public:
    /// @brief Registers a tool. Overwrites any existing tool with the same name.
    /// @param tool The tool to register.
    void registerTool(std::unique_ptr<AgentTool> tool);

    /// @brief Unregisters a tool by name.
    /// @param name The tool name to remove.
    /// @return true if the tool was found and removed, false if not found.
    auto unregisterTool(std::string_view name) -> bool;

    /// @brief Finds a tool by name.
    /// @param name The tool name to search for.
    /// @return Pointer to the tool, or nullptr if not found.
    [[nodiscard]] auto findTool(std::string_view name) const -> AgentTool*;

    /// @brief Returns tool definitions for all registered tools.
    [[nodiscard]] auto definitions() const -> std::vector<ToolDefinition>;

    /// @brief Returns tool definitions for tools matching the filter predicate.
    /// @param filter Predicate that returns true for tool names to include.
    [[nodiscard]] auto definitions(ToolFilter const& filter) const -> std::vector<ToolDefinition>;

    /// @brief Executes a tool call by dispatching to the appropriate tool.
    ///
    /// If the tool is not found, returns a ToolResult with isError=true.
    /// If the tool execution fails, returns a ToolResult with the error message.
    /// @param call The tool call to execute.
    /// @return The tool result (never throws).
    [[nodiscard]] auto execute(ToolCall const& call) const -> ToolResult;

    /// @brief Returns the number of registered tools.
    [[nodiscard]] auto size() const noexcept -> size_t;

  private:
    std::vector<std::unique_ptr<AgentTool>> _tools;
    std::unordered_map<std::string, size_t> _nameIndex; ///< Maps tool name to index in _tools.
};

} // namespace endo::agent
