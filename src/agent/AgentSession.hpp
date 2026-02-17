// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <expected>
#include <string>
#include <string_view>

#include <agent/ConversationHistory.hpp>
#include <agent/LlmProvider.hpp>
#include <agent/Types.hpp>

namespace endo::agent
{

/// Error codes specific to the agent session.
enum class AgentErrorCode : uint8_t
{
    ProviderError, ///< The LLM provider returned an error.
    NoProvider,    ///< No provider is configured or authenticated.
};

/// Error information from an agent session operation.
struct AgentError
{
    AgentErrorCode code;
    std::string message;
};

/// Manages a conversation with an LLM provider.
///
/// AgentSession maintains conversation history and delegates generation
/// to the configured LlmProvider. Each call to processMessage() appends
/// the user message and the assistant's response to the history.
class AgentSession
{
  public:
    /// @brief Constructs a session with the given provider.
    /// @param provider The LLM provider to use for generation.
    explicit AgentSession(LlmProvider& provider);

    /// @brief Processes a user message and generates a response.
    ///
    /// Adds the user message to history, calls the provider with streaming,
    /// adds the assistant response to history, and returns the full response text.
    /// @param userMessage The user's query text.
    /// @param streamCb Optional callback for streaming tokens as they arrive.
    /// @return The complete response text, or an error.
    [[nodiscard]] auto processMessage(std::string_view userMessage, StreamCallback streamCb)
        -> std::expected<std::string, AgentError>;

    /// @brief Sets or replaces the system prompt.
    /// @param systemPrompt The system prompt text.
    void setSystemPrompt(std::string systemPrompt);

    /// @brief Returns the conversation history (read-only).
    [[nodiscard]] auto history() const -> ConversationHistory const&;

    /// @brief Resets the conversation, clearing all history.
    void reset();

  private:
    LlmProvider& _provider;
    ConversationHistory _history;
};

} // namespace endo::agent
