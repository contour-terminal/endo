// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <format>

#include <agent/ConversationCompactor.hpp>
#include <agent/TokenEstimator.hpp>

namespace endo::agent
{

ConversationCompactor::ConversationCompactor(LlmProvider& provider, CompactionConfig config):
    _provider(provider), _config(config)
{
}

auto ConversationCompactor::compactIfNeeded(ConversationHistory& history) -> std::expected<bool, std::string>
{
    auto const contextLimit = _provider.contextSize();
    if (contextLimit == 0)
        return false;

    auto const threshold = static_cast<size_t>(static_cast<double>(contextLimit) * _config.triggerThreshold);
    if (history.estimatedTokenCount() < threshold)
        return false;

    auto const [summarizeEnd, preserveStart] = partitionMessages(history);

    // Need at least some messages to summarize (beyond the system prompt at index 0)
    if (summarizeEnd <= 1)
        return false;

    auto const msgs = history.messages();

    // Extract messages to summarize (skip system prompt at index 0)
    auto toSummarize = msgs.subspan(1, summarizeEnd - 1);
    if (toSummarize.empty())
        return false;

    auto summaryResult = generateSummary(toSummarize);
    if (!summaryResult.has_value())
        return std::unexpected(summaryResult.error());

    // Build new message list: system prompt + summary + preserved messages
    auto newMessages = std::vector<ChatMessage> {};
    newMessages.reserve(1 + 1 + (msgs.size() - preserveStart));

    // Keep the original system prompt
    newMessages.push_back(msgs[0]);

    // Add summary as a system message
    newMessages.push_back(
        ChatMessage::text(Role::System, std::format("[Conversation summary]\n{}", *summaryResult)));

    // Add preserved recent messages
    for (auto i = preserveStart; i < msgs.size(); ++i)
        newMessages.push_back(msgs[i]);

    history.replaceMessages(std::move(newMessages));
    return true;
}

auto ConversationCompactor::generateSummary(std::span<ChatMessage const> messages)
    -> std::expected<std::string, std::string>
{
    // Build a prompt asking the LLM to summarize the conversation
    auto summaryMessages = std::vector<ChatMessage> {};
    summaryMessages.push_back(ChatMessage::text(
        Role::System,
        "You are a conversation summarizer. Summarize the following conversation concisely, "
        "preserving key facts, decisions, code references, file paths, and important context. "
        "Focus on information that would be needed to continue the conversation."));

    // Collect the conversation text for summarization
    auto conversationText = std::string {};
    for (auto const& msg: messages)
    {
        auto const roleStr = roleToString(msg.role);
        auto const text = msg.textContent();
        if (!text.empty())
            conversationText += std::format("[{}]: {}\n\n", roleStr, text);
    }

    summaryMessages.push_back(ChatMessage::text(Role::User, std::move(conversationText)));

    auto result = _provider.generate(summaryMessages, {}, nullptr);
    if (!result.has_value())
        return std::unexpected(std::format("Failed to generate summary: {}", result.error().message));

    return result->textContent();
}

auto ConversationCompactor::partitionMessages(ConversationHistory const& history) const
    -> std::pair<size_t, size_t>
{
    auto const msgs = history.messages();
    auto const totalMessages = msgs.size();

    if (totalMessages <= _config.preserveLastMessages + 1) // +1 for system prompt
        return { 1, 1 };                                   // Nothing to summarize

    auto preserveStart = totalMessages - _config.preserveLastMessages;

    // Scan backward from preserve boundary to include pending tool result chains.
    // A tool result (User message with ToolResultBlock) must be kept together with
    // the preceding assistant message (with ToolUseBlock).
    while (preserveStart > 1)
    {
        auto const& msg = msgs[preserveStart];
        if (msg.role != Role::User)
            break;

        auto const hasToolResult = std::ranges::any_of(
            msg.content, [](auto const& block) { return std::holds_alternative<ToolResultBlock>(block); });

        if (!hasToolResult)
            break;

        // Include this tool result message and its preceding assistant message
        preserveStart = (preserveStart >= 2) ? preserveStart - 2 : 1;
    }

    // summarizeEnd is where summarization stops (exclusive), which is preserveStart
    return { preserveStart, preserveStart };
}

} // namespace endo::agent
