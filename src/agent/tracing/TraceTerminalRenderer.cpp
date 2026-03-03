// SPDX-License-Identifier: Apache-2.0
#include <tui/TerminalOutput.hpp>

#include <cstdio>
#include <format>
#include <string>
#include <variant>

#include <agent/tracing/TraceTerminalRenderer.hpp>

namespace endo::agent
{

namespace
{
    /// Dim style for trace labels.
    constexpr auto dimStyle = tui::Style { .dim = true };

    /// Style for error trace lines.
    constexpr auto errorStyle = tui::Style { .fg = uint8_t { 196 }, .bold = true }; // bright red

    /// Style for the trace prefix label.
    constexpr auto prefixStyle = tui::Style { .fg = uint8_t { 243 }, .dim = true }; // gray

    /// Formats a duration in milliseconds as a human-readable string.
    auto formatDuration(std::chrono::milliseconds ms) -> std::string
    {
        if (ms.count() < 1000)
            return std::format("{}ms", ms.count());
        return std::format("{:.1f}s", static_cast<double>(ms.count()) / 1000.0);
    }

    /// Formats a token/byte count with K/M suffix.
    auto formatCount(size_t count) -> std::string
    {
        if (count < 1000)
            return std::format("{}", count);
        if (count < 100'000)
            return std::format("{:.1f}k", static_cast<double>(count) / 1000.0);
        return std::format("{:.1f}M", static_cast<double>(count) / 1'000'000.0);
    }

    /// Truncates a string to maxLen characters, appending "[... N more]" if truncated.
    auto truncateContent(std::string_view content, size_t maxLen = 2048) -> std::string
    {
        if (content.size() <= maxLen)
            return std::string(content);
        auto const remaining = content.size() - maxLen;
        return std::format("{}[... {} more bytes]", content.substr(0, maxLen), remaining);
    }

    /// Summarizes tool arguments as a compact string.
    auto summarizeArguments(nlohmann::json const& args) -> std::string
    {
        if (args.is_null() || args.empty())
            return "{}";

        auto result = std::string {};
        auto first = true;
        for (auto const& [key, val]: args.items())
        {
            if (!first)
                result += ", ";
            first = false;

            if (val.is_string())
            {
                auto const& s = val.get_ref<std::string const&>();
                if (s.size() > 60)
                    result += std::format("{}:\"{}...\"", key, s.substr(0, 57));
                else
                    result += std::format("{}:\"{}\"", key, s);
            }
            else
            {
                auto const dumped = val.dump();
                if (dumped.size() > 40)
                    result += std::format("{}:...", key);
                else
                    result += std::format("{}:{}", key, dumped);
            }
        }
        return "{" + result + "}";
    }

    /// Formats a tool result summary (byte count + ok/error indicator).
    auto formatResultSummary(std::string_view content, bool isError) -> std::string
    {
        if (isError)
            return std::format("error ({} bytes)", content.size());
        return std::format("{} bytes", content.size());
    }

    /// Renders a trace event to a tui::TerminalOutput.
    struct RenderVisitor
    {
        tui::TerminalOutput& out;

        void operator()(TraceUserMessageEvent const& e) const
        {
            out.writeText("[trace \xe2\x86\x92] ", prefixStyle);
            auto const preview = truncateContent(e.content, 80);
            out.writeText(std::format("User ({}): \"{}\"\n", e.mode, preview), dimStyle);
        }

        void operator()(TraceLlmRequestEvent const& e) const
        {
            out.writeText("[trace \xe2\x86\x92] ", prefixStyle);
            out.writeText(std::format("LLM request #{}: {} msgs, ~{} tokens\n",
                                      e.iteration,
                                      e.messageCount,
                                      formatCount(e.tokenEstimate)),
                          dimStyle);
        }

        void operator()(TraceLlmResponseEvent const& e) const
        {
            out.writeText("[trace \xe2\x86\x90] ", prefixStyle);
            auto line = std::format("LLM response #{}: ", e.iteration);
            if (e.hasToolCalls)
                line += std::format("{} tool call{}", e.toolCount, e.toolCount == 1 ? "" : "s");
            else
                line += std::format("{} chars", e.textLength);
            line += std::format(", {}", formatDuration(e.duration));
            if (e.usage.has_value())
                line += std::format(", in:{} out:{}",
                                    formatCount(static_cast<size_t>(e.usage->inputTokens)),
                                    formatCount(static_cast<size_t>(e.usage->outputTokens)));
            line += "\n";
            out.writeText(line, dimStyle);
        }

        void operator()(TraceToolCallEvent const& e) const
        {
            out.writeText("[trace \xe2\x86\x94] ", prefixStyle);
            auto const argSummary = summarizeArguments(e.arguments);
            auto const resultSummary = formatResultSummary(e.resultContent, e.resultIsError);
            out.writeText(std::format("{} {} \xe2\x86\x92 {} ({})\n",
                                      e.name,
                                      argSummary,
                                      resultSummary,
                                      formatDuration(e.duration)),
                          e.resultIsError ? errorStyle : dimStyle);
        }

        void operator()(TraceCompactionEvent const& e) const
        {
            out.writeText("[trace \xe2\x88\xbc] ", prefixStyle);
            out.writeText(std::format("Compaction: {}\xe2\x86\x92{} msgs, {}\xe2\x86\x92{} tokens\n",
                                      e.beforeMessages,
                                      e.afterMessages,
                                      formatCount(e.beforeTokens),
                                      formatCount(e.afterTokens)),
                          dimStyle);
        }

        void operator()(TraceErrorEvent const& e) const
        {
            out.writeText("[trace !] ", prefixStyle);
            out.writeText(std::format("Error ({}): {}\n", e.code, e.message), errorStyle);
        }
    };

    /// Formats a trace event as a plain ANSI string for stderr output.
    auto formatForStderr(TraceEvent const& event) -> std::string
    {
        // dim = ESC[2m, reset = ESC[0m, bold red = ESC[1;31m
        constexpr auto dim = "\033[2m";
        constexpr auto red = "\033[1;31m";
        constexpr auto reset = "\033[0m";

        return std::visit(
            [&](auto const& e) -> std::string {
                using T = std::decay_t<decltype(e)>;
                if constexpr (std::is_same_v<T, TraceUserMessageEvent>)
                {
                    auto const preview = truncateContent(e.content, 80);
                    return std::format(
                        "{}[trace \xe2\x86\x92] User ({}): \"{}\"{}\n", dim, e.mode, preview, reset);
                }
                else if constexpr (std::is_same_v<T, TraceLlmRequestEvent>)
                {
                    return std::format("{}[trace \xe2\x86\x92] LLM request #{}: {} msgs, ~{} tokens{}\n",
                                       dim,
                                       e.iteration,
                                       e.messageCount,
                                       formatCount(e.tokenEstimate),
                                       reset);
                }
                else if constexpr (std::is_same_v<T, TraceLlmResponseEvent>)
                {
                    auto line = std::format("{}[trace \xe2\x86\x90] LLM response #{}: ", dim, e.iteration);
                    if (e.hasToolCalls)
                        line += std::format("{} tool call{}", e.toolCount, e.toolCount == 1 ? "" : "s");
                    else
                        line += std::format("{} chars", e.textLength);
                    line += std::format(", {}", formatDuration(e.duration));
                    if (e.usage.has_value())
                        line += std::format(", in:{} out:{}",
                                            formatCount(static_cast<size_t>(e.usage->inputTokens)),
                                            formatCount(static_cast<size_t>(e.usage->outputTokens)));
                    line += std::format("{}\n", reset);
                    return line;
                }
                else if constexpr (std::is_same_v<T, TraceToolCallEvent>)
                {
                    auto const argSummary = summarizeArguments(e.arguments);
                    auto const resultSummary = formatResultSummary(e.resultContent, e.resultIsError);
                    auto const style = e.resultIsError ? red : dim;
                    return std::format("{}[trace \xe2\x86\x94] {} {} \xe2\x86\x92 {} ({}){}\n",
                                       style,
                                       e.name,
                                       argSummary,
                                       resultSummary,
                                       formatDuration(e.duration),
                                       reset);
                }
                else if constexpr (std::is_same_v<T, TraceCompactionEvent>)
                {
                    return std::format("{}[trace \xe2\x88\xbc] Compaction: {}\xe2\x86\x92{} msgs, "
                                       "{}\xe2\x86\x92{} tokens{}\n",
                                       dim,
                                       e.beforeMessages,
                                       e.afterMessages,
                                       formatCount(e.beforeTokens),
                                       formatCount(e.afterTokens),
                                       reset);
                }
                else if constexpr (std::is_same_v<T, TraceErrorEvent>)
                {
                    return std::format("{}[trace !] Error ({}): {}{}\n", red, e.code, e.message, reset);
                }
                else
                {
                    return {};
                }
            },
            event);
    }
} // namespace

void renderTraceEvent(tui::TerminalOutput& out, TraceEvent const& event)
{
    std::visit(RenderVisitor { out }, event);
    out.flush();
}

void renderTraceEventToStderr(TraceEvent const& event)
{
    auto const text = formatForStderr(event);
    if (!text.empty())
        std::fputs(text.c_str(), stderr);
}

} // namespace endo::agent
