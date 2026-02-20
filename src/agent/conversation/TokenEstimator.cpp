// SPDX-License-Identifier: Apache-2.0
#include <algorithm>

#include <agent/conversation/TokenEstimator.hpp>

namespace endo::agent
{

namespace
{
    /// Threshold for the fraction of code-like punctuation characters that triggers
    /// the lower divisor (more tokens per character) heuristic.
    constexpr auto CodePunctuationThreshold = 0.15;

    /// Approximate characters per token for natural language.
    constexpr auto NaturalLanguageDivisor = 4.0;

    /// Approximate characters per token for code-like text.
    constexpr auto CodeDivisor = 3.5;

    /// Fixed overhead per message for role framing tokens.
    constexpr auto PerMessageOverhead = size_t { 4 };

    /// Fixed token estimate for image content blocks.
    constexpr auto ImageTokenEstimate = size_t { 1000 };

    [[nodiscard]] auto isCodePunctuation(char ch) noexcept -> bool
    {
        switch (ch)
        {
            case '{':
            case '}':
            case '[':
            case ']':
            case '(':
            case ')':
            case ';':
            case '=':
            case '<':
            case '>':
            case '|':
            case '&':
            case '#': return true;
            default: return false;
        }
    }

    [[nodiscard]] auto estimateContentBlockTokens(ContentBlock const& block) noexcept -> size_t
    {
        return std::visit(
            [](auto const& b) -> size_t {
                using T = std::decay_t<decltype(b)>;
                if constexpr (std::is_same_v<T, TextBlock>)
                    return estimateTokenCount(b.text);
                else if constexpr (std::is_same_v<T, ImageBlock>)
                    return ImageTokenEstimate;
                else if constexpr (std::is_same_v<T, ToolUseBlock>)
                    return estimateTokenCount(b.name) + estimateTokenCount(b.arguments.dump());
                else if constexpr (std::is_same_v<T, ToolResultBlock>)
                    return estimateTokenCount(b.content);
            },
            block);
    }
} // namespace

auto estimateTokenCount(std::string_view text) noexcept -> size_t
{
    if (text.empty())
        return 0;

    auto const punctCount = std::ranges::count_if(text, [](char ch) { return isCodePunctuation(ch); });
    auto const punctFraction = static_cast<double>(punctCount) / static_cast<double>(text.size());
    auto const divisor = punctFraction > CodePunctuationThreshold ? CodeDivisor : NaturalLanguageDivisor;

    return std::max(size_t { 1 }, static_cast<size_t>(static_cast<double>(text.size()) / divisor));
}

auto estimateTokenCount(ChatMessage const& message) noexcept -> size_t
{
    auto total = PerMessageOverhead;
    for (auto const& block: message.content)
        total += estimateContentBlockTokens(block);
    return total;
}

auto estimateTokenCount(std::span<ChatMessage const> messages) noexcept -> size_t
{
    auto total = size_t { 0 };
    for (auto const& msg: messages)
        total += estimateTokenCount(msg);
    return total;
}

} // namespace endo::agent
