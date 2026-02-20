// SPDX-License-Identifier: Apache-2.0
#include "ClaudeProvider.hpp"

#include <crispy/base64.h>

#include <format>

#include <agent/auth/OAuthFlow.hpp>

namespace endo::agent
{

namespace
{
    // OAuth-specific constants for request headers.
    constexpr auto OAuthBetaHeader = "oauth-2025-04-20";
    constexpr auto ClaudeCodeBetaHeader = "claude-code-20250219";
    constexpr auto ClaudeCodeUserAgent = "claude-code/2.1.49 (external, cli)";
} // namespace

ClaudeProvider::ClaudeProvider(http::HttpClient const& httpClient, ClaudeProviderConfig config):
    _httpClient(httpClient), _config(std::move(config))
{
}

auto ClaudeProvider::buildRequest(std::span<ChatMessage const> messages,
                                  std::span<ToolDefinition const> tools) const -> http::HttpRequest
{
    auto const requestBody =
        serializeRequest(messages, tools, _config.model, _config.maxTokens, _config.thinkingMode);

    auto request = http::HttpRequest {};
    request.url = std::format("{}/v1/messages", _config.baseUrl);
    request.method = http::HttpMethod::Post;
    request.body = requestBody.dump();

    // Build headers based on token type (OAuth vs API key).
    request.headers = { "Content-Type: application/json" };

    if (isOAuthToken(_config.apiKey))
    {
        // OAuth token: Bearer auth with Claude Code identity headers.
        request.headers.push_back(std::format("Authorization: Bearer {}", _config.apiKey));
        request.headers.push_back(std::format("anthropic-version: {}", _config.apiVersion));
        request.headers.push_back(
            std::format("anthropic-beta: {},{}", ClaudeCodeBetaHeader, OAuthBetaHeader));
        request.headers.push_back(std::format("user-agent: {}", ClaudeCodeUserAgent));
        request.headers.push_back("x-app: cli");
    }
    else
    {
        // Standard API key auth.
        request.headers.push_back(std::format("x-api-key: {}", _config.apiKey));
        request.headers.push_back(std::format("anthropic-version: {}", _config.apiVersion));
    }

    return request;
}

auto ClaudeProvider::executeStreaming(http::HttpRequest const& request, StreamCallback const& streamCb)
    -> std::expected<GenerateResult, ProviderError>
{
    auto result = GenerateResult {};
    auto accumulators = std::vector<ContentBlockAccumulator> {};

    auto errorBody = std::string {};
    auto const sseResult = _httpClient.executeStreaming(
        request,
        [&](http::SseEvent const& event) -> bool {
            auto parsed = parseSseEvent(event, accumulators);
            if (!parsed.has_value())
                return false;

            if (!parsed->textDelta.empty() && streamCb)
            {
                if (!streamCb(parsed->textDelta))
                    return false; // Abort streaming on cancellation.
            }

            for (auto& block: parsed->completedBlocks)
                result.content.push_back(std::move(block));

            for (auto& toolCall: parsed->completedToolCalls)
                result.toolCalls.push_back(std::move(toolCall));

            return !parsed->done;
        },
        &errorBody);

    if (!sseResult.has_value())
        return std::unexpected(
            ProviderError { .code = ProviderErrorCode::NetworkError, .message = sseResult.error().message });

    auto const statusCode = sseResult.value();
    if (statusCode != 200)
    {
        // Try to extract the API error message from the JSON error body.
        auto message = std::format("HTTP {}", statusCode);
        if (!errorBody.empty())
        {
            try
            {
                auto const errorJson = nlohmann::json::parse(errorBody);
                if (errorJson.contains("error") && errorJson["error"].contains("message"))
                    message = std::format(
                        "HTTP {}: {}", statusCode, errorJson["error"]["message"].get<std::string>());
            }
            catch (nlohmann::json::parse_error const&)
            {
                // Fall back to generic message with raw body snippet.
                auto const snippet = errorBody.substr(0, 200);
                message = std::format("HTTP {}: {}", statusCode, snippet);
            }
        }
        return std::unexpected(mapHttpError(statusCode, std::move(message)));
    }

    return result;
}

auto ClaudeProvider::generate(std::span<ChatMessage const> messages,
                              std::span<ToolDefinition const> tools,
                              StreamCallback streamCb) -> std::expected<GenerateResult, ProviderError>
{
    if (_config.apiKey.empty())
        return std::unexpected(
            ProviderError { .code = ProviderErrorCode::ConfigError, .message = "API key is not configured" });

    auto request = buildRequest(messages, tools);
    auto result = executeStreaming(request, streamCb);

    // On 401 with an OAuth token and a token refresher, attempt refresh and retry once.
    if (!result.has_value() && result.error().httpStatus == 401 && isOAuthToken(_config.apiKey)
        && _config.tokenRefresher)
    {
        auto refreshed = _config.tokenRefresher();
        if (refreshed.has_value())
        {
            _config.apiKey = std::move(*refreshed);
            request = buildRequest(messages, tools);
            result = executeStreaming(request, streamCb);
        }
        // If refresh failed, fall through and return the original 401 error.
    }

    return result;
}

auto ClaudeProvider::supportsToolUse() const noexcept -> bool
{
    return true;
}

auto ClaudeProvider::supportsImageInput() const noexcept -> bool
{
    return true;
}

auto ClaudeProvider::supportsImageOutput() const noexcept -> bool
{
    return false;
}

auto ClaudeProvider::contextSize() const noexcept -> size_t
{
    return _config.contextWindowSize;
}

auto ClaudeProvider::modelInfo() const -> ModelInfo
{
    return ModelInfo {
        .providerName = "claude",
        .modelName = _config.model,
        .contextSize = _config.contextWindowSize,
        .supportsToolUse = true,
        .supportsImageInput = true,
        .supportsImageOutput = false,
    };
}

namespace
{
    /// Serializes a single ContentBlock into the Claude API JSON format.
    auto serializeContentBlock(ContentBlock const& block) -> nlohmann::json
    {
        return std::visit(
            [](auto const& b) -> nlohmann::json {
                using T = std::decay_t<decltype(b)>;
                if constexpr (std::is_same_v<T, TextBlock>)
                {
                    return { { "type", "text" }, { "text", b.text } };
                }
                else if constexpr (std::is_same_v<T, ImageBlock>)
                {
                    auto const encoded = crispy::base64::encode(b.data.begin(), b.data.end());
                    return {
                        { "type", "image" },
                        { "source",
                          { { "type", "base64" }, { "media_type", b.mediaType }, { "data", encoded } } },
                    };
                }
                else if constexpr (std::is_same_v<T, ToolUseBlock>)
                {
                    return {
                        { "type", "tool_use" },
                        { "id", b.id },
                        { "name", b.name },
                        { "input", b.arguments },
                    };
                }
                else if constexpr (std::is_same_v<T, ToolResultBlock>)
                {
                    return {
                        { "type", "tool_result" },
                        { "tool_use_id", b.toolUseId },
                        { "content", b.content },
                    };
                }
            },
            block);
    }
} // namespace

auto ClaudeProvider::serializeRequest(std::span<ChatMessage const> messages,
                                      std::span<ToolDefinition const> tools,
                                      std::string const& model,
                                      size_t maxTokens,
                                      ThinkingMode thinkingMode) -> nlohmann::json
{
    auto body = nlohmann::json {
        { "model", model },
        { "max_tokens", maxTokens },
        { "stream", true },
    };

    // Apply thinking mode if enabled.
    // Claude 4.6 models use adaptive thinking; older models use manual budget_tokens.
    if (thinkingMode != ThinkingMode::Off)
    {
        bool const isAdaptiveModel = model.find("4-6") != std::string::npos;
        if (isAdaptiveModel)
        {
            // Adaptive thinking (recommended for 4.6 models).
            body["thinking"] = { { "type", "adaptive" } };
            // Map ThinkingMode to effort level via output_config.
            if (thinkingMode == ThinkingMode::Normal)
                body["output_config"] = { { "effort", "medium" } };
            // Extended = high effort (the default for adaptive), no output_config needed.
        }
        else
        {
            // Manual extended thinking for older models.
            size_t budgetTokens = (thinkingMode == ThinkingMode::Extended) ? 32000 : 10000;
            body["thinking"] = { { "type", "enabled" }, { "budget_tokens", budgetTokens } };
        }
        // Extended thinking requires temperature = 1 for non-adaptive mode.
        if (!body.contains("output_config"))
            body["temperature"] = 1;
    }

    // Extract system messages into a single top-level "system" string.
    auto systemText = std::string {};
    for (auto const& msg: messages)
    {
        if (msg.role == Role::System)
        {
            if (!systemText.empty())
                systemText += '\n';
            systemText += msg.textContent();
        }
    }
    if (!systemText.empty())
        body["system"] = systemText;

    // Build the messages array (non-system messages only).
    auto messagesArray = nlohmann::json::array();
    for (auto const& msg: messages)
    {
        if (msg.role == Role::System)
            continue;

        auto contentArray = nlohmann::json::array();
        for (auto const& block: msg.content)
            contentArray.push_back(serializeContentBlock(block));

        messagesArray.push_back({
            { "role", std::string(roleToString(msg.role)) },
            { "content", std::move(contentArray) },
        });
    }
    body["messages"] = std::move(messagesArray);

    // Include tools array only if tools are provided.
    if (!tools.empty())
    {
        auto toolsArray = nlohmann::json::array();
        for (auto const& tool: tools)
        {
            toolsArray.push_back({
                { "name", tool.name },
                { "description", tool.description },
                { "input_schema", tool.inputSchema },
            });
        }
        body["tools"] = std::move(toolsArray);
    }

    return body;
}

auto ClaudeProvider::parseSseEvent(http::SseEvent const& event,
                                   std::vector<ContentBlockAccumulator>& accumulators)
    -> std::expected<ClaudeSseResult, ProviderError>
{
    auto result = ClaudeSseResult {};

    if (event.event == "message_start")
    {
        // Initialize — nothing specific to track at message level.
        return result;
    }

    if (event.event == "message_stop")
    {
        result.done = true;
        return result;
    }

    if (event.event == "message_delta")
    {
        // May contain stop_reason; we handle completion via message_stop.
        return result;
    }

    if (event.event == "ping")
        return result;

    // Parse the data payload for content block events.
    auto data = nlohmann::json {};
    try
    {
        data = nlohmann::json::parse(event.data);
    }
    catch (nlohmann::json::parse_error const& e)
    {
        return std::unexpected(
            ProviderError { .code = ProviderErrorCode::InvalidResponse,
                            .message = std::format("Failed to parse SSE data: {}", e.what()) });
    }

    if (event.event == "content_block_start")
    {
        auto const index = data.value("index", 0);
        auto const& contentBlock = data["content_block"];
        auto const type = contentBlock.value("type", std::string {});

        // Ensure accumulator vector is large enough.
        if (index >= accumulators.size())
            accumulators.resize(index + 1);

        auto& acc = accumulators[index];
        acc.type = type;

        if (type == "tool_use")
        {
            acc.toolId = contentBlock.value("id", std::string {});
            acc.toolName = contentBlock.value("name", std::string {});
        }

        return result;
    }

    if (event.event == "content_block_delta")
    {
        auto const index = data.value("index", 0);
        auto const& delta = data["delta"];
        auto const deltaType = delta.value("type", std::string {});

        if (index >= accumulators.size())
            return std::unexpected(
                ProviderError { .code = ProviderErrorCode::InvalidResponse,
                                .message = std::format("Content block delta index {} out of range", index) });

        auto& acc = accumulators[index];

        if (deltaType == "text_delta")
        {
            auto const text = delta.value("text", std::string {});
            acc.text += text;
            result.textDelta = text;
        }
        else if (deltaType == "thinking_delta")
        {
            // Accumulate thinking text silently (not streamed to user).
            acc.text += delta.value("thinking", std::string {});
        }
        else if (deltaType == "signature_delta")
        {
            // Thinking block signature — ignored (we don't pass thinking blocks back).
        }
        else if (deltaType == "input_json_delta")
        {
            acc.toolArgumentsJson += delta.value("partial_json", std::string {});
        }

        return result;
    }

    if (event.event == "content_block_stop")
    {
        auto const index = data.value("index", 0);

        if (index >= accumulators.size())
            return std::unexpected(
                ProviderError { .code = ProviderErrorCode::InvalidResponse,
                                .message = std::format("Content block stop index {} out of range", index) });

        auto& acc = accumulators[index];

        if (acc.type == "thinking" || acc.type == "redacted_thinking")
        {
            // Thinking blocks are internal reasoning — not included in output content.
        }
        else if (acc.type == "text")
        {
            result.completedBlocks.emplace_back(TextBlock { .text = std::move(acc.text) });
        }
        else if (acc.type == "tool_use")
        {
            auto arguments = nlohmann::json::object();
            if (!acc.toolArgumentsJson.empty())
            {
                try
                {
                    arguments = nlohmann::json::parse(acc.toolArgumentsJson);
                }
                catch (nlohmann::json::parse_error const&)
                {
                    return std::unexpected(ProviderError {
                        .code = ProviderErrorCode::InvalidResponse,
                        .message = std::format("Failed to parse tool arguments for '{}'", acc.toolName) });
                }
            }

            result.completedBlocks.emplace_back(
                ToolUseBlock { .id = acc.toolId, .name = acc.toolName, .arguments = arguments });
            result.completedToolCalls.emplace_back(
                ToolCall { .id = acc.toolId, .name = acc.toolName, .arguments = std::move(arguments) });
        }

        return result;
    }

    // Unknown event types are silently ignored.
    return result;
}

auto ClaudeProvider::mapHttpError(long statusCode, std::string message) -> ProviderError
{
    if (statusCode == 401)
        return ProviderError { .code = ProviderErrorCode::AuthenticationError,
                               .message = std::move(message),
                               .httpStatus = 401 };
    if (statusCode == 429)
        return ProviderError { .code = ProviderErrorCode::RateLimitError,
                               .message = std::move(message),
                               .httpStatus = 429 };
    if (statusCode >= 500)
        return ProviderError { .code = ProviderErrorCode::ServerError,
                               .message = std::move(message),
                               .httpStatus = static_cast<int>(statusCode) };

    return ProviderError { .code = ProviderErrorCode::Unknown,
                           .message = std::move(message),
                           .httpStatus = static_cast<int>(statusCode) };
}

} // namespace endo::agent
