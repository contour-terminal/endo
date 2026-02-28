// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <agent/Types.hpp>

namespace endo::agent::local
{

/// Supported chat template formats for local LLM inference.
enum class ChatTemplateFormat : uint8_t
{
    ChatML,  ///< OpenAI ChatML format (<|im_start|>/<|im_end|>).
    Llama3,  ///< Meta Llama 3.x format.
    Mistral, ///< Mistral AI format.
    Gemma,   ///< Google Gemma format.
    Phi3,    ///< Microsoft Phi-3 format.
    Qwen2,   ///< Alibaba Qwen 2.x format.
    Generic, ///< Fallback generic format (system/user/assistant markers).
};

/// Parses a chat template format name from a string.
/// @param name Template name (case-insensitive, e.g. "chatml", "llama3").
/// @return The matching format, or ChatTemplateFormat::Generic if unrecognized.
[[nodiscard]] auto chatTemplateFromString(std::string_view name) -> ChatTemplateFormat;

/// Formats a conversation into a prompt string for the given template format.
/// @param messages The conversation history.
/// @param tools Tool definitions (injected into system message when non-empty).
/// @param format The chat template format.
/// @return The formatted prompt string ready for tokenization.
[[nodiscard]] auto formatPrompt(std::span<ChatMessage const> messages,
                                std::span<ToolDefinition const> tools,
                                ChatTemplateFormat format) -> std::string;

/// Returns the stop token strings for a given template format.
/// @param format The chat template format.
/// @return A vector of stop strings to use during generation.
[[nodiscard]] auto stopTokens(ChatTemplateFormat format) -> std::vector<std::string>;

} // namespace endo::agent::local
