// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>

#include <agent/Plan.hpp>
#include <agent/tools/AgentTool.hpp>

namespace endo::agent
{

/// Pseudo-tool the LLM calls to submit a structured plan during exploration.
///
/// The tool parses the JSON arguments into a Plan and stores it internally.
/// AgentSession checks for a parsed plan after executeToolCalls() returns
/// and breaks out of the exploration loop.
class SubmitPlanTool final: public AgentTool
{
  public:
    /// @brief Returns the tool name: "submit_plan".
    [[nodiscard]] auto name() const noexcept -> std::string_view override;

    /// @brief Returns the JSON Schema definition for plan submission.
    [[nodiscard]] auto definition() const -> ToolDefinition override;

    /// @brief Parses the JSON arguments into a Plan and stores it.
    /// @param arguments JSON matching the submit_plan schema.
    /// @return Success result, or error if JSON is malformed.
    [[nodiscard]] auto execute(nlohmann::json const& arguments)
        -> std::expected<ToolResult, ToolError> override;

    /// @brief Returns the last parsed plan, if any.
    [[nodiscard]] auto lastParsedPlan() const -> std::optional<Plan> const&;

    /// @brief Clears any previously parsed plan.
    void clearParsedPlan();

  private:
    std::optional<Plan> _lastParsedPlan;
};

} // namespace endo::agent
