// SPDX-License-Identifier: Apache-2.0
#include <chrono>
#include <format>

#include <agent/ToolTracer.hpp>
#include <nlohmann/json.hpp>

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

ToolTracer::ToolTracer(std::filesystem::path path, std::ofstream stream):
    _path(std::move(path)), _stream(std::move(stream))
{
}

auto ToolTracer::create(std::filesystem::path path) -> std::expected<ToolTracer, std::string>
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

    return ToolTracer(std::move(path), std::move(stream));
}

void ToolTracer::writeSessionHeader(std::string_view providerName, std::string_view modelName)
{
    auto const doc = nlohmann::json {
        { "type", "session" },
        { "timestamp", utcTimestamp() },
        { "provider", providerName },
        { "model", modelName },
    };
    _stream << doc.dump() << '\n';
    _stream.flush();
}

void ToolTracer::writeToolCall(ToolTraceEntry const& entry)
{
    auto const doc = nlohmann::json {
        { "type", "tool_call" },
        { "timestamp", entry.timestamp },
        { "call_id", entry.callId },
        { "tool_name", entry.toolName },
        { "arguments", entry.arguments },
        { "result",
          nlohmann::json { { "content", entry.resultContent }, { "is_error", entry.resultIsError } } },
        { "duration_ms", entry.duration.count() },
    };
    _stream << doc.dump() << '\n';
    _stream.flush();
}

auto ToolTracer::path() const noexcept -> std::filesystem::path const&
{
    return _path;
}

} // namespace endo::agent
