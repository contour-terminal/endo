// SPDX-License-Identifier: Apache-2.0
#include "GeminiProvider.hpp"

#include <http/HttpClient.hpp>

#include <crispy/base64.h>

#include <format>
#include <ranges>

#include <agent/providers/ProviderUtils.hpp>

namespace endo::agent
{

GeminiProvider::GeminiProvider(http::HttpClient const& httpClient, GeminiProviderConfig config):
    _httpClient { httpClient }, _config { std::move(config) }
{
}

auto GeminiProvider::buildUrl() const -> std::string
{
    if (_config.useOAuth)
        return "https://cloudcode-pa.googleapis.com/v1internal:streamGenerateContent?alt=sse";
    return std::format(
        "https://generativelanguage.googleapis.com/v1beta/models/{}:streamGenerateContent?alt=sse&key={}",
        _config.model,
        _config.apiKey);
}

auto GeminiProvider::wrapCodeAssistRequest(nlohmann::json innerRequest) const -> nlohmann::json
{
    return nlohmann::json {
        { "model", _config.model },
        { "request", std::move(innerRequest) },
    };
}

auto GeminiProvider::ensureCodeAssistOnboarded() -> std::optional<ProviderError>
{
    if (_codeAssistOnboarded || !_config.useOAuth)
        return std::nullopt;

    auto const headers = buildAuthHeaders();

    // Try loadCodeAssist first — succeeds if user is already onboarded.
    auto loadRequest = http::HttpRequest {};
    loadRequest.url = "https://cloudcode-pa.googleapis.com/v1internal:loadCodeAssist";
    loadRequest.method = http::HttpMethod::Post;
    loadRequest.headers = headers;
    loadRequest.body = "{}";
    loadRequest.timeout = std::chrono::seconds(30);

    auto loadResult = _httpClient.execute(loadRequest);
    if (loadResult.has_value() && loadResult->statusCode == 200)
    {
        _codeAssistOnboarded = true;
        return std::nullopt;
    }

    // Onboard the user.
    auto onboardRequest = http::HttpRequest {};
    onboardRequest.url = "https://cloudcode-pa.googleapis.com/v1internal:onboardUser";
    onboardRequest.method = http::HttpMethod::Post;
    onboardRequest.headers = headers;
    onboardRequest.body = "{}";
    onboardRequest.timeout = std::chrono::seconds(30);

    auto onboardResult = _httpClient.execute(onboardRequest);
    if (!onboardResult.has_value())
        return ProviderError { .code = ProviderErrorCode::NetworkError,
                               .message = std::format("Code Assist onboarding failed: {}",
                                                      onboardResult.error().message),
                               .httpStatus = 0 };

    if (onboardResult->statusCode != 200)
        return ProviderError {
            .code = ProviderErrorCode::AuthenticationError,
            .message = std::format("Code Assist onboarding failed (HTTP {}): {}",
                                   onboardResult->statusCode,
                                   onboardResult->body.substr(0, 500)),
            .httpStatus = static_cast<int>(onboardResult->statusCode),
        };

    _codeAssistOnboarded = true;
    return std::nullopt;
}

auto GeminiProvider::buildAuthHeaders() const -> std::vector<std::string>
{
    if (_config.useOAuth)
        return { "Content-Type: application/json", std::format("Authorization: Bearer {}", _config.apiKey) };
    return { "Content-Type: application/json" };
}

auto GeminiProvider::mapHttpError(long statusCode, std::string const& body) -> ProviderError
{
    return mapHttpStatusToProviderError(
        statusCode, extractJsonErrorMessage(statusCode, body), /*treat403AsAuth=*/true);
}

auto GeminiProvider::findToolName(std::span<ChatMessage const> messages, std::string_view toolUseId)
    -> std::string
{
    // Walk backwards through messages to find a ToolUseBlock with matching id.
    for (const auto& message: std::ranges::reverse_view(messages))
    {
        for (auto const& block: message.content)
        {
            if (auto const* toolUse = std::get_if<ToolUseBlock>(&block))
            {
                if (toolUse->id == toolUseId)
                    return toolUse->name;
            }
        }
    }
    // Fallback: use the toolUseId itself as the name.
    return std::string(toolUseId);
}

auto GeminiProvider::serializeRequest(std::span<ChatMessage const> messages,
                                      std::span<ToolDefinition const> tools,
                                      size_t maxTokens,
                                      ThinkingMode thinkingMode) -> nlohmann::json
{
    auto request = nlohmann::json::object();

    // Extract system instruction from system messages.
    auto systemText = std::string {};
    auto contents = nlohmann::json::array();

    for (auto const& msg: messages)
    {
        if (msg.role == Role::System)
        {
            // System messages go into systemInstruction.
            if (!systemText.empty())
                systemText += '\n';
            systemText += msg.textContent();
            continue;
        }

        // Determine Gemini role: "user" or "model".
        const auto* const geminiRole = (msg.role == Role::Assistant) ? "model" : "user";

        // Check if this message contains ToolResultBlocks — they need special handling.
        auto hasToolResults = false;
        for (auto const& block: msg.content)
        {
            if (std::holds_alternative<ToolResultBlock>(block))
            {
                hasToolResults = true;
                break;
            }
        }

        if (hasToolResults)
        {
            // ToolResultBlocks must be in a "user" role entry with functionResponse parts.
            auto parts = nlohmann::json::array();
            for (auto const& block: msg.content)
            {
                if (auto const* toolResult = std::get_if<ToolResultBlock>(&block))
                {
                    auto const toolName = findToolName(messages, toolResult->toolUseId);
                    parts.push_back(
                        nlohmann::json { { "functionResponse",
                                           { { "name", toolName },
                                             { "response", { { "content", toolResult->content } } } } } });
                }
                else if (auto const* text = std::get_if<TextBlock>(&block))
                {
                    if (!text->text.empty())
                        parts.push_back(nlohmann::json { { "text", text->text } });
                }
            }
            if (!parts.empty())
                contents.push_back(nlohmann::json { { "role", "user" }, { "parts", std::move(parts) } });
            continue;
        }

        // Regular message: serialize all content blocks.
        auto parts = nlohmann::json::array();
        for (auto const& block: msg.content)
        {
            if (auto const* text = std::get_if<TextBlock>(&block))
            {
                parts.push_back(nlohmann::json { { "text", text->text } });
            }
            else if (auto const* image = std::get_if<ImageBlock>(&block))
            {
                auto const encoded = crispy::base64::encode(image->data.begin(), image->data.end());
                parts.push_back(nlohmann::json {
                    { "inlineData", { { "mimeType", image->mediaType }, { "data", encoded } } } });
            }
            else if (auto const* toolUse = std::get_if<ToolUseBlock>(&block))
            {
                parts.push_back(nlohmann::json {
                    { "functionCall", { { "name", toolUse->name }, { "args", toolUse->arguments } } } });
            }
        }
        if (!parts.empty())
            contents.push_back(nlohmann::json { { "role", geminiRole }, { "parts", std::move(parts) } });
    }

    if (!systemText.empty())
        request["systemInstruction"] =
            nlohmann::json { { "parts", nlohmann::json::array({ { { "text", systemText } } }) } };

    request["contents"] = std::move(contents);

    // Tool definitions.
    if (!tools.empty())
    {
        auto functionDeclarations = nlohmann::json::array();
        for (auto const& tool: tools)
        {
            auto decl = nlohmann::json { { "name", tool.name }, { "description", tool.description } };
            if (!tool.inputSchema.is_null() && !tool.inputSchema.empty())
                decl["parameters"] = tool.inputSchema;
            functionDeclarations.push_back(std::move(decl));
        }
        request["tools"] =
            nlohmann::json::array({ { { "functionDeclarations", std::move(functionDeclarations) } } });
    }

    // Generation config with optional thinking budget.
    auto genConfig = nlohmann::json { { "maxOutputTokens", maxTokens } };
    if (thinkingMode != ThinkingMode::Off)
    {
        // Gemini thinkingConfig: thinkingBudget controls how many tokens for thinking.
        // 0 = disabled, positive value = budget. -1 = dynamic (let the model decide).
        int thinkingBudget = (thinkingMode == ThinkingMode::Extended) ? 24576 : 8192;
        genConfig["thinkingConfig"] = { { "thinkingBudget", thinkingBudget } };
    }
    request["generationConfig"] = std::move(genConfig);

    return request;
}

auto GeminiProvider::executeStreaming(http::HttpRequest const& request, StreamCallback const& streamCb)
    -> std::expected<GenerateResult, ProviderError>
{
    auto result = GenerateResult {};
    auto accumulatedText = std::string {};
    auto toolCallIdCounter = 0;
    auto errorBody = std::string {};
    auto responseBodyAccumulator = std::string {};

    auto const sseResult = _httpClient.executeStreaming(
        request,
        [&](http::SseEvent const& event) -> bool {
            if (!event.data.empty())
            {
                responseBodyAccumulator += event.data;
                responseBodyAccumulator += '\n';
            }

            if (event.data.empty() || event.data == "[DONE]")
                return true;

            auto parsed = nlohmann::json {};
            try
            {
                parsed = nlohmann::json::parse(event.data);
            }
            catch (nlohmann::json::parse_error const&)
            {
                return true; // Skip malformed events.
            }

            // Unwrap Code Assist response envelope if present.
            if (parsed.contains("response"))
                parsed = std::move(parsed["response"]);

            // Check for error response.
            if (parsed.contains("error"))
                return false;

            // Extract usage metadata (appears in each chunk, last one has final counts).
            if (parsed.contains("usageMetadata"))
            {
                auto const& u = parsed["usageMetadata"];
                auto usage = TokenUsage {};
                usage.inputTokens = u.value("promptTokenCount", int64_t { 0 });
                usage.outputTokens = u.value("candidatesTokenCount", int64_t { 0 });
                usage.cacheReadTokens = u.value("cachedContentTokenCount", int64_t { 0 });
                result.usage = usage;
            }

            // Extract parts from candidates[0].content.parts[].
            if (!parsed.contains("candidates") || parsed["candidates"].empty())
                return true;

            auto const& candidate = parsed["candidates"][0];
            if (!candidate.contains("content") || !candidate["content"].contains("parts"))
                return true;

            for (auto const& part: candidate["content"]["parts"])
            {
                if (part.contains("text"))
                {
                    auto const& text = part["text"].get<std::string>();
                    accumulatedText += text;
                    if (streamCb)
                    {
                        if (!streamCb(text))
                            return false; // Abort streaming on cancellation.
                    }
                }
                else if (part.contains("functionCall"))
                {
                    auto const& funcCall = part["functionCall"];
                    auto toolCall = ToolCall {};
                    toolCall.name = funcCall["name"].get<std::string>();
                    toolCall.arguments = funcCall.value("args", nlohmann::json::object());
                    toolCall.id = std::format("call_{}", toolCallIdCounter++);
                    result.toolCalls.push_back(std::move(toolCall));
                }
                else if (part.contains("inlineData"))
                {
                    auto const& inlineData = part["inlineData"];
                    auto imageBlock = ImageBlock {};
                    imageBlock.mediaType = inlineData.value("mimeType", "image/png");
                    auto const& b64Data = inlineData["data"].get<std::string>();
                    imageBlock.data.resize(crispy::base64::decodeLength(b64Data));
                    imageBlock.data.resize(crispy::base64::decode(b64Data, imageBlock.data.data()));
                    result.content.emplace_back(std::move(imageBlock));
                }
            }

            return true;
        },
        &errorBody);

    if (!sseResult.has_value())
        return std::unexpected(
            ProviderError { .code = ProviderErrorCode::NetworkError,
                            .message = std::format("HTTP request failed: {}", sseResult.error().message),
                            .httpStatus = 0 });

    auto const statusCode = sseResult.value();
    if (statusCode != 200)
    {
        auto error = mapHttpError(statusCode, errorBody);
        error.responseBody = std::move(errorBody);
        return std::unexpected(std::move(error));
    }

    if (!accumulatedText.empty())
        result.content.emplace_back(TextBlock { .text = std::move(accumulatedText) });

    // Convert tool calls into ToolUseBlock content entries as well.
    for (auto const& toolCall: result.toolCalls)
        result.content.emplace_back(
            ToolUseBlock { .id = toolCall.id, .name = toolCall.name, .arguments = toolCall.arguments });

    result.responseBody = std::move(responseBodyAccumulator);
    return result;
}

auto GeminiProvider::generate(std::span<ChatMessage const> messages,
                              std::span<ToolDefinition const> tools,
                              StreamCallback streamCb) -> std::expected<GenerateResult, ProviderError>
{
    if (auto onboardError = ensureCodeAssistOnboarded())
        return std::unexpected(std::move(*onboardError));

    auto requestBody = serializeRequest(messages, tools, _config.maxTokens, _config.thinkingMode);
    if (_config.useOAuth)
        requestBody = wrapCodeAssistRequest(std::move(requestBody));

    auto request = http::HttpRequest {};
    request.url = buildUrl();
    request.method = http::HttpMethod::Post;
    request.headers = buildAuthHeaders();
    request.body = requestBody.dump();

    auto result = executeStreaming(request, streamCb);

    // On 401/403 with OAuth, attempt a token refresh and retry once.
    if (!result.has_value() && (result.error().httpStatus == 401 || result.error().httpStatus == 403)
        && _config.useOAuth && _config.tokenRefresher)
    {
        auto refreshed = _config.tokenRefresher();
        if (refreshed.has_value())
        {
            _config.apiKey = std::move(*refreshed);
            request.url = buildUrl();
            request.headers = buildAuthHeaders();
            result = executeStreaming(request, streamCb);
        }
        // If refresh failed, fall through and return the original error.
    }

    // Populate HTTP I/O context for trace logging.
    if (result.has_value())
    {
        result->requestUrl = request.url;
        result->requestBody = std::move(request.body);
    }
    else
    {
        result.error().requestUrl = request.url;
        result.error().requestBody = std::move(request.body);
    }

    return result;
}

auto GeminiProvider::supportsToolUse() const noexcept -> bool
{
    return true;
}

auto GeminiProvider::supportsImageInput() const noexcept -> bool
{
    return true;
}

auto GeminiProvider::supportsImageOutput() const noexcept -> bool
{
    return true;
}

auto GeminiProvider::contextSize() const noexcept -> size_t
{
    return _config.contextWindowSize;
}

auto GeminiProvider::modelInfo() const -> ModelInfo
{
    return ModelInfo { .providerName = "gemini",
                       .modelName = _config.model,
                       .contextSize = _config.contextWindowSize,
                       .supportsToolUse = true,
                       .supportsImageInput = true,
                       .supportsImageOutput = true };
}

} // namespace endo::agent
