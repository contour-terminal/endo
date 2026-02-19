// SPDX-License-Identifier: Apache-2.0
#include <cstdlib>
#include <fstream>
#include <print>
#include <string>

#include <agent/TraceReplay.hpp>
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
    }

    if (toolCallCount == 0)
        std::println("No tool calls found in trace file.");
    else
        std::println("\nTotal: {} tool call(s)", toolCallCount);

    return EXIT_SUCCESS;
}

} // namespace endo::agent
