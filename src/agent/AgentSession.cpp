// SPDX-License-Identifier: Apache-2.0
#include <format>
#include <span>

#include <agent/AgentSession.hpp>

namespace endo::agent
{

AgentSession::AgentSession(LlmProvider& provider): _provider(provider)
{
}

auto AgentSession::processMessage(std::string_view userMessage, StreamCallback streamCb)
    -> std::expected<std::string, AgentError>
{
    // Add user message to history
    _history.addMessage(ChatMessage::text(Role::User, std::string(userMessage)));

    // Call provider with conversation history (no tools in Phase 2)
    auto const tools = std::span<ToolDefinition const> {};
    auto result = _provider.generate(_history.messages(), tools, std::move(streamCb));

    if (!result.has_value())
    {
        return std::unexpected(AgentError {
            .code = AgentErrorCode::ProviderError,
            .message = std::format("{} (HTTP {})", result.error().message, result.error().httpStatus),
        });
    }

    // Extract response text and add to history
    auto responseText = result->textContent();
    _history.addMessage(ChatMessage::text(Role::Assistant, responseText));

    return responseText;
}

void AgentSession::setSystemPrompt(std::string systemPrompt)
{
    _history.setSystemPrompt(std::move(systemPrompt));
}

auto AgentSession::history() const -> ConversationHistory const&
{
    return _history;
}

void AgentSession::reset()
{
    _history.clear();
}

} // namespace endo::agent
