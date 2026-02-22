// SPDX-License-Identifier: Apache-2.0
#include <chrono>
#include <format>
#include <span>

#include <agent/PermissionManager.hpp>
#include <agent/conversation/ConversationCompactor.hpp>
#include <agent/session/AgentSession.hpp>
#include <agent/tools/AgentTool.hpp>
#include <agent/tools/SubmitPlanTool.hpp>
#include <agent/tools/ToolRegistry.hpp>
#include <agent/tracing/AgentTracer.hpp>

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

    /// @brief Trims leading and trailing whitespace from @p str in place.
    void trimInPlace(std::string& str)
    {
        if (auto const start = str.find_first_not_of(" \t\n\r"); start != std::string::npos)
            str = str.substr(start, str.find_last_not_of(" \t\n\r") - start + 1);
        else
            str.clear();
    }

    /// Returns the current UTC time as an ISO 8601 timestamp string.
    auto utcTimestampNow() -> std::string
    {
        auto const now = std::chrono::system_clock::now();
        return std::format("{:%FT%TZ}", std::chrono::floor<std::chrono::milliseconds>(now));
    }
} // namespace

AgentSession::AgentSession(LlmProvider& provider): _provider(&provider)
{
}

AgentSession::~AgentSession() = default;

void AgentSession::setProvider(LlmProvider& provider)
{
    _provider = &provider;
}

auto AgentSession::processMessage(std::string_view userMessage, StreamCallback streamCb)
    -> std::expected<std::string, AgentError>
{
    auto trimmedMessage = std::string(userMessage);
    trimInPlace(trimmedMessage);
    if (trimmedMessage.empty())
        return std::unexpected(AgentError {
            .code = AgentErrorCode::ProviderError,
            .message = "Empty message after trimming whitespace.",
        });

    // Add user message to history
    _history.addMessage(ChatMessage::text(Role::User, std::move(trimmedMessage)));

    if (_tracer)
        _tracer->writeUserMessage("chat", userMessage);

    auto const toolDefs = _toolRegistry ? _toolRegistry->definitions() : std::vector<ToolDefinition> {};
    auto const tools = std::span<ToolDefinition const>(toolDefs);

    _lastTurnUsage = {};

    for (auto iteration = size_t { 0 }; iteration < _maxToolIterations; ++iteration)
    {
        // Compact conversation if needed before calling the provider
        if (_compactor)
        {
            auto const beforeMessages = _history.size();
            auto const beforeTokens = _history.estimatedTokenCount();
            auto compactResult = _compactor->compactIfNeeded(_history);
            if (_tracer && _history.size() != beforeMessages)
            {
                _tracer->writeCompaction(
                    beforeMessages, _history.size(), beforeTokens, _history.estimatedTokenCount());
            }
            // Log but don't abort on compaction failure
            (void) compactResult;
        }

        if (_tracer)
            _tracer->writeLlmRequest(iteration, _history.size(), _history.estimatedTokenCount());

        auto const generateStart = std::chrono::steady_clock::now();
        auto result = _provider->generate(_history.messages(), tools, streamCb);
        auto const generateElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - generateStart);

        if (!result.has_value())
        {
            auto const errorMsg =
                std::format("{} (HTTP {})", result.error().message, result.error().httpStatus);
            if (_tracer)
                _tracer->writeError("ProviderError", errorMsg);
            return std::unexpected(AgentError {
                .code = AgentErrorCode::ProviderError,
                .message = errorMsg,
            });
        }

        if (_tracer)
            _tracer->writeLlmResponse(iteration,
                                      result->hasToolCalls(),
                                      result->toolCalls.size(),
                                      result->textContent().size(),
                                      generateElapsed,
                                      result->textContent(),
                                      result->toolCalls,
                                      result->usage);

        // Accumulate token usage from this generate() call.
        if (result->usage.has_value())
        {
            _sessionUsage += *result->usage;
            // Per-turn: input/cache reflect last call's context, output is summed.
            _lastTurnUsage.inputTokens = result->usage->inputTokens;
            _lastTurnUsage.cacheReadTokens = result->usage->cacheReadTokens;
            _lastTurnUsage.cacheCreationTokens = result->usage->cacheCreationTokens;
            _lastTurnUsage.outputTokens += result->usage->outputTokens;
        }

        // Add the full assistant message (including ToolUseBlocks) to history
        auto assistantMsg = ChatMessage { .role = Role::Assistant, .content = result->content };
        _history.addMessage(std::move(assistantMsg));

        // If no tool calls or no registry, return the text response
        if (!result->hasToolCalls() || !_toolRegistry)
        {
            ++_turnCount;
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
    }

    auto const errorMsg = std::format("Tool loop exceeded {} iterations", _maxToolIterations);
    if (_tracer)
        _tracer->writeError("ToolLoopExceeded", errorMsg);

    return std::unexpected(AgentError {
        .code = AgentErrorCode::ToolLoopExceeded,
        .message = errorMsg,
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
    _sessionUsage = {};
    _lastTurnUsage = {};
    _turnCount = 0;
}

auto AgentSession::sessionUsage() const noexcept -> TokenUsage const&
{
    return _sessionUsage;
}

auto AgentSession::lastTurnUsage() const noexcept -> TokenUsage const&
{
    return _lastTurnUsage;
}

auto AgentSession::turnCount() const noexcept -> int
{
    return _turnCount;
}

void AgentSession::loadPersistedMessages(std::vector<ChatMessage> messages)
{
    _history.replaceMessages(std::move(messages));
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

    auto trimmedPlanMessage = std::string(userMessage);
    trimInPlace(trimmedPlanMessage);
    if (trimmedPlanMessage.empty())
        return std::unexpected(AgentError {
            .code = AgentErrorCode::ProviderError,
            .message = "Empty message after trimming whitespace.",
        });

    // Add user message to history
    _history.addMessage(ChatMessage::text(Role::User, std::move(trimmedPlanMessage)));

    if (_tracer)
        _tracer->writeUserMessage("plan", userMessage);

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

    _lastTurnUsage = {};

    for (auto iteration = size_t { 0 }; iteration < _maxExplorationIterations; ++iteration)
    {
        // Compact conversation if needed
        if (_compactor)
        {
            auto const beforeMessages = _history.size();
            auto const beforeTokens = _history.estimatedTokenCount();
            (void) _compactor->compactIfNeeded(_history);
            if (_tracer && _history.size() != beforeMessages)
            {
                _tracer->writeCompaction(
                    beforeMessages, _history.size(), beforeTokens, _history.estimatedTokenCount());
            }
        }

        if (_tracer)
            _tracer->writeLlmRequest(iteration, _history.size(), _history.estimatedTokenCount());

        auto const generateStart = std::chrono::steady_clock::now();
        auto result = _provider->generate(_history.messages(), tools, streamCb);
        auto const generateElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - generateStart);

        if (!result.has_value())
        {
            auto const errorMsg =
                std::format("{} (HTTP {})", result.error().message, result.error().httpStatus);
            if (_tracer)
                _tracer->writeError("ProviderError", errorMsg);
            return std::unexpected(AgentError {
                .code = AgentErrorCode::ProviderError,
                .message = errorMsg,
            });
        }

        if (_tracer)
            _tracer->writeLlmResponse(iteration,
                                      result->hasToolCalls(),
                                      result->toolCalls.size(),
                                      result->textContent().size(),
                                      generateElapsed,
                                      result->textContent(),
                                      result->toolCalls,
                                      result->usage);

        // Accumulate token usage from this generate() call.
        if (result->usage.has_value())
        {
            _sessionUsage += *result->usage;
            // Per-turn: input/cache reflect last call's context, output is summed.
            _lastTurnUsage.inputTokens = result->usage->inputTokens;
            _lastTurnUsage.cacheReadTokens = result->usage->cacheReadTokens;
            _lastTurnUsage.cacheCreationTokens = result->usage->cacheCreationTokens;
            _lastTurnUsage.outputTokens += result->usage->outputTokens;
        }

        // Add assistant message to history
        auto assistantMsg = ChatMessage { .role = Role::Assistant, .content = result->content };
        _history.addMessage(std::move(assistantMsg));

        // If no tool calls, the agent didn't submit a plan
        if (!result->hasToolCalls() || !_toolRegistry)
        {
            auto const errorMsg = std::string("Agent finished exploration without submitting a plan.");
            if (_tracer)
                _tracer->writeError("ProviderError", errorMsg);
            return std::unexpected(AgentError {
                .code = AgentErrorCode::ProviderError,
                .message = errorMsg,
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
            ++_turnCount;
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

    auto const errorMsg = std::format("Exploration exceeded {} iterations without submitting a plan.",
                                      _maxExplorationIterations);
    if (_tracer)
        _tracer->writeError("ToolLoopExceeded", errorMsg);

    return std::unexpected(AgentError {
        .code = AgentErrorCode::ToolLoopExceeded,
        .message = errorMsg,
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
    _compactor = std::make_unique<ConversationCompactor>(*_provider, config);
}

auto AgentSession::forceCompaction() -> std::expected<bool, std::string>
{
    if (!_compactor)
        return false;
    return _compactor->compactIfNeeded(_history);
}

void AgentSession::setPermissionManager(PermissionManager* pm)
{
    _permissionManager = pm;
}

void AgentSession::setTracer(AgentTracer* tracer)
{
    _tracer = tracer;
}

auto AgentSession::executeToolCalls(std::span<ToolCall const> calls) -> std::vector<ToolResult>
{
    auto results = std::vector<ToolResult> {};
    results.reserve(calls.size());

    for (auto const& call: calls)
    {
        if (_toolStatusCallback)
            _toolStatusCallback(call);

        // Permission check: classify risk and check with permission manager.
        if (_permissionManager)
        {
            auto risk = ToolRisk::Mutating;
            if (auto const* tool = _toolRegistry->findTool(call.name))
                risk = tool->classifyRisk(call.arguments);

            auto const decision = _permissionManager->checkPermission(call.name, risk, call.arguments);
            if (decision != PermissionDecision::Approved)
            {
                auto errorMsg = std::string {};
                switch (decision)
                {
                    case PermissionDecision::Denied:
                        errorMsg = std::format("Permission denied for tool: {}", call.name);
                        break;
                    case PermissionDecision::Blocked:
                        errorMsg = std::format("Tool is blocked: {}", call.name);
                        break;
                    case PermissionDecision::Cancelled:
                        errorMsg = std::format("Permission prompt cancelled for tool: {}", call.name);
                        break;
                    default: errorMsg = std::format("Tool not approved: {}", call.name); break;
                }

                auto deniedResult = ToolResult {
                    .callId = call.id,
                    .content = errorMsg,
                    .isError = true,
                };

                if (_tracer)
                {
                    _tracer->writeToolCall(ToolTraceEntry {
                        .timestamp = utcTimestampNow(),
                        .callId = call.id,
                        .toolName = call.name,
                        .arguments = call.arguments,
                        .resultContent = deniedResult.content,
                        .resultIsError = true,
                        .duration = std::chrono::milliseconds { 0 },
                    });
                }

                results.push_back(std::move(deniedResult));
                continue;
            }
        }

        auto const startTime = std::chrono::steady_clock::now();
        auto result = _toolRegistry->execute(call);
        auto const elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startTime);

        truncateToolResult(result, _maxToolResultSize);

        if (_tracer)
        {
            _tracer->writeToolCall(ToolTraceEntry {
                .timestamp = utcTimestampNow(),
                .callId = call.id,
                .toolName = call.name,
                .arguments = call.arguments,
                .resultContent = result.content,
                .resultIsError = result.isError,
                .duration = elapsed,
            });
        }

        results.push_back(std::move(result));
    }

    return results;
}

} // namespace endo::agent
