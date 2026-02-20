// SPDX-License-Identifier: Apache-2.0
#include <format>

#include <agent/session/AgentSession.hpp>
#include <agent/session/PlanExecutor.hpp>

namespace endo::agent
{

PlanExecutor::PlanExecutor(AgentSession& session, Plan plan): _session(session), _plan(std::move(plan))
{
}

auto PlanExecutor::executeNextStep(StreamCallback streamCb) -> std::expected<PlanStepStatus, AgentError>
{
    // Find the first pending step
    auto* nextStep = static_cast<PlanStep*>(nullptr);
    for (auto& step: _plan.steps)
    {
        if (step.status == PlanStepStatus::Pending)
        {
            nextStep = &step;
            break;
        }
    }

    if (!nextStep)
    {
        return std::unexpected(AgentError {
            .code = AgentErrorCode::ToolLoopExceeded,
            .message = "No pending steps remaining.",
        });
    }

    _currentStepIndex = nextStep->index;
    nextStep->status = PlanStepStatus::InProgress;

    auto const prompt = buildStepPrompt(*nextStep);
    auto result = _session.processMessage(prompt, std::move(streamCb));

    if (result.has_value())
    {
        nextStep->status = PlanStepStatus::Completed;
        return PlanStepStatus::Completed;
    }

    nextStep->status = PlanStepStatus::Failed;
    return std::unexpected(result.error());
}

auto PlanExecutor::plan() const -> Plan const&
{
    return _plan;
}

auto PlanExecutor::currentStepIndex() const noexcept -> size_t
{
    return _currentStepIndex;
}

auto PlanExecutor::isComplete() const noexcept -> bool
{
    for (auto const& step: _plan.steps)
    {
        if (step.status == PlanStepStatus::Pending || step.status == PlanStepStatus::InProgress)
            return false;
    }
    return true;
}

void PlanExecutor::skipStep(size_t index)
{
    if (index < _plan.steps.size())
        _plan.steps[index].status = PlanStepStatus::Skipped;
}

auto PlanExecutor::buildStepPrompt(PlanStep const& step) const -> std::string
{
    auto prompt = std::format("Execute step {}/{}: {}", step.index + 1, _plan.steps.size(), step.description);

    if (!step.filesTouched.empty())
    {
        prompt += "\nFiles: ";
        for (auto i = size_t { 0 }; i < step.filesTouched.size(); ++i)
        {
            if (i > 0)
                prompt += ", ";
            prompt += step.filesTouched[i];
        }
    }

    if (!step.rationale.empty())
        prompt += std::format("\nRationale: {}", step.rationale);

    return prompt;
}

} // namespace endo::agent
