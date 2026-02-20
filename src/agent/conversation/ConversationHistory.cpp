// SPDX-License-Identifier: Apache-2.0
#include <agent/conversation/ConversationHistory.hpp>
#include <agent/conversation/TokenEstimator.hpp>

namespace endo::agent
{

void ConversationHistory::addMessage(ChatMessage message)
{
    _estimatedTokens += estimateTokenCount(message);
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
    _estimatedTokens = 0;
}

void ConversationHistory::setSystemPrompt(std::string systemPrompt)
{
    auto systemMessage = ChatMessage::text(Role::System, std::move(systemPrompt));

    if (!_messages.empty() && _messages.front().role == Role::System)
    {
        _estimatedTokens -= estimateTokenCount(_messages.front());
        _estimatedTokens += estimateTokenCount(systemMessage);
        _messages.front() = std::move(systemMessage);
    }
    else
    {
        _estimatedTokens += estimateTokenCount(systemMessage);
        _messages.insert(_messages.begin(), std::move(systemMessage));
    }
}

auto ConversationHistory::estimatedTokenCount() const noexcept -> size_t
{
    return _estimatedTokens;
}

void ConversationHistory::replaceMessages(std::vector<ChatMessage> messages)
{
    _messages = std::move(messages);
    recalculateTokens();
}

void ConversationHistory::recalculateTokens()
{
    _estimatedTokens = endo::agent::estimateTokenCount(std::span<ChatMessage const>(_messages));
}

} // namespace endo::agent
