// SPDX-License-Identifier: Apache-2.0
#include "ClaudeProvider.hpp"

#include <crispy/base64.h>

#include <format>

namespace endo::agent
{

ClaudeProvider::ClaudeProvider(http::HttpClient const& httpClient, ClaudeProviderConfig config):
    _httpClient(httpClient), _config(std::move(config))
{
}

auto ClaudeProvider::generate(std::span<ChatMessage const> messages,
                              std::span<ToolDefinition const> tools,
                              StreamCallback streamCb) -> std::expected<GenerateResult, ProviderError>
{
    if (_config.apiKey.empty())
        return std::unexpected(
            ProviderError { .code = ProviderErrorCode::ConfigError, .message = "API key is not configured" });

    auto const requestBody = serializeRequest(messages, tools, _config.model, _config.maxTokens);

    auto request = http::HttpRequest {};
    request.url = std::format("{}/v1/messages", _config.baseUrl);
    request.method = http::HttpMethod::Post;
    request.headers = {
        "Content-Type: application/json",
        std::format("x-api-key: {}", _config.apiKey),
        std::format("anthropic-version: {}", _config.apiVersion),
    };
    request.body = requestBody.dump();

    auto result = GenerateResult {};
    auto accumulators = std::vector<ContentBlockAccumulator> {};

    auto const sseResult = _httpClient.executeStreaming(request, [&](http::SseEvent const& event) -> bool {
        auto parsed = parseSseEvent(event, accumulators);
        if (!parsed.has_value())
            return false;

        if (!parsed->textDelta.empty() && streamCb)
            streamCb(parsed->textDelta);

        for (auto& block: parsed->completedBlocks)
            result.content.push_back(std::move(block));

        for (auto& toolCall: parsed->completedToolCalls)
            result.toolCalls.push_back(std::move(toolCall));

        return !parsed->done;
    });

    if (!sseResult.has_value())
        return std::unexpected(
            ProviderError { .code = ProviderErrorCode::NetworkError, .message = sseResult.error().message });

    auto const statusCode = sseResult.value();
    if (statusCode != 200)
        return std::unexpected(mapHttpError(statusCode, std::format("HTTP {}", statusCode)));

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
                                      size_t maxTokens) -> nlohmann::json
{
    auto body = nlohmann::json {
        { "model", model },
        { "max_tokens", maxTokens },
        { "stream", true },
    };

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

        if (acc.type == "text")
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
