// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <expected>
#include <span>
#include <string>
#include <utility>

#include <agent/ConversationHistory.hpp>
#include <agent/providers/LlmProvider.hpp>
#include <agent/Types.hpp>

namespace endo::agent
{

/// Configuration for conversation compaction behavior.
struct CompactionConfig
{
    double triggerThreshold = 0.8;    ///< Fraction of context window that triggers compaction.
    size_t preserveLastMessages = 10; ///< Number of recent messages to keep uncompacted.
    size_t summaryMaxTokens = 2048;   ///< Maximum tokens for the summary.
};

/// Compacts conversation history by summarizing old messages via the LLM provider.
///
/// When the conversation's estimated token count approaches the provider's context window,
/// the compactor summarizes older messages into a single summary message while preserving
/// the system prompt and recent messages.
class ConversationCompactor
{
  public:
    /// @brief Constructs a compactor with the given provider and configuration.
    /// @param provider The LLM provider used to generate summaries.
    /// @param config Compaction configuration.
    ConversationCompactor(LlmProvider& provider, CompactionConfig config = {});

    /// @brief Checks whether compaction is needed and performs it if so.
    ///
    /// Compaction is triggered when the estimated token count exceeds
    /// `triggerThreshold * provider.contextSize()`. Old messages are summarized
    /// into a single system message, preserving the system prompt and recent messages.
    /// @param history The conversation history to potentially compact.
    /// @return true if compaction was performed, false if not needed, or an error.
    [[nodiscard]] auto compactIfNeeded(ConversationHistory& history) -> std::expected<bool, std::string>;

  private:
    /// Generates a summary of the given messages using the LLM provider.
    [[nodiscard]] auto generateSummary(std::span<ChatMessage const> messages)
        -> std::expected<std::string, std::string>;

    /// Determines the partition points for summarization.
    /// @return A pair of (summarize_end_index, preserve_start_index).
    [[nodiscard]] auto partitionMessages(ConversationHistory const& history) const
        -> std::pair<size_t, size_t>;

    LlmProvider& _provider;
    CompactionConfig _config;
};

} // namespace endo::agent
