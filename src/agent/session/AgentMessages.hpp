// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file AgentMessages.hpp
/// @brief Message types for bidirectional communication between the main thread and agent worker.

#include <chrono>
#include <cstdint>
#include <string>
#include <variant>

#include <agent/Plan.hpp>
#include <agent/Types.hpp>
#include <agent/tools/AskUserTool.hpp>

namespace endo::agent
{

// ============================================================================
// Main Thread → Agent Worker messages
// ============================================================================

/// User submitted a prompt for the agent to process.
struct UserPromptMessage
{
    std::string text;      ///< The user's query text.
    bool planMode = false; ///< Whether to use plan exploration mode.
};

/// Request to cancel the current operation.
struct CancelMessage
{
};

/// Request for clean shutdown.
struct ShutdownMessage
{
};

/// User's response to an AskUserTool question.
struct UserAnswerMessage
{
    uint64_t requestId = 0; ///< Correlates with the AskUserRequest.
    UserAnswer answer;      ///< The user's answer.
};

/// All message types that flow from the main thread to the agent worker.
using ToAgentMessage = std::variant<UserPromptMessage, CancelMessage, ShutdownMessage, UserAnswerMessage>;

// ============================================================================
// Agent Worker → Main Thread messages
// ============================================================================

/// A streamed text token from the LLM.
struct TokenMessage
{
    std::string token; ///< The text fragment.
};

/// Indicates the agent has started thinking (show spinner).
struct ThinkingStartMessage
{
};

/// A tool is about to be executed.
struct ToolStatusMessage
{
    ToolCall call; ///< The tool call being executed.
};

/// A tool has finished execution.
struct ToolResultMessage
{
    std::string name;                         ///< Tool name.
    std::string content;                      ///< Tool output.
    bool isError = false;                     ///< Whether the tool returned an error.
    std::chrono::milliseconds duration { 0 }; ///< Execution duration.
};

/// The agent has completed processing a message.
struct CompletionMessage
{
    std::string fullResponse; ///< The complete response text.
    bool success = true;      ///< Whether processing succeeded.
    std::string errorMessage; ///< Error details if success is false.
};

/// The agent needs user input via AskUserTool.
struct AskUserRequest
{
    uint64_t requestId = 0; ///< Unique ID for correlating the response.
    UserQuestion question;  ///< The question to present to the user.
};

/// A plan has been generated and needs user approval.
struct PlanGeneratedMessage
{
    Plan plan; ///< The generated plan.
};

/// The agent worker thread has exited.
struct AgentShutdownComplete
{
};

/// All message types that flow from the agent worker to the main thread.
using FromAgentMessage = std::variant<TokenMessage,
                                      ThinkingStartMessage,
                                      ToolStatusMessage,
                                      ToolResultMessage,
                                      CompletionMessage,
                                      AskUserRequest,
                                      PlanGeneratedMessage,
                                      AgentShutdownComplete>;

} // namespace endo::agent
