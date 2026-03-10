// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <format>
#include <vector>

#include <agent/tracing/AgentTracer.hpp>
#include <nlohmann/json.hpp>
#include <platform/UserPaths.hpp>

namespace endo::agent
{

namespace
{
    /// Returns the current UTC time as an ISO 8601 timestamp string.
    auto utcTimestamp() -> std::string
    {
        auto const now = std::chrono::system_clock::now();
        return std::format("{:%FT%TZ}", std::chrono::floor<std::chrono::milliseconds>(now));
    }
} // namespace

AgentTracer::AgentTracer(std::filesystem::path path, std::ofstream stream):
    _path(std::move(path)), _stream(std::move(stream))
{
}

auto AgentTracer::create(std::filesystem::path path) -> std::expected<AgentTracer, std::string>
{
    auto const parentDir = path.parent_path();
    if (!parentDir.empty())
    {
        auto ec = std::error_code {};
        std::filesystem::create_directories(parentDir, ec);
        if (ec)
            return std::unexpected(
                std::format("Failed to create directory '{}': {}", parentDir.string(), ec.message()));
    }

    auto stream = std::ofstream(path, std::ios::app);
    if (!stream.is_open())
        return std::unexpected(std::format("Failed to open trace file '{}'", path.string()));

    return AgentTracer(std::move(path), std::move(stream));
}

void AgentTracer::writeLine(nlohmann::json const& doc)
{
    _stream << doc.dump() << '\n';
    _stream.flush();
}

void AgentTracer::writeSessionHeader(std::string_view providerName, std::string_view modelName)
{
    writeLine(nlohmann::json {
        { "type", "session" },
        { "timestamp", utcTimestamp() },
        { "provider", providerName },
        { "model", modelName },
    });
}

void AgentTracer::writeToolCall(ToolTraceEntry const& entry)
{
    writeLine(nlohmann::json {
        { "type", "tool_call" },
        { "timestamp", entry.timestamp },
        { "call_id", entry.callId },
        { "tool_name", entry.toolName },
        { "arguments", entry.arguments },
        { "result",
          nlohmann::json { { "content", entry.resultContent }, { "is_error", entry.resultIsError } } },
        { "duration_ms", entry.duration.count() },
    });
}

void AgentTracer::writeUserMessage(std::string_view mode, std::string_view content)
{
    writeLine(nlohmann::json {
        { "type", "user_message" },
        { "timestamp", utcTimestamp() },
        { "mode", mode },
        { "content", content },
    });
}

void AgentTracer::writeLlmRequest(size_t iteration, size_t messageCount, size_t tokenEstimate)
{
    writeLine(nlohmann::json {
        { "type", "llm_request" },
        { "timestamp", utcTimestamp() },
        { "iteration", iteration },
        { "message_count", messageCount },
        { "token_estimate", tokenEstimate },
    });
}

void AgentTracer::writeLlmResponse(size_t iteration,
                                   bool hasToolCalls,
                                   size_t toolCount,
                                   size_t textLength,
                                   std::chrono::milliseconds duration,
                                   std::string_view textContent,
                                   std::span<ToolCall const> toolCalls,
                                   std::optional<TokenUsage> const& usage,
                                   std::string_view url,
                                   std::string_view requestBody,
                                   std::string_view responseBody)
{
    auto doc = nlohmann::json {
        { "type", "llm_response" },  { "timestamp", utcTimestamp() },
        { "iteration", iteration },  { "has_tool_calls", hasToolCalls },
        { "tool_count", toolCount }, { "text_length", textLength },
        { "text", textContent },     { "duration_ms", duration.count() },
    };

    if (!url.empty())
        doc["url"] = url;
    if (!requestBody.empty())
        doc["request_body"] = requestBody;
    if (!responseBody.empty())
        doc["response_body"] = responseBody;

    auto toolCallsArray = nlohmann::json::array();
    for (auto const& tc: toolCalls)
    {
        toolCallsArray.push_back(nlohmann::json {
            { "id", tc.id },
            { "name", tc.name },
            { "arguments", tc.arguments },
        });
    }
    doc["tool_calls"] = std::move(toolCallsArray);

    if (usage.has_value())
    {
        doc["usage"] = nlohmann::json {
            { "input_tokens", usage->inputTokens },
            { "output_tokens", usage->outputTokens },
            { "cache_read_tokens", usage->cacheReadTokens },
            { "cache_creation_tokens", usage->cacheCreationTokens },
        };
    }

    writeLine(doc);
}

void AgentTracer::writeCompaction(size_t beforeMessages,
                                  size_t afterMessages,
                                  size_t beforeTokens,
                                  size_t afterTokens)
{
    writeLine(nlohmann::json {
        { "type", "compaction" },
        { "timestamp", utcTimestamp() },
        { "before_messages", beforeMessages },
        { "after_messages", afterMessages },
        { "before_tokens", beforeTokens },
        { "after_tokens", afterTokens },
    });
}

void AgentTracer::writeError(std::string_view code,
                             std::string_view message,
                             std::string_view url,
                             std::string_view requestBody,
                             std::string_view responseBody)
{
    auto doc = nlohmann::json {
        { "type", "error" },
        { "timestamp", utcTimestamp() },
        { "code", code },
        { "message", message },
    };
    if (!url.empty())
        doc["url"] = url;
    if (!requestBody.empty())
        doc["request_body"] = requestBody;
    if (!responseBody.empty())
        doc["response_body"] = responseBody;
    writeLine(doc);
}

void AgentTracer::close()
{
    _stream.close();
}

auto AgentTracer::path() const noexcept -> std::filesystem::path const&
{
    return _path;
}

auto resolveTraceLogDirectory() -> std::filesystem::path
{
    auto ec = std::error_code {};
    auto current = std::filesystem::current_path(ec);
    if (!ec)
    {
        // Walk upward looking for a .git directory (project root).
        while (true)
        {
            if (std::filesystem::is_directory(current / ".git", ec))
                return current / ".endo" / "trace-logs";

            auto const parent = current.parent_path();
            if (parent == current)
                break;
            current = parent;
        }
    }

    // Fallback: global state directory.
    if (auto const home = platform::homeDirectory())
        return *home / ".local" / "state" / "endo" / "trace-logs";

#if defined(_WIN32)
    if (auto const* temp = std::getenv("TEMP"); temp && *temp != '\0')
        return std::filesystem::path(temp) / "endo" / "trace-logs";
#endif
    return std::filesystem::path("/tmp") / "endo" / "trace-logs";
}

void pruneOldTraceFiles(std::filesystem::path const& dir, size_t maxFiles)
{
    auto ec = std::error_code {};
    if (!std::filesystem::is_directory(dir, ec))
        return;

    auto files = std::vector<std::filesystem::directory_entry> {};
    for (auto const& entry: std::filesystem::directory_iterator(dir, ec))
    {
        if (entry.is_regular_file(ec) && entry.path().extension() == ".jsonl")
            files.push_back(entry);
    }

    if (files.size() <= maxFiles)
        return;

    // Sort by last-write-time, oldest first.
    std::ranges::sort(files, [](auto const& a, auto const& b) {
        auto ec = std::error_code {};
        return a.last_write_time(ec) < b.last_write_time(ec);
    });

    auto const toRemove = files.size() - maxFiles;
    for (size_t i = 0; i < toRemove; ++i)
        std::filesystem::remove(files[i].path(), ec);
}

} // namespace endo::agent
