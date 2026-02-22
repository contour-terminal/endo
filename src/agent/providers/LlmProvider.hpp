// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <expected>
#include <functional>
#include <span>
#include <string>

#include <agent/Types.hpp>

namespace endo::agent
{

/// Callback type for refreshing an expired OAuth token.
/// Returns the new access token on success, or an error message.
using TokenRefresher = std::function<std::expected<std::string, std::string>()>;

/// Abstract interface for LLM providers.
///
/// Each concrete provider (Claude, OpenAI, Gemini) implements this interface
/// to provide a unified API for generating text and tool calls.
class LlmProvider
{
  public:
    virtual ~LlmProvider() = default;

    /// Generates a response from the model.
    /// @param messages    The conversation history.
    /// @param tools       Tool definitions available to the model.
    /// @param streamCb    Optional callback for streaming text tokens as they arrive.
    /// @return The complete generation result, or an error.
    [[nodiscard]] virtual auto generate(std::span<ChatMessage const> messages,
                                        std::span<ToolDefinition const> tools,
                                        StreamCallback streamCb)
        -> std::expected<GenerateResult, ProviderError> = 0;

    /// Returns whether this provider/model supports tool use.
    [[nodiscard]] virtual auto supportsToolUse() const noexcept -> bool = 0;

    /// Returns whether this provider/model supports image input.
    [[nodiscard]] virtual auto supportsImageInput() const noexcept -> bool = 0;

    /// Returns whether this provider/model supports image output.
    [[nodiscard]] virtual auto supportsImageOutput() const noexcept -> bool = 0;

    /// Returns the maximum context window size in tokens.
    [[nodiscard]] virtual auto contextSize() const noexcept -> size_t = 0;

    /// Returns detailed information about this provider and model.
    [[nodiscard]] virtual auto modelInfo() const -> ModelInfo = 0;
};

} // namespace endo::agent
