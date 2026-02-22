// SPDX-License-Identifier: Apache-2.0
#include "AgentWorker.hpp"

#include <agent/session/PlanExecutor.hpp>
#include <agent/tools/ToolRegistry.hpp>

namespace endo::agent
{

AgentWorker::AgentWorker(AgentSession& session, platform::MessageQueue<FromAgentMessage>& outbound):
    _session(session), _outbound(outbound)
{
    // Wire tool status callback to push ToolStatusMessage to outbound queue.
    _session.setToolStatusCallback(
        [this](ToolCall const& call) { _outbound.push(ToolStatusMessage { .call = call }); });
}

AgentWorker::~AgentWorker()
{
    stop();
}

void AgentWorker::start()
{
    // Reset queues in case they were shut down by a previous stop() call.
    _inbound.reset();
    _askUserResponses.reset();
    _permissionResponses.reset();
    _cancelled.store(false, std::memory_order_relaxed);
    _thread = std::jthread([this](std::stop_token st) { run(std::move(st)); });
}

void AgentWorker::stop()
{
    if (_thread.joinable())
    {
        _thread.request_stop();
        _inbound.shutdown();
        _askUserResponses.shutdown();
        _permissionResponses.shutdown();
        _thread.join();
    }
}

auto AgentWorker::makeAskUserCallback() -> AskUserCallback
{
    return [this](UserQuestion const& question) -> UserAnswer {
        auto const requestId = _nextRequestId.fetch_add(1, std::memory_order_relaxed);
        _outbound.push(AskUserRequest { .requestId = requestId, .question = question });

        // Block until the main thread provides an answer.
        // The run() loop is blocked inside handlePrompt() during tool execution,
        // so we must drain _inbound ourselves to route UserAnswerMessage → _askUserResponses.
        while (true)
        {
            // Drain _inbound: route responses to their queues, handle Cancel/Shutdown.
            while (auto inMsg = _inbound.tryPop())
            {
                std::visit(
                    [this](auto const& m) {
                        using MsgType = std::decay_t<decltype(m)>;
                        if constexpr (std::is_same_v<MsgType, CancelMessage>)
                            _cancelled.store(true, std::memory_order_relaxed);
                        else if constexpr (std::is_same_v<MsgType, UserAnswerMessage>)
                            _askUserResponses.push(m);
                        else if constexpr (std::is_same_v<MsgType, PermissionResponseMessage>)
                            _permissionResponses.push(m);
                        else if constexpr (std::is_same_v<MsgType, ShutdownMessage>)
                            _cancelled.store(true, std::memory_order_relaxed);
                        // PlanApproveMessage, UserPromptMessage: ignored during blocking waits.
                    },
                    *inMsg);
            }

            if (_cancelled.load(std::memory_order_relaxed))
                return UserAnswer { .cancelled = true };

            auto response = _askUserResponses.popFor(std::chrono::milliseconds(200));
            if (!response.has_value())
                continue;
            if (response->requestId == requestId)
                return response->answer;
            // Wrong request ID — shouldn't happen but handle gracefully.
        }
    };
}

auto AgentWorker::makePermissionCallback() -> PermissionPromptCallback
{
    return [this](PermissionPrompt const& prompt) -> PermissionDecision {
        auto const requestId = _nextRequestId.fetch_add(1, std::memory_order_relaxed);
        _outbound.push(PermissionRequest { .requestId = requestId, .prompt = prompt });

        // Block until the main thread provides a decision.
        // Same drain pattern as makeAskUserCallback().
        while (true)
        {
            // Drain _inbound: route responses to their queues, handle Cancel/Shutdown.
            while (auto inMsg = _inbound.tryPop())
            {
                std::visit(
                    [this](auto const& m) {
                        using MsgType = std::decay_t<decltype(m)>;
                        if constexpr (std::is_same_v<MsgType, CancelMessage>)
                            _cancelled.store(true, std::memory_order_relaxed);
                        else if constexpr (std::is_same_v<MsgType, UserAnswerMessage>)
                            _askUserResponses.push(m);
                        else if constexpr (std::is_same_v<MsgType, PermissionResponseMessage>)
                            _permissionResponses.push(m);
                        else if constexpr (std::is_same_v<MsgType, ShutdownMessage>)
                            _cancelled.store(true, std::memory_order_relaxed);
                        // PlanApproveMessage, UserPromptMessage: ignored during blocking waits.
                    },
                    *inMsg);
            }

            if (_cancelled.load(std::memory_order_relaxed))
                return PermissionDecision::Cancelled;

            auto response = _permissionResponses.popFor(std::chrono::milliseconds(200));
            if (!response.has_value())
                continue;
            if (response->requestId == requestId)
                return response->decision;
            // Wrong request ID — shouldn't happen but handle gracefully.
        }
    };
}

void AgentWorker::run(std::stop_token stopToken)
{
    while (!stopToken.stop_requested())
    {
        auto msg = _inbound.popFor(std::chrono::milliseconds(200));
        if (!msg.has_value())
            continue;

        std::visit(
            [&](auto const& m) {
                using MsgType = std::decay_t<decltype(m)>;
                if constexpr (std::is_same_v<MsgType, UserPromptMessage>)
                {
                    handlePrompt(m, stopToken);
                }
                else if constexpr (std::is_same_v<MsgType, CancelMessage>)
                {
                    _cancelled.store(true, std::memory_order_relaxed);
                }
                else if constexpr (std::is_same_v<MsgType, ShutdownMessage>)
                {
                    _inbound.shutdown();
                }
                else if constexpr (std::is_same_v<MsgType, PlanApproveMessage>)
                {
                    handlePlanExecution(m, stopToken);
                }
                else if constexpr (std::is_same_v<MsgType, UserAnswerMessage>)
                {
                    _askUserResponses.push(m);
                }
                else if constexpr (std::is_same_v<MsgType, PermissionResponseMessage>)
                {
                    _permissionResponses.push(m);
                }
            },
            *msg);
    }

    _outbound.push(AgentShutdownComplete {});
}

void AgentWorker::handlePrompt(UserPromptMessage const& msg, std::stop_token const& stopToken)
{
    _busy.store(true, std::memory_order_relaxed);
    _cancelled.store(false, std::memory_order_relaxed);

    _outbound.push(ThinkingStartMessage {});

    auto streamCb = makeStreamCallback(stopToken);

    auto const images = std::span<agent::ImageBlock const>(msg.images);

    if (msg.planMode)
    {
        auto planResult = _session.processMessageForPlan(msg.text, images, std::move(streamCb));
        if (planResult.has_value())
        {
            _outbound.push(PlanGeneratedMessage { .plan = std::move(*planResult) });
            _outbound.push(CompletionMessage { .success = true,
                                               .turnUsage = _session.lastTurnUsage(),
                                               .sessionUsage = _session.sessionUsage() });
        }
        else
        {
            _outbound.push(CompletionMessage { .success = false,
                                               .errorMessage = planResult.error().message,
                                               .sessionUsage = _session.sessionUsage() });
        }
    }
    else
    {
        auto result = _session.processMessage(msg.text, images, std::move(streamCb));
        if (result.has_value())
        {
            _outbound.push(CompletionMessage { .fullResponse = std::move(*result),
                                               .success = true,
                                               .turnUsage = _session.lastTurnUsage(),
                                               .sessionUsage = _session.sessionUsage() });
        }
        else
        {
            _outbound.push(CompletionMessage { .success = false,
                                               .errorMessage = result.error().message,
                                               .sessionUsage = _session.sessionUsage() });
        }
    }

    _busy.store(false, std::memory_order_relaxed);
}

auto AgentWorker::makeStreamCallback(std::stop_token const& stopToken) -> StreamCallback
{
    return [this, &stopToken](std::string_view token) -> bool {
        if (stopToken.stop_requested())
            return false;
        if (_cancelled.load(std::memory_order_relaxed))
            return false;

        // Drain _inbound: route responses to their queues, handle Cancel/Shutdown.
        while (auto inMsg = _inbound.tryPop())
        {
            std::visit(
                [this](auto const& m) {
                    using MsgType = std::decay_t<decltype(m)>;
                    if constexpr (std::is_same_v<MsgType, CancelMessage>)
                        _cancelled.store(true, std::memory_order_relaxed);
                    else if constexpr (std::is_same_v<MsgType, UserAnswerMessage>)
                        _askUserResponses.push(m);
                    else if constexpr (std::is_same_v<MsgType, PermissionResponseMessage>)
                        _permissionResponses.push(m);
                    else if constexpr (std::is_same_v<MsgType, ShutdownMessage>)
                        _cancelled.store(true, std::memory_order_relaxed);
                    // PlanApproveMessage, UserPromptMessage: ignored during streaming.
                },
                *inMsg);
        }

        if (_cancelled.load(std::memory_order_relaxed))
            return false;

        _outbound.push(TokenMessage { .token = std::string(token) });
        return true;
    };
}

void AgentWorker::handlePlanExecution(PlanApproveMessage const& msg, std::stop_token const& stopToken)
{
    _busy.store(true, std::memory_order_relaxed);
    _cancelled.store(false, std::memory_order_relaxed);

    // Optionally compact conversation before execution to free context window space.
    if (msg.compactFirst)
    {
        auto compactResult = _session.forceCompaction();
        // Log but don't abort on compaction failure.
        (void) compactResult;
    }

    auto executor = PlanExecutor(_session, msg.plan);
    auto const totalSteps = executor.plan().steps.size();

    while (!executor.isComplete() && !_cancelled.load(std::memory_order_relaxed)
           && !stopToken.stop_requested())
    {
        auto const stepIndex = executor.currentStepIndex();
        auto const& step = executor.plan().steps[stepIndex];

        _outbound.push(PlanStepStartMessage {
            .stepIndex = stepIndex,
            .totalSteps = totalSteps,
            .description = step.description,
        });

        auto streamCb = makeStreamCallback(stopToken);
        auto stepResult = executor.executeNextStep(std::move(streamCb));

        if (stepResult.has_value())
        {
            _outbound.push(PlanStepCompleteMessage {
                .stepIndex = stepIndex,
                .status = *stepResult,
            });
        }
        else
        {
            _outbound.push(PlanStepCompleteMessage {
                .stepIndex = stepIndex,
                .status = PlanStepStatus::Failed,
                .errorMessage = stepResult.error().message,
            });
            // Stop execution on failure — remaining steps stay Pending.
            break;
        }
    }

    // If cancelled, skip remaining pending steps.
    if (_cancelled.load(std::memory_order_relaxed))
    {
        for (auto i = size_t { 0 }; i < executor.plan().steps.size(); ++i)
        {
            if (executor.plan().steps[i].status == PlanStepStatus::Pending)
                executor.skipStep(i);
        }
    }

    // Check if all steps succeeded.
    auto allSucceeded = true;
    for (auto const& step: executor.plan().steps)
    {
        if (step.status != PlanStepStatus::Completed)
        {
            allSucceeded = false;
            break;
        }
    }

    _outbound.push(PlanCompleteMessage {
        .plan = executor.plan(),
        .allSucceeded = allSucceeded,
    });

    _busy.store(false, std::memory_order_relaxed);
}

} // namespace endo::agent
