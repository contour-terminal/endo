// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#include <agent/ToolTracer.hpp>
#include <nlohmann/json.hpp>

using namespace endo::agent;

namespace
{
/// Reads all lines from a file into a vector.
auto readLines(std::filesystem::path const& path) -> std::vector<std::string>
{
    auto lines = std::vector<std::string> {};
    auto ifs = std::ifstream(path);
    auto line = std::string {};
    while (std::getline(ifs, line))
        lines.push_back(std::move(line));
    return lines;
}
} // namespace

TEST_CASE("ToolTracer.create_creates_file", "[agent]")
{
    auto const tmpDir = std::filesystem::temp_directory_path() / "endo-test-tracer-create";
    std::filesystem::remove_all(tmpDir);
    auto const tracePath = tmpDir / "trace.jsonl";

    auto result = ToolTracer::create(tracePath);
    REQUIRE(result.has_value());
    CHECK(std::filesystem::exists(tracePath));

    std::filesystem::remove_all(tmpDir);
}

TEST_CASE("ToolTracer.session_header_format", "[agent]")
{
    auto const tmpDir = std::filesystem::temp_directory_path() / "endo-test-tracer-header";
    std::filesystem::remove_all(tmpDir);
    auto const tracePath = tmpDir / "trace.jsonl";

    auto tracer = ToolTracer::create(tracePath);
    REQUIRE(tracer.has_value());

    tracer->writeSessionHeader("claude", "claude-sonnet-4-5-20250929");

    auto const lines = readLines(tracePath);
    REQUIRE(lines.size() == 1);

    auto const doc = nlohmann::json::parse(lines[0]);
    CHECK(doc.at("type") == "session");
    CHECK(doc.at("provider") == "claude");
    CHECK(doc.at("model") == "claude-sonnet-4-5-20250929");
    CHECK(doc.contains("timestamp"));

    std::filesystem::remove_all(tmpDir);
}

TEST_CASE("ToolTracer.tool_call_entry_format", "[agent]")
{
    auto const tmpDir = std::filesystem::temp_directory_path() / "endo-test-tracer-entry";
    std::filesystem::remove_all(tmpDir);
    auto const tracePath = tmpDir / "trace.jsonl";

    auto tracer = ToolTracer::create(tracePath);
    REQUIRE(tracer.has_value());

    auto const entry = ToolTraceEntry {
        .timestamp = "2026-02-19T14:32:05.456Z",
        .callId = "call_abc",
        .toolName = "read_file",
        .arguments = nlohmann::json { { "path", "/src/main.cpp" } },
        .resultContent = "file contents here",
        .resultIsError = false,
        .duration = std::chrono::milliseconds { 42 },
    };
    tracer->writeToolCall(entry);

    auto const lines = readLines(tracePath);
    REQUIRE(lines.size() == 1);

    auto const doc = nlohmann::json::parse(lines[0]);
    CHECK(doc.at("type") == "tool_call");
    CHECK(doc.at("timestamp") == "2026-02-19T14:32:05.456Z");
    CHECK(doc.at("call_id") == "call_abc");
    CHECK(doc.at("tool_name") == "read_file");
    CHECK(doc.at("arguments").at("path") == "/src/main.cpp");
    CHECK(doc.at("result").at("content") == "file contents here");
    CHECK(doc.at("result").at("is_error") == false);
    CHECK(doc.at("duration_ms") == 42);

    std::filesystem::remove_all(tmpDir);
}

TEST_CASE("ToolTracer.multiple_entries_one_per_line", "[agent]")
{
    auto const tmpDir = std::filesystem::temp_directory_path() / "endo-test-tracer-multi";
    std::filesystem::remove_all(tmpDir);
    auto const tracePath = tmpDir / "trace.jsonl";

    auto tracer = ToolTracer::create(tracePath);
    REQUIRE(tracer.has_value());

    tracer->writeSessionHeader("openai", "gpt-4o");
    tracer->writeToolCall(ToolTraceEntry {
        .timestamp = "2026-02-19T14:32:01.000Z",
        .callId = "c1",
        .toolName = "glob",
        .arguments = nlohmann::json { { "pattern", "*.cpp" } },
        .resultContent = "src/main.cpp",
        .duration = std::chrono::milliseconds { 10 },
    });
    tracer->writeToolCall(ToolTraceEntry {
        .timestamp = "2026-02-19T14:32:02.000Z",
        .callId = "c2",
        .toolName = "read_file",
        .arguments = nlohmann::json { { "path", "src/main.cpp" } },
        .resultContent = "int main() {}",
        .duration = std::chrono::milliseconds { 20 },
    });

    auto const lines = readLines(tracePath);
    CHECK(lines.size() == 3);

    // Each line is valid JSON
    for (auto const& line: lines)
        CHECK_NOTHROW(nlohmann::json::parse(line));

    std::filesystem::remove_all(tmpDir);
}

TEST_CASE("ToolTracer.create_creates_parent_directories", "[agent]")
{
    auto const tmpDir = std::filesystem::temp_directory_path() / "endo-test-tracer-mkdir" / "nested" / "dirs";
    std::filesystem::remove_all(std::filesystem::temp_directory_path() / "endo-test-tracer-mkdir");
    auto const tracePath = tmpDir / "trace.jsonl";

    auto result = ToolTracer::create(tracePath);
    REQUIRE(result.has_value());
    CHECK(std::filesystem::exists(tracePath));

    std::filesystem::remove_all(std::filesystem::temp_directory_path() / "endo-test-tracer-mkdir");
}

TEST_CASE("ToolTracer.create_returns_error_for_invalid_path", "[agent]")
{
    // /proc is read-only on Linux, so writing there should fail
    auto result = ToolTracer::create("/proc/nonexistent/dir/trace.jsonl");
    CHECK_FALSE(result.has_value());
    CHECK_FALSE(result.error().empty());
}

TEST_CASE("ToolTracer.path_returns_configured_path", "[agent]")
{
    auto const tmpDir = std::filesystem::temp_directory_path() / "endo-test-tracer-path";
    std::filesystem::remove_all(tmpDir);
    auto const tracePath = tmpDir / "trace.jsonl";

    auto tracer = ToolTracer::create(tracePath);
    REQUIRE(tracer.has_value());
    CHECK(tracer->path() == tracePath);

    std::filesystem::remove_all(tmpDir);
}

TEST_CASE("ToolTracer.error_result_entry", "[agent]")
{
    auto const tmpDir = std::filesystem::temp_directory_path() / "endo-test-tracer-error";
    std::filesystem::remove_all(tmpDir);
    auto const tracePath = tmpDir / "trace.jsonl";

    auto tracer = ToolTracer::create(tracePath);
    REQUIRE(tracer.has_value());

    tracer->writeToolCall(ToolTraceEntry {
        .timestamp = "2026-02-19T14:32:05.000Z",
        .callId = "c-err",
        .toolName = "write_file",
        .arguments = nlohmann::json { { "path", "/readonly/file" } },
        .resultContent = "Permission denied",
        .resultIsError = true,
        .duration = std::chrono::milliseconds { 5 },
    });

    auto const lines = readLines(tracePath);
    REQUIRE(lines.size() == 1);

    auto const doc = nlohmann::json::parse(lines[0]);
    CHECK(doc.at("result").at("is_error") == true);
    CHECK(doc.at("result").at("content") == "Permission denied");

    std::filesystem::remove_all(tmpDir);
}
