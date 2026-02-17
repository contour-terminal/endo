// SPDX-License-Identifier: Apache-2.0
#include <format>
#include <span>

#include <agent/AgentSession.hpp>
#include <agent/ConversationCompactor.hpp>
#include <agent/tools/SubmitPlanTool.hpp>
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

auto AgentSession::processMessageForPlan(std::string_view userMessage, StreamCallback streamCb)
    -> std::expected<Plan, AgentError>
{
    if (!_toolRegistry)
    {
        return std::unexpected(AgentError {
            .code = AgentErrorCode::NoProvider,
            .message = "No tool registry configured for plan mode.",
        });
    }

    // Find or verify the submit_plan tool is registered
    auto* submitPlanTool = dynamic_cast<SubmitPlanTool*>(_toolRegistry->findTool("submit_plan"));
    if (!submitPlanTool)
    {
        return std::unexpected(AgentError {
            .code = AgentErrorCode::NoProvider,
            .message = "submit_plan tool not registered in tool registry.",
        });
    }

    // Clear any previous plan
    submitPlanTool->clearParsedPlan();

    // Add user message to history
    _history.addMessage(ChatMessage::text(Role::User, std::string(userMessage)));

    // Build filtered tool definitions: only read-only tools + submit_plan
    static constexpr auto allowedTools = std::array<std::string_view, 5> {
        "read_file", "glob", "grep", "git", "submit_plan",
    };
    auto const filteredDefs = _toolRegistry->definitions([](std::string_view toolName) {
        for (auto const& allowed: allowedTools)
            if (toolName == allowed)
                return true;
        return false;
    });
    auto const tools = std::span<ToolDefinition const>(filteredDefs);

    for (auto iteration = size_t { 0 }; iteration < _maxExplorationIterations; ++iteration)
    {
        // Compact conversation if needed
        if (_compactor)
            (void) _compactor->compactIfNeeded(_history);

        auto result = _provider.generate(_history.messages(), tools, streamCb);

        if (!result.has_value())
        {
            return std::unexpected(AgentError {
                .code = AgentErrorCode::ProviderError,
                .message = std::format("{} (HTTP {})", result.error().message, result.error().httpStatus),
            });
        }

        // Add assistant message to history
        auto assistantMsg = ChatMessage { .role = Role::Assistant, .content = result->content };
        _history.addMessage(std::move(assistantMsg));

        // If no tool calls, the agent didn't submit a plan
        if (!result->hasToolCalls() || !_toolRegistry)
        {
            return std::unexpected(AgentError {
                .code = AgentErrorCode::ProviderError,
                .message = "Agent finished exploration without submitting a plan.",
            });
        }

        // Execute tool calls
        auto toolResults = executeToolCalls(result->toolCalls);

        // Check if submit_plan was called
        if (submitPlanTool->lastParsedPlan().has_value())
        {
            // Add tool results to history before returning
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

            auto plan = *submitPlanTool->lastParsedPlan();
            submitPlanTool->clearParsedPlan();
            return plan;
        }

        // Add tool results to history and continue exploration
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

        // Clear stream callback for subsequent iterations
        streamCb = nullptr;
    }

    return std::unexpected(AgentError {
        .code = AgentErrorCode::ToolLoopExceeded,
        .message = std::format("Exploration exceeded {} iterations without submitting a plan.",
                               _maxExplorationIterations),
    });
}

void AgentSession::setMaxExplorationIterations(size_t n)
{
    _maxExplorationIterations = n;
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
