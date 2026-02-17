// SPDX-License-Identifier: Apache-2.0
#include <agent/ConversationHistory.hpp>

namespace endo::agent
{

void ConversationHistory::addMessage(ChatMessage message)
{
    _messages.emplace_back(std::move(message));
}

auto ConversationHistory::messages() const noexcept -> std::span<ChatMessage const>
{
    return _messages;
}

auto ConversationHistory::size() const noexcept -> size_t
{
    return _messages.size();
}

auto ConversationHistory::empty() const noexcept -> bool
{
    return _messages.empty();
}

void ConversationHistory::clear()
{
    _messages.clear();
}

void ConversationHistory::setSystemPrompt(std::string systemPrompt)
{
    auto systemMessage = ChatMessage::text(Role::System, std::move(systemPrompt));
    if (!_messages.empty() && _messages.front().role == Role::System)
        _messages.front() = std::move(systemMessage);
    else
        _messages.insert(_messages.begin(), std::move(systemMessage));
}

} // namespace endo::agent
