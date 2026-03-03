// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file TraceEvent.hpp
/// @brief Structured trace event types for real-time agent I/O visibility.

#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <agent/Types.hpp>

namespace endo::agent
{

/// A user message was submitted to the agent.
struct TraceUserMessageEvent
{
    std::string mode;    ///< Session mode ("chat" or "plan").
    std::string content; ///< The user's message text.
};

/// An LLM request is about to be sent.
struct TraceLlmRequestEvent
{
    size_t iteration = 0;     ///< Current tool loop iteration index.
    size_t messageCount = 0;  ///< Number of messages in conversation history.
    size_t tokenEstimate = 0; ///< Estimated token count of the conversation.
};

/// An LLM response was received.
struct TraceLlmResponseEvent
{
    size_t iteration = 0;                     ///< Current tool loop iteration index.
    bool hasToolCalls = false;                ///< Whether the response contains tool calls.
    size_t toolCount = 0;                     ///< Number of tool calls in the response.
    size_t textLength = 0;                    ///< Length of the text content.
    std::chrono::milliseconds duration { 0 }; ///< Generation duration.
    std::optional<TokenUsage> usage;          ///< Token usage statistics, if available.
    std::vector<ToolCall> toolCalls;          ///< Tool calls the model requested.
};

/// A tool call was executed (or denied).
struct TraceToolCallEvent
{
    std::string name;                         ///< Tool name.
    nlohmann::json arguments;                 ///< Tool input arguments.
    std::string resultContent;                ///< Tool output content (post-truncation).
    bool resultIsError = false;               ///< Whether the tool returned an error.
    std::chrono::milliseconds duration { 0 }; ///< Execution duration.
};

/// Conversation compaction occurred.
struct TraceCompactionEvent
{
    size_t beforeMessages = 0; ///< Number of messages before compaction.
    size_t afterMessages = 0;  ///< Number of messages after compaction.
    size_t beforeTokens = 0;   ///< Estimated tokens before compaction.
    size_t afterTokens = 0;    ///< Estimated tokens after compaction.
};

/// An error occurred during agent processing.
struct TraceErrorEvent
{
    std::string code;    ///< Error code (e.g. "ProviderError", "ToolLoopExceeded").
    std::string message; ///< Descriptive error message.
};

/// All possible trace event types.
using TraceEvent = std::variant<TraceUserMessageEvent,
                                TraceLlmRequestEvent,
                                TraceLlmResponseEvent,
                                TraceToolCallEvent,
                                TraceCompactionEvent,
                                TraceErrorEvent>;

/// Callback invoked when a trace event is emitted.
/// @param event The trace event to process.
using TraceEventCallback = std::function<void(TraceEvent const&)>;

} // namespace endo::agent
