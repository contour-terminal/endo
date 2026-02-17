// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace endo::agent
{

/// Status of an individual plan step during execution.
enum class PlanStepStatus : uint8_t
{
    Pending,    ///< Step has not started yet.
    InProgress, ///< Step is currently being executed.
    Completed,  ///< Step completed successfully.
    Failed,     ///< Step execution failed.
    Skipped,    ///< Step was skipped by the user.
};

/// A single step in an agent plan.
struct PlanStep
{
    size_t index = 0;                                ///< Zero-based step index.
    std::string description;                         ///< What this step will do.
    std::vector<std::string> filesTouched;           ///< Files this step will read or modify.
    std::string rationale;                           ///< Why this step is needed.
    std::vector<size_t> dependsOn;                   ///< Indices of steps this step depends on.
    PlanStepStatus status = PlanStepStatus::Pending; ///< Current execution status.
};

/// A structured plan produced by the agent during exploration.
struct Plan
{
    std::string summary;                   ///< High-level summary of the plan.
    std::vector<PlanStep> steps;           ///< Ordered list of steps to execute.
    std::string riskAssessment;            ///< Assessment of risks involved.
    std::vector<std::string> alternatives; ///< Alternative approaches considered.
};

} // namespace endo::agent
