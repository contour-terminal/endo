// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <expected>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <agent/ConversationHistory.hpp>
#include <agent/LlmProvider.hpp>
#include <agent/Types.hpp>

namespace endo::agent
{

class ToolRegistry;
class ConversationCompactor;
struct CompactionConfig;

/// Error codes specific to the agent session.
enum class AgentErrorCode : uint8_t
{
    ProviderError,    ///< The LLM provider returned an error.
    NoProvider,       ///< No provider is configured or authenticated.
    ToolLoopExceeded, ///< The tool call loop exceeded the maximum number of iterations.
};

/// Error information from an agent session operation.
struct AgentError
{
    AgentErrorCode code;
    std::string message;
};

/// Callback invoked when a tool begins execution.
/// @param toolName The name of the tool being executed.
using ToolStatusCallback = std::function<void(std::string_view toolName)>;

/// Manages a conversation with an LLM provider.
///
/// AgentSession maintains conversation history and delegates generation
/// to the configured LlmProvider. Each call to processMessage() appends
/// the user message and the assistant's response to the history.
///
/// When a ToolRegistry is set, processMessage() enters a tool loop:
/// the model can request tool calls, which are executed and fed back
/// until the model produces a final text response or the iteration limit is reached.
class AgentSession
{
  public:
    /// @brief Constructs a session with the given provider.
    /// @param provider The LLM provider to use for generation.
    explicit AgentSession(LlmProvider& provider);

    ~AgentSession();

    /// @brief Processes a user message and generates a response.
    ///
    /// Adds the user message to history, calls the provider with streaming,
    /// executes any tool calls in a loop, and returns the final response text.
    /// @param userMessage The user's query text.
    /// @param streamCb Optional callback for streaming tokens as they arrive.
    /// @return The complete response text, or an error.
    [[nodiscard]] auto processMessage(std::string_view userMessage, StreamCallback streamCb)
        -> std::expected<std::string, AgentError>;

    /// @brief Sets the tool registry for tool call dispatch.
    ///
    /// When set, the session will pass tool definitions to the provider
    /// and execute tool calls in a loop until the model produces a final response.
    /// @param registry Pointer to the tool registry (must outlive the session).
    void setToolRegistry(ToolRegistry* registry);

    /// @brief Sets the maximum number of tool loop iterations.
    /// @param n Maximum iterations (default: 25).
    void setMaxToolIterations(size_t n);

    /// @brief Sets an optional callback for tool execution status updates.
    /// @param callback Callback invoked with the tool name when a tool starts executing.
    void setToolStatusCallback(ToolStatusCallback callback);

    /// @brief Sets or replaces the system prompt.
    /// @param systemPrompt The system prompt text.
    void setSystemPrompt(std::string systemPrompt);

    /// @brief Returns the conversation history (read-only).
    [[nodiscard]] auto history() const -> ConversationHistory const&;

    /// @brief Resets the conversation, clearing all history.
    void reset();

    /// @brief Sets the maximum tool result size in bytes before truncation.
    /// @param maxBytes Maximum bytes for a single tool result content.
    void setMaxToolResultSize(size_t maxBytes);

    /// @brief Configures conversation compaction with the given settings.
    ///
    /// Creates a ConversationCompactor that will summarize old messages when
    /// the conversation approaches the context window limit.
    /// @param config The compaction configuration.
    void setCompactionConfig(CompactionConfig const& config);

  private:
    /// Executes a batch of tool calls and returns results.
    [[nodiscard]] auto executeToolCalls(std::span<ToolCall const> calls) -> std::vector<ToolResult>;

    LlmProvider& _provider;
    ConversationHistory _history;
    ToolRegistry* _toolRegistry = nullptr;
    size_t _maxToolIterations = 25;
    size_t _maxToolResultSize = 30720;
    ToolStatusCallback _toolStatusCallback;
    std::unique_ptr<ConversationCompactor> _compactor;
};

} // namespace endo::agent
