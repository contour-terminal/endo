// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file AgentWorker.hpp
/// @brief Background thread wrapper for AgentSession, enabling non-blocking agent mode.

#include <http/HttpClient.hpp>

#include <atomic>
#include <memory>
#include <thread>

#include <agent/providers/LlmProvider.hpp>
#include <agent/session/AgentMessages.hpp>
#include <agent/session/AgentSession.hpp>
#include <platform/MessageQueue.hpp>

namespace endo::agent
{

class ToolRegistry;

/// Runs an AgentSession on a background thread with message-based communication.
///
/// The main thread communicates with the worker via MessageQueues:
/// - Inbound (ToAgentMessage): user prompts, cancellation, shutdown, ask-user answers.
/// - Outbound (FromAgentMessage): tokens, tool status, completion, ask-user requests.
///
/// The session is referenced (not owned) — it persists across runAgentMode() calls.
/// Tool execution happens synchronously on the worker thread.
class AgentWorker
{
  public:
    /// @brief Constructs a worker referencing an external session.
    ///
    /// The session is owned by the caller (e.g., Shell) and must outlive the worker.
    /// The session must only be accessed by the main thread when the worker is stopped.
    /// @param session Reference to the agent session.
    /// @param outbound Reference to the outbound queue (owned by main thread).
    AgentWorker(AgentSession& session, platform::MessageQueue<FromAgentMessage>& outbound);

    ~AgentWorker();

    AgentWorker(AgentWorker const&) = delete;
    AgentWorker& operator=(AgentWorker const&) = delete;
    AgentWorker(AgentWorker&&) = delete;
    AgentWorker& operator=(AgentWorker&&) = delete;

    /// @brief Starts the worker thread.
    void start();

    /// @brief Requests graceful shutdown and joins the worker thread.
    void stop();

    /// @brief Returns the inbound message queue for sending messages to the worker.
    [[nodiscard]] auto inbound() -> platform::MessageQueue<ToAgentMessage>& { return _inbound; }

    /// @brief Returns whether the worker is currently processing a prompt.
    [[nodiscard]] auto isBusy() const noexcept -> bool { return _busy.load(std::memory_order_relaxed); }

    /// @brief Returns an AskUserCallback that routes questions through the message queues.
    ///
    /// The returned callback pushes an AskUserRequest to the outbound queue and
    /// blocks on the ask-user response queue until the main thread provides an answer.
    [[nodiscard]] auto makeAskUserCallback() -> AskUserCallback;

  private:
    /// Main run loop executed on the worker thread.
    void run(std::stop_token stopToken);

    /// Handles a user prompt message.
    void handlePrompt(UserPromptMessage const& msg, std::stop_token const& stopToken);

    /// Creates a StreamCallback that checks for cancellation and pushes tokens.
    [[nodiscard]] auto makeStreamCallback(std::stop_token const& stopToken) -> StreamCallback;

    AgentSession& _session;

    platform::MessageQueue<ToAgentMessage> _inbound;
    platform::MessageQueue<FromAgentMessage>& _outbound;

    /// Queue for AskUserTool synchronization: worker blocks here waiting for user answer.
    platform::MessageQueue<UserAnswerMessage> _askUserResponses;

    std::jthread _thread;
    std::atomic<bool> _busy { false };
    std::atomic<bool> _cancelled { false };
    std::atomic<uint64_t> _nextRequestId { 1 };
};

} // namespace endo::agent
