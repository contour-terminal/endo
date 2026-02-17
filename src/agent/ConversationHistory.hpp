// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <span>
#include <string>
#include <vector>

#include <agent/Types.hpp>

namespace endo::agent
{

/// Manages the ordered sequence of chat messages in an agent conversation.
///
/// Provides methods to add messages, query conversation state, and manage
/// the system prompt. The system prompt is always kept at index 0.
class ConversationHistory
{
  public:
    ConversationHistory() = default;

    /// @brief Adds a message to the conversation history.
    /// @param message The chat message to append.
    void addMessage(ChatMessage message);

    /// @brief Returns a read-only view of all messages.
    [[nodiscard]] auto messages() const noexcept -> std::span<ChatMessage const>;

    /// @brief Returns the number of messages in the history.
    [[nodiscard]] auto size() const noexcept -> size_t;

    /// @brief Returns whether the history is empty.
    [[nodiscard]] auto empty() const noexcept -> bool;

    /// @brief Clears all messages from the history.
    void clear();

    /// @brief Sets or replaces the system prompt.
    ///
    /// If the first message is already a system message, it is replaced.
    /// Otherwise, a new system message is inserted at position 0.
    /// @param systemPrompt The system prompt text.
    void setSystemPrompt(std::string systemPrompt);

  private:
    std::vector<ChatMessage> _messages;
};

} // namespace endo::agent
