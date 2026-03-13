// SPDX-License-Identifier: Apache-2.0
#include "CopilotProvider.hpp"

#include <http/HttpClient.hpp>

#include <format>
#include <map>

#include <agent/providers/OpenAiProvider.hpp>
#include <agent/providers/ProviderUtils.hpp>

using namespace std::string_view_literals;

namespace endo::agent
{

namespace
{
    /// Base URL for the GitHub Copilot Chat API.
    constexpr auto CopilotApiBaseUrl = "https://api.githubcopilot.com";
} // namespace

CopilotProvider::CopilotProvider(http::HttpClient const& httpClient, CopilotProviderConfig config):
    _httpClient(httpClient), _config(std::move(config)), _sessionToken {}
{
}

auto CopilotProvider::ensureSessionToken() -> std::expected<std::string, ProviderError>
{
    if (!isCopilotTokenExpired(_sessionToken))
        return _sessionToken.token;

    // Exchange the GitHub token for a Copilot session token.
    auto exchangeResult = exchangeCopilotToken(_httpClient, _config.githubToken);

    if (!exchangeResult.has_value())
    {
        // If exchange failed with auth error, try refreshing the GitHub token.
        if (_config.tokenRefresher)
        {
            auto refreshed = _config.tokenRefresher();
            if (refreshed.has_value())
            {
                _config.githubToken = std::move(*refreshed);
                exchangeResult = exchangeCopilotToken(_httpClient, _config.githubToken);
            }
        }

        if (!exchangeResult.has_value())
        {
            return std::unexpected(ProviderError {
                .code = ProviderErrorCode::AuthenticationError,
                .message = std::string("Copilot token exchange failed: ") + exchangeResult.error(),
                .httpStatus = 401,
            });
        }
    }

    _sessionToken = std::move(*exchangeResult);
    return _sessionToken.token;
}

auto CopilotProvider::mapHttpError(long statusCode, std::string const& body) -> ProviderError
{
    return mapHttpStatusToProviderError(statusCode, extractJsonErrorMessage(statusCode, body));
}

auto CopilotProvider::generate(std::span<ChatMessage const> messages,
                               std::span<ToolDefinition const> tools,
                               StreamCallback streamCb) -> std::expected<GenerateResult, ProviderError>
{
    // Ensure we have a valid Copilot session token.
    auto const tokenResult = ensureSessionToken();
    if (!tokenResult.has_value())
        return std::unexpected(tokenResult.error());

    // Reuse OpenAI serialization — Copilot uses the exact same format.
    auto const requestBody = OpenAiProvider::serializeRequest(
        messages, tools, _config.model, _config.maxTokens, _config.thinkingMode);

    auto headers = std::vector<std::string> {
        "Content-Type: application/json",        std::format("Authorization: Bearer {}", *tokenResult),
        "Copilot-Integration-Id: vscode-chat",   "Editor-Version: Endo/1.0",
        "Editor-Plugin-Version: endo-agent/1.0",
    };

    auto request = http::HttpRequest {
        .url = std::format("{}/chat/completions", CopilotApiBaseUrl),
        .method = http::HttpMethod::Post,
        .headers = std::move(headers),
        .body = requestBody.dump(),
    };

    auto result = GenerateResult {};
    auto textAccumulator = std::string {};

    // Accumulate tool calls by index (same structure as OpenAiProvider).
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

        // Reuse OpenAI SSE parsing.
        auto const parsed = OpenAiProvider::parseSseData(event.data);
        if (!parsed.has_value())
            return false; // [DONE] sentinel

        auto const& json = parsed.value();
        if (json.is_discarded())
            return true; // Skip malformed chunks.

        // Check for error response embedded in stream.
        if (json.contains("error"))
            return false;

        // Extract usage from the final chunk.
        if (json.contains("usage") && !json["usage"].is_null())
        {
            auto const& u = json["usage"];
            auto usage = TokenUsage {};
            usage.inputTokens = u.value("prompt_tokens", int64_t { 0 });
            usage.outputTokens = u.value("completion_tokens", int64_t { 0 });
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
        // On 401, invalidate the session token so it's refreshed on next call.
        if (httpStatus == 401)
            _sessionToken = {};

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

auto CopilotProvider::supportsToolUse() const noexcept -> bool
{
    return true;
}

auto CopilotProvider::supportsImageInput() const noexcept -> bool
{
    return true;
}

auto CopilotProvider::supportsImageOutput() const noexcept -> bool
{
    return false;
}

auto CopilotProvider::contextSize() const noexcept -> size_t
{
    return _config.contextWindowSize;
}

auto CopilotProvider::modelInfo() const -> ModelInfo
{
    return ModelInfo {
        .providerName = "copilot",
        .modelName = _config.model,
        .contextSize = _config.contextWindowSize,
        .supportsToolUse = true,
        .supportsImageInput = true,
        .supportsImageOutput = false,
    };
}

} // namespace endo::agent
