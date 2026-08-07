// SPDX-License-Identifier: Apache-2.0
#include <crispy/Base64.hpp>

#include <format>
#include <map>
#include <optional>

#include <agent/providers/OpenAiProvider.hpp>
#include <agent/providers/ProviderUtils.hpp>

using namespace std::string_view_literals;

namespace endo::agent
{

namespace
{

    /// Converts a ChatMessage content block to OpenAI JSON format.
    auto serializeContentBlock(ContentBlock const& block) -> nlohmann::json
    {
        return std::visit(
            [](auto const& b) -> nlohmann::json {
                using T = std::decay_t<decltype(b)>;
                if constexpr (std::is_same_v<T, TextBlock>)
                {
                    return nlohmann::json { { "type", "text" }, { "text", b.text } };
                }
                else if constexpr (std::is_same_v<T, ImageBlock>)
                {
                    auto const encoded = crispy::base64::encode(b.data.begin(), b.data.end());
                    auto const dataUrl = std::format("data:{};base64,{}", b.mediaType, encoded);
                    return nlohmann::json { { "type", "image_url" },
                                            { "image_url", { { "url", dataUrl } } } };
                }
                else if constexpr (std::is_same_v<T, ToolUseBlock>)
                {
                    // ToolUseBlock is serialized at the message level, not as content.
                    return nlohmann::json {};
                }
                else if constexpr (std::is_same_v<T, ToolResultBlock>)
                {
                    // ToolResultBlock is serialized as a separate "tool" role message.
                    return nlohmann::json {};
                }
            },
            block);
    }

    /// Checks whether a message contains any image blocks.
    auto hasImageContent(std::vector<ContentBlock> const& content) -> bool
    {
        return std::ranges::any_of(
            content, [](auto const& block) { return std::holds_alternative<ImageBlock>(block); });
    }

    /// Serializes a single ChatMessage into one or more OpenAI message JSON objects.
    auto serializeMessage(ChatMessage const& msg) -> std::vector<nlohmann::json>
    {
        auto results = std::vector<nlohmann::json> {};

        // Handle ToolResultBlocks in User messages (provider-agnostic convention).
        // When AgentSession puts tool results in User-role messages, serialize as "tool" role.
        auto const hasToolResults = std::ranges::any_of(
            msg.content, [](auto const& block) { return std::holds_alternative<ToolResultBlock>(block); });

        if (msg.role == Role::Tool || (msg.role == Role::User && hasToolResults))
        {
            // Each ToolResultBlock becomes a separate "tool" role message.
            for (auto const& block: msg.content)
            {
                if (auto const* result = std::get_if<ToolResultBlock>(&block))
                {
                    results.push_back(nlohmann::json {
                        { "role", "tool" },
                        { "tool_call_id", result->toolUseId },
                        { "content", result->content },
                    });
                }
            }
            return results;
        }

        if (msg.role == Role::Assistant)
        {
            // Collect text content and tool calls separately.
            auto textParts = std::string {};
            auto toolCalls = nlohmann::json::array();

            for (auto const& block: msg.content)
            {
                if (auto const* text = std::get_if<TextBlock>(&block))
                {
                    if (!textParts.empty())
                        textParts += '\n';
                    textParts += text->text;
                }
                else if (auto const* tool = std::get_if<ToolUseBlock>(&block))
                {
                    toolCalls.push_back(nlohmann::json {
                        { "id", tool->id },
                        { "type", "function" },
                        { "function", { { "name", tool->name }, { "arguments", tool->arguments.dump() } } },
                    });
                }
            }

            auto message = nlohmann::json { { "role", "assistant" } };
            if (!textParts.empty())
                message["content"] = textParts;
            if (!toolCalls.empty())
                message["tool_calls"] = toolCalls;
            results.push_back(std::move(message));
            return results;
        }

        // System and User messages.
        auto const roleStr = std::string(roleToString(msg.role));

        if (!hasImageContent(msg.content))
        {
            // Text-only: use simple string content.
            auto text = std::string {};
            for (auto const& block: msg.content)
            {
                if (auto const* tb = std::get_if<TextBlock>(&block))
                {
                    if (!text.empty())
                        text += '\n';
                    text += tb->text;
                }
            }
            results.push_back(nlohmann::json { { "role", roleStr }, { "content", text } });
        }
        else
        {
            // Multimodal: use content array.
            auto contentArray = nlohmann::json::array();
            for (auto const& block: msg.content)
            {
                auto serialized = serializeContentBlock(block);
                if (!serialized.empty())
                    contentArray.push_back(std::move(serialized));
            }
            results.push_back(nlohmann::json { { "role", roleStr }, { "content", contentArray } });
        }
        return results;
    }

    /// Serializes tool definitions into OpenAI function calling format.
    auto serializeTools(std::span<ToolDefinition const> tools) -> nlohmann::json
    {
        auto result = nlohmann::json::array();
        for (auto const& tool: tools)
        {
            result.push_back(nlohmann::json {
                { "type", "function" },
                { "function",
                  { { "name", tool.name },
                    { "description", tool.description },
                    { "parameters", tool.inputSchema } } },
            });
        }
        return result;
    }

} // namespace

OpenAiProvider::OpenAiProvider(http::HttpClient const& httpClient, OpenAiProviderConfig config):
    _httpClient(httpClient), _config(std::move(config))
{
}

auto OpenAiProvider::serializeRequest(std::span<ChatMessage const> messages,
                                      std::span<ToolDefinition const> tools,
                                      std::string const& model,
                                      size_t maxTokens,
                                      ThinkingMode thinkingMode) -> nlohmann::json
{
    auto body = nlohmann::json {
        { "model", model },
        { "max_tokens", maxTokens },
        { "stream", true },
        { "stream_options", { { "include_usage", true } } },
    };

    // Apply reasoning_effort for models that support it (o-series, etc.).
    switch (thinkingMode)
    {
        case ThinkingMode::Off: body["reasoning_effort"] = "low"; break;
        case ThinkingMode::Normal:
            // Omit reasoning_effort to use the model's default behavior.
            break;
        case ThinkingMode::Extended: body["reasoning_effort"] = "high"; break;
    }

    auto messagesArray = nlohmann::json::array();
    for (auto const& msg: messages)
    {
        for (auto& serialized: serializeMessage(msg))
            messagesArray.push_back(std::move(serialized));
    }
    body["messages"] = std::move(messagesArray);

    if (!tools.empty())
        body["tools"] = serializeTools(tools);

    return body;
}

auto OpenAiProvider::parseSseData(std::string_view data) -> std::optional<nlohmann::json>
{
    auto const trimmed = std::string_view(data);
    if (trimmed == "[DONE]")
        return std::nullopt;

    return nlohmann::json::parse(trimmed, nullptr, false);
}

auto OpenAiProvider::mapHttpError(long statusCode, std::string const& body) -> ProviderError
{
    return mapHttpStatusToProviderError(statusCode, extractJsonErrorMessage(statusCode, body));
}

auto OpenAiProvider::generate(std::span<ChatMessage const> messages,
                              std::span<ToolDefinition const> tools,
                              StreamCallback streamCb) -> std::expected<GenerateResult, ProviderError>
{
    auto const requestBody =
        serializeRequest(messages, tools, _config.model, _config.maxTokens, _config.thinkingMode);

    auto headers = std::vector<std::string> { "Content-Type: application/json" };
    if (!_config.apiKey.empty())
        headers.push_back(std::format("Authorization: Bearer {}", _config.apiKey));

    auto request = http::HttpRequest {
        .url = std::format("{}/chat/completions", _config.baseUrl),
        .method = http::HttpMethod::Post,
        .headers = std::move(headers),
        .body = requestBody.dump(),
    };

    auto result = GenerateResult {};
    auto textAccumulator = std::string {};

    // Accumulate tool calls by index: maps delta index -> (id, name, accumulated arguments string).
    struct PendingToolCall
    {
        std::string id;
        std::string name;
        std::string arguments;
    };

    auto pendingToolCalls = std::map<int, PendingToolCall> {};
    auto finishReason = std::string {};

    auto responseBodyAccumulator = std::string {};

    auto const sseCallback = [&](http::SseEvent const& event) -> bool {
        if (!event.data.empty())
        {
            responseBodyAccumulator += event.data;
            responseBodyAccumulator += '\n';
        }

        auto const parsed = parseSseData(event.data);
        if (!parsed.has_value())
            return false; // [DONE] sentinel

        auto const& json = parsed.value();
        if (json.is_discarded())
            return true; // Skip malformed chunks.

        // Check for error response embedded in stream.
        if (json.contains("error"))
        {
            auto const& err = json["error"];
            result = GenerateResult {};
            return false;
        }

        // Extract usage from the final chunk (when include_usage is enabled).
        if (json.contains("usage") && !json["usage"].is_null())
        {
            auto const& u = json["usage"];
            auto usage = TokenUsage {};
            usage.inputTokens = u.value("prompt_tokens", int64_t { 0 });
            usage.outputTokens = u.value("completion_tokens", int64_t { 0 });
            // OpenAI provides cached tokens in prompt_tokens_details.
            if (u.contains("prompt_tokens_details") && !u["prompt_tokens_details"].is_null())
                usage.cacheReadTokens = u["prompt_tokens_details"].value("cached_tokens", int64_t { 0 });
            result.usage = usage;
        }

        if (!json.contains("choices") || json["choices"].empty())
            return true;

        auto const& choice = json["choices"][0];

        // Track finish reason.
        if (choice.contains("finish_reason") && !choice["finish_reason"].is_null())
            finishReason = choice["finish_reason"].get<std::string>();

        if (!choice.contains("delta"))
            return true;

        auto const& delta = choice["delta"];

        // Text content delta.
        if (delta.contains("content") && !delta["content"].is_null())
        {
            auto const token = delta["content"].get<std::string>();
            textAccumulator += token;
            if (streamCb)
            {
                if (!streamCb(token))
                    return false; // Abort streaming on cancellation.
            }
        }

        // Tool call deltas.
        if (delta.contains("tool_calls"))
        {
            for (auto const& tc: delta["tool_calls"])
            {
                auto const index = tc.value("index", 0);
                auto& pending = pendingToolCalls[index];

                if (tc.contains("id"))
                    pending.id = tc["id"].get<std::string>();

                if (tc.contains("function"))
                {
                    auto const& fn = tc["function"];
                    if (fn.contains("name"))
                        pending.name = fn["name"].get<std::string>();
                    if (fn.contains("arguments"))
                        pending.arguments += fn["arguments"].get<std::string>();
                }
            }
        }

        return true;
    };

    auto errorBody = std::string {};
    auto const streamResult = _httpClient.executeStreaming(request, sseCallback, &errorBody);
    if (!streamResult.has_value())
    {
        return std::unexpected(ProviderError { .code = ProviderErrorCode::NetworkError,
                                               .message = streamResult.error().message,
                                               .httpStatus = 0,
                                               .requestUrl = request.url,
                                               .requestBody = request.body });
    }

    auto const httpStatus = streamResult.value();
    if (httpStatus < 200 || httpStatus >= 300)
    {
        auto error = mapHttpError(httpStatus, errorBody);
        error.requestUrl = request.url;
        error.requestBody = request.body;
        error.responseBody = std::move(errorBody);
        return std::unexpected(std::move(error));
    }

    // Assemble text content.
    if (!textAccumulator.empty())
        result.content.emplace_back(TextBlock { .text = std::move(textAccumulator) });

    // Assemble tool calls.
    if (finishReason == "tool_calls")
    {
        for (auto& [index, pending]: pendingToolCalls)
        {
            auto arguments = nlohmann::json::parse(pending.arguments, nullptr, false);
            if (arguments.is_discarded())
                arguments = nlohmann::json::object();

            result.toolCalls.push_back(ToolCall {
                .id = std::move(pending.id),
                .name = std::move(pending.name),
                .arguments = std::move(arguments),
            });

            result.content.emplace_back(ToolUseBlock {
                .id = result.toolCalls.back().id,
                .name = result.toolCalls.back().name,
                .arguments = result.toolCalls.back().arguments,
            });
        }
    }

    // Populate HTTP I/O context for trace logging.
    result.requestUrl = request.url;
    result.requestBody = std::move(request.body);
    result.responseBody = std::move(responseBodyAccumulator);

    return result;
}

auto OpenAiProvider::supportsToolUse() const noexcept -> bool
{
    return _config.supportsTools;
}

auto OpenAiProvider::supportsImageInput() const noexcept -> bool
{
    return _config.supportsImages;
}

auto OpenAiProvider::supportsImageOutput() const noexcept -> bool
{
    return false;
}

auto OpenAiProvider::contextSize() const noexcept -> size_t
{
    return _config.contextWindowSize;
}

auto OpenAiProvider::modelInfo() const -> ModelInfo
{
    return ModelInfo {
        .providerName = "openai",
        .modelName = _config.model,
        .contextSize = _config.contextWindowSize,
        .supportsToolUse = _config.supportsTools,
        .supportsImageInput = _config.supportsImages,
        .supportsImageOutput = false,
    };
}

} // namespace endo::agent
