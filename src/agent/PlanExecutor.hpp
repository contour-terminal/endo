// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <expected>
#include <string>

#include <agent/Plan.hpp>
#include <agent/Types.hpp>

namespace endo::agent
{

class AgentSession;
struct AgentError;

/// Drives step-by-step execution of an approved plan.
///
/// After the user approves a plan, PlanExecutor iterates through each step,
/// delegating execution to the AgentSession with full tool access.
class PlanExecutor
{
  public:
    /// @brief Constructs an executor for the given session and plan.
    /// @param session The agent session to use for executing steps.
    /// @param plan The approved plan to execute.
    PlanExecutor(AgentSession& session, Plan plan);

    /// @brief Executes the next pending step.
    ///
    /// Finds the first Pending step, sets it to InProgress, calls
    /// processMessage() on the session with a step-specific prompt,
    /// and sets the step to Completed or Failed based on the result.
    /// @param streamCb Optional callback for streaming tokens.
    /// @return The resulting step status, or an error.
    [[nodiscard]] auto executeNextStep(StreamCallback streamCb) -> std::expected<PlanStepStatus, AgentError>;

    /// @brief Returns the current plan (read-only).
    [[nodiscard]] auto plan() const -> Plan const&;

    /// @brief Returns the index of the step currently being executed.
    [[nodiscard]] auto currentStepIndex() const noexcept -> size_t;

    /// @brief Returns true when all steps are completed, failed, or skipped.
    [[nodiscard]] auto isComplete() const noexcept -> bool;

    /// @brief Marks a step as skipped.
    /// @param index The zero-based step index to skip.
    void skipStep(size_t index);

  private:
    /// Builds a prompt for the LLM to execute a specific step.
    [[nodiscard]] auto buildStepPrompt(PlanStep const& step) const -> std::string;

    AgentSession& _session;
    Plan _plan;
    size_t _currentStepIndex = 0;
};

} // namespace endo::agent
