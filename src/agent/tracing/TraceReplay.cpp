// SPDX-License-Identifier: Apache-2.0
#include <cstdlib>
#include <fstream>
#include <print>
#include <string>

#include <agent/tracing/TraceReplay.hpp>
#include <nlohmann/json.hpp>

namespace endo::agent
{

auto runTraceReplay(std::string_view traceFilePath) -> int
{
    auto ifs = std::ifstream(std::string(traceFilePath));
    if (!ifs.is_open())
    {
        std::println(stderr, "Error: Could not open trace file: {}", traceFilePath);
        return EXIT_FAILURE;
    }

    auto lineNumber = 0;
    auto toolCallCount = 0;
    auto userMessageCount = 0;
    auto llmRequestCount = 0;
    auto llmResponseCount = 0;
    auto compactionCount = 0;
    auto errorCount = 0;
    auto line = std::string {};

    while (std::getline(ifs, line))
    {
        ++lineNumber;
        if (line.empty())
            continue;

        nlohmann::json doc;
        try
        {
            doc = nlohmann::json::parse(line);
        }
        catch (nlohmann::json::parse_error const& e)
        {
            std::println(stderr, "Warning: Skipping malformed JSON on line {}: {}", lineNumber, e.what());
            continue;
        }

        auto const type = doc.value("type", "");
        if (type == "session")
        {
            auto const provider = doc.value("provider", "unknown");
            auto const model = doc.value("model", "unknown");
            auto const timestamp = doc.value("timestamp", "");
            std::println("=== Session: {} / {} ({})", provider, model, timestamp);
        }
        else if (type == "user_message")
        {
            ++userMessageCount;
            auto const mode = doc.value("mode", "unknown");
            auto content = doc.value("content", "");
            if (content.size() > 80)
                content = content.substr(0, 77) + "...";
            std::println("  [USER] ({}) {}", mode, content);
        }
        else if (type == "llm_request")
        {
            ++llmRequestCount;
            auto const iteration = doc.value("iteration", 0);
            auto const msgCount = doc.value("message_count", 0);
            auto const tokenEst = doc.value("token_estimate", 0);
            std::println("  [REQ] iteration={} messages={} tokens=~{}", iteration, msgCount, tokenEst);
        }
        else if (type == "llm_response")
        {
            ++llmResponseCount;
            auto const iteration = doc.value("iteration", 0);
            auto const toolCount = doc.value("tool_count", 0);
            auto const textLen = doc.value("text_length", 0);
            auto const durationMs = doc.value("duration_ms", 0);
            std::println("  [RES] iteration={} tools={} text={} chars ({}ms)",
                         iteration,
                         toolCount,
                         textLen,
                         durationMs);

            // Display text content preview if available
            if (doc.contains("text") && doc["text"].is_string())
            {
                auto text = doc["text"].get<std::string>();
                if (text.size() > 120)
                    text = text.substr(0, 117) + "...";
                if (!text.empty())
                    std::println("        text: \"{}\"", text);
            }

            // Display tool call names if available
            if (doc.contains("tool_calls") && doc["tool_calls"].is_array() && !doc["tool_calls"].empty())
            {
                auto names = std::string {};
                for (auto const& tc: doc["tool_calls"])
                {
                    if (!names.empty())
                        names += ", ";
                    names += tc.value("name", "?");
                }
                std::println("        tools: [{}]", names);
            }

            // Display token usage if available
            if (doc.contains("usage") && doc["usage"].is_object())
            {
                auto const& u = doc["usage"];
                std::println("        usage: in={} out={} cache_read={} cache_write={}",
                             u.value("input_tokens", 0),
                             u.value("output_tokens", 0),
                             u.value("cache_read_tokens", 0),
                             u.value("cache_creation_tokens", 0));
            }
        }
        else if (type == "tool_call")
        {
            ++toolCallCount;
            auto const toolName = doc.value("tool_name", "unknown");
            auto const durationMs = doc.value("duration_ms", 0);
            auto const isError = doc.contains("result") && doc["result"].value("is_error", false);

            // Summarize arguments (truncate for display)
            auto argsStr = std::string {};
            if (doc.contains("arguments") && !doc["arguments"].is_null())
            {
                argsStr = doc["arguments"].dump();
                if (argsStr.size() > 80)
                    argsStr = argsStr.substr(0, 77) + "...";
            }

            auto const status = isError ? "ERROR" : "OK";
            std::println(
                "  [{:3}] {} ({}) -> {} ({}ms)", toolCallCount, toolName, argsStr, status, durationMs);
        }
        else if (type == "compaction")
        {
            ++compactionCount;
            auto const beforeMsgs = doc.value("before_messages", 0);
            auto const afterMsgs = doc.value("after_messages", 0);
            auto const beforeTokens = doc.value("before_tokens", 0);
            auto const afterTokens = doc.value("after_tokens", 0);
            std::println("  [COMPACT] messages: {}->{}  tokens: ~{}->~{}",
                         beforeMsgs,
                         afterMsgs,
                         beforeTokens,
                         afterTokens);
        }
        else if (type == "error")
        {
            ++errorCount;
            auto const code = doc.value("code", "unknown");
            auto const message = doc.value("message", "");
            std::println("  [ERROR] {}: {}", code, message);
        }
    }

    auto const totalEvents =
        toolCallCount + userMessageCount + llmRequestCount + llmResponseCount + compactionCount + errorCount;
    if (totalEvents == 0)
        std::println("No events found in trace file.");
    else
    {
        std::println("\nSummary:");
        if (userMessageCount > 0)
            std::println("  User messages:  {}", userMessageCount);
        if (llmRequestCount > 0)
            std::println("  LLM requests:   {}", llmRequestCount);
        if (llmResponseCount > 0)
            std::println("  LLM responses:  {}", llmResponseCount);
        if (toolCallCount > 0)
            std::println("  Tool calls:     {}", toolCallCount);
        if (compactionCount > 0)
            std::println("  Compactions:    {}", compactionCount);
        if (errorCount > 0)
            std::println("  Errors:         {}", errorCount);
        std::println("  Total events:   {}", totalEvents);
    }

    return EXIT_SUCCESS;
}

} // namespace endo::agent
