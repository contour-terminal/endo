// SPDX-License-Identifier: Apache-2.0
#include <agent/HeadlessRunner.hpp>

namespace endo::agent
{

auto toJson(HeadlessRunResult const& result) -> nlohmann::json
{
    auto toolCallsJson = nlohmann::json::array();
    for (auto const& tc: result.toolCalls)
    {
        toolCallsJson.push_back({
            { "name", tc.name },
            { "arguments", tc.arguments },
            { "result", tc.result },
            { "is_error", tc.isError },
            { "duration_ms", tc.duration.count() },
        });
    }

    auto json = nlohmann::json {
        { "success", result.success },
        { "response", result.response },
        { "tool_calls", std::move(toolCallsJson) },
        {
            "token_usage",
            {
                { "input_tokens", result.tokenUsage.inputTokens },
                { "output_tokens", result.tokenUsage.outputTokens },
                { "cache_read_tokens", result.tokenUsage.cacheReadTokens },
                { "cache_write_tokens", result.tokenUsage.cacheCreationTokens },
            },
        },
        { "turn_count", result.turnCount },
        { "provider", result.providerName },
        { "model", result.modelName },
    };

    if (!result.success && !result.errorMessage.empty())
        json["error"] = result.errorMessage;

    return json;
}

} // namespace endo::agent
