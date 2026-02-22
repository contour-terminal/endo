// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <expected>
#include <string>
#include <string_view>

#include <agent/PermissionManager.hpp>
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
/// Tools also declare their risk level for the permission system.
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

    /// @brief Returns the default risk level for this tool.
    ///
    /// Override in subclasses. Default is Mutating.
    [[nodiscard]] virtual auto riskLevel() const noexcept -> ToolRisk { return ToolRisk::Mutating; }

    /// @brief Classifies the risk of a specific invocation based on the arguments.
    ///
    /// Override for tools with dynamic risk (e.g. shell_execute, git).
    /// Default implementation returns riskLevel().
    /// @param arguments The tool call arguments.
    [[nodiscard]] virtual auto classifyRisk(nlohmann::json const& /*arguments*/) const -> ToolRisk
    {
        return riskLevel();
    }
};

} // namespace endo::agent
