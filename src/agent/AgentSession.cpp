// SPDX-License-Identifier: Apache-2.0
#include <format>
#include <span>

#include <agent/AgentSession.hpp>
#include <agent/ConversationCompactor.hpp>
#include <agent/tools/ToolRegistry.hpp>

namespace endo::agent
{

namespace
{
    /// Truncates a tool result's content if it exceeds the maximum allowed size.
    /// Appends a marker indicating how many bytes were omitted.
    void truncateToolResult(ToolResult& result, size_t maxSize)
    {
        if (result.content.size() <= maxSize)
            return;
        auto const omitted = result.content.size() - maxSize;
        result.content.resize(maxSize);
        result.content += std::format("\n\n[truncated -- {} bytes omitted]", omitted);
    }
} // namespace

AgentSession::AgentSession(LlmProvider& provider): _provider(provider)
{
}

AgentSession::~AgentSession() = default;

auto AgentSession::processMessage(std::string_view userMessage, StreamCallback streamCb)
    -> std::expected<std::string, AgentError>
{
    // Add user message to history
    _history.addMessage(ChatMessage::text(Role::User, std::string(userMessage)));

    auto const toolDefs = _toolRegistry ? _toolRegistry->definitions() : std::vector<ToolDefinition> {};
    auto const tools = std::span<ToolDefinition const>(toolDefs);

    for (auto iteration = size_t { 0 }; iteration < _maxToolIterations; ++iteration)
    {
        // Compact conversation if needed before calling the provider
        if (_compactor)
        {
            auto compactResult = _compactor->compactIfNeeded(_history);
            // Log but don't abort on compaction failure
            (void) compactResult;
        }

        auto result = _provider.generate(_history.messages(), tools, streamCb);

        if (!result.has_value())
        {
            return std::unexpected(AgentError {
                .code = AgentErrorCode::ProviderError,
                .message = std::format("{} (HTTP {})", result.error().message, result.error().httpStatus),
            });
        }

        // Add the full assistant message (including ToolUseBlocks) to history
        auto assistantMsg = ChatMessage { .role = Role::Assistant, .content = result->content };
        _history.addMessage(std::move(assistantMsg));

        // If no tool calls or no registry, return the text response
        if (!result->hasToolCalls() || !_toolRegistry)
        {
            return result->textContent();
        }

        // Execute tool calls and add results to history
        auto toolResults = executeToolCalls(result->toolCalls);

        auto toolResultMsg = ChatMessage { .role = Role::User };
        for (auto& tr: toolResults)
        {
            toolResultMsg.content.emplace_back(ToolResultBlock {
                .toolUseId = std::move(tr.callId),
                .content = std::move(tr.content),
                .isError = tr.isError,
            });
        }
        _history.addMessage(std::move(toolResultMsg));

        // Clear stream callback for subsequent iterations (only stream the first response)
        streamCb = nullptr;
    }

    return std::unexpected(AgentError {
        .code = AgentErrorCode::ToolLoopExceeded,
        .message = std::format("Tool loop exceeded {} iterations", _maxToolIterations),
    });
}

void AgentSession::setToolRegistry(ToolRegistry* registry)
{
    _toolRegistry = registry;
}

void AgentSession::setMaxToolIterations(size_t n)
{
    _maxToolIterations = n;
}

void AgentSession::setToolStatusCallback(ToolStatusCallback callback)
{
    _toolStatusCallback = std::move(callback);
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

void AgentSession::setMaxToolResultSize(size_t maxBytes)
{
    _maxToolResultSize = maxBytes;
}

void AgentSession::setCompactionConfig(CompactionConfig const& config)
{
    _compactor = std::make_unique<ConversationCompactor>(_provider, config);
}

auto AgentSession::executeToolCalls(std::span<ToolCall const> calls) -> std::vector<ToolResult>
{
    auto results = std::vector<ToolResult> {};
    results.reserve(calls.size());

    for (auto const& call: calls)
    {
        if (_toolStatusCallback)
            _toolStatusCallback(call.name);

        auto result = _toolRegistry->execute(call);
        truncateToolResult(result, _maxToolResultSize);
        results.push_back(std::move(result));
    }

    return results;
}

} // namespace endo::agent
