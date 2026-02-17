// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <expected>
#include <string>
#include <string_view>

#include <agent/Types.hpp>
#include <nlohmann/json.hpp>

namespace endo::agent
{

/// Error returned when a tool execution fails.
struct ToolError
{
    std::string message; ///< Human-readable error description.
};

/// Abstract interface for agent tools.
///
/// Each tool provides a name, a JSON Schema definition of its input,
/// and an execute method that processes the arguments and returns a result.
class AgentTool
{
  public:
    virtual ~AgentTool() = default;

    /// @brief Returns the unique name of this tool.
    [[nodiscard]] virtual auto name() const noexcept -> std::string_view = 0;

    /// @brief Returns the tool definition including JSON Schema for the input.
    [[nodiscard]] virtual auto definition() const -> ToolDefinition = 0;

    /// @brief Executes the tool with the given arguments.
    /// @param arguments JSON object matching the tool's input schema.
    /// @return The tool result, or an error.
    [[nodiscard]] virtual auto execute(nlohmann::json const& arguments)
        -> std::expected<ToolResult, ToolError> = 0;
};

} // namespace endo::agent
