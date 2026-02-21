// SPDX-License-Identifier: Apache-2.0
#include "AgentWorker.hpp"

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
            // Drain _inbound: route UserAnswerMessage, handle Cancel/Shutdown.
            while (auto inMsg = _inbound.tryPop())
            {
                std::visit(
                    [this](auto const& m) {
                        using MsgType = std::decay_t<decltype(m)>;
                        if constexpr (std::is_same_v<MsgType, CancelMessage>)
                            _cancelled.store(true, std::memory_order_relaxed);
                        else if constexpr (std::is_same_v<MsgType, UserAnswerMessage>)
                            _askUserResponses.push(m);
                        else if constexpr (std::is_same_v<MsgType, ShutdownMessage>)
                            _cancelled.store(true, std::memory_order_relaxed);
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
                else if constexpr (std::is_same_v<MsgType, UserAnswerMessage>)
                {
                    _askUserResponses.push(m);
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

    // Capture session usage before processing to compute per-turn delta.
    auto const usageBefore = _session.sessionUsage();

    if (msg.planMode)
    {
        auto planResult = _session.processMessageForPlan(msg.text, std::move(streamCb));
        if (planResult.has_value())
        {
            _outbound.push(PlanGeneratedMessage { .plan = std::move(*planResult) });
            auto turnDelta = _session.sessionUsage();
            turnDelta.inputTokens -= usageBefore.inputTokens;
            turnDelta.outputTokens -= usageBefore.outputTokens;
            turnDelta.cacheReadTokens -= usageBefore.cacheReadTokens;
            turnDelta.cacheCreationTokens -= usageBefore.cacheCreationTokens;
            _outbound.push(CompletionMessage {
                .success = true, .turnUsage = turnDelta, .sessionUsage = _session.sessionUsage() });
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
        auto result = _session.processMessage(msg.text, std::move(streamCb));
        if (result.has_value())
        {
            auto turnDelta = _session.sessionUsage();
            turnDelta.inputTokens -= usageBefore.inputTokens;
            turnDelta.outputTokens -= usageBefore.outputTokens;
            turnDelta.cacheReadTokens -= usageBefore.cacheReadTokens;
            turnDelta.cacheCreationTokens -= usageBefore.cacheCreationTokens;
            _outbound.push(CompletionMessage { .fullResponse = std::move(*result),
                                               .success = true,
                                               .turnUsage = turnDelta,
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

        // Drain inbound for CancelMessage or UserAnswerMessage during streaming.
        while (auto inMsg = _inbound.tryPop())
        {
            std::visit(
                [this](auto const& m) {
                    using MsgType = std::decay_t<decltype(m)>;
                    if constexpr (std::is_same_v<MsgType, CancelMessage>)
                        _cancelled.store(true, std::memory_order_relaxed);
                    else if constexpr (std::is_same_v<MsgType, UserAnswerMessage>)
                        _askUserResponses.push(m);
                    else if constexpr (std::is_same_v<MsgType, ShutdownMessage>)
                        _cancelled.store(true, std::memory_order_relaxed);
                },
                *inMsg);
        }

        if (_cancelled.load(std::memory_order_relaxed))
            return false;

        _outbound.push(TokenMessage { .token = std::string(token) });
        return true;
    };
}

} // namespace endo::agent
