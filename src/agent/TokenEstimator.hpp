// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <span>
#include <string_view>

#include <agent/Types.hpp>

namespace endo::agent
{

/// Estimates the token count for a text string using a character-based heuristic.
///
/// Uses ~4 characters per token for natural language and ~3.5 for code-dense text
/// (detected by high density of punctuation characters).
/// @param text The text to estimate tokens for.
/// @return Estimated number of tokens.
[[nodiscard]] auto estimateTokenCount(std::string_view text) noexcept -> size_t;

/// Estimates the token count for a single chat message.
///
/// Includes per-message overhead (4 tokens for role framing) plus content block estimates.
/// @param message The chat message to estimate tokens for.
/// @return Estimated number of tokens.
[[nodiscard]] auto estimateTokenCount(ChatMessage const& message) noexcept -> size_t;

/// Estimates the total token count for a span of chat messages.
/// @param messages The messages to estimate tokens for.
/// @return Estimated total number of tokens.
[[nodiscard]] auto estimateTokenCount(std::span<ChatMessage const> messages) noexcept -> size_t;

} // namespace endo::agent
