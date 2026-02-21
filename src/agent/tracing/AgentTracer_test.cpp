// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#include <agent/tracing/AgentTracer.hpp>
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

TEST_CASE("AgentTracer.create_creates_file", "[agent]")
{
    auto const tmpDir = std::filesystem::temp_directory_path() / "endo-test-tracer-create";
    std::filesystem::remove_all(tmpDir);
    auto const tracePath = tmpDir / "trace.jsonl";

    auto result = AgentTracer::create(tracePath);
    REQUIRE(result.has_value());
    CHECK(std::filesystem::exists(tracePath));

    std::filesystem::remove_all(tmpDir);
}

TEST_CASE("AgentTracer.session_header_format", "[agent]")
{
    auto const tmpDir = std::filesystem::temp_directory_path() / "endo-test-tracer-header";
    std::filesystem::remove_all(tmpDir);
    auto const tracePath = tmpDir / "trace.jsonl";

    auto tracer = AgentTracer::create(tracePath);
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

TEST_CASE("AgentTracer.tool_call_entry_format", "[agent]")
{
    auto const tmpDir = std::filesystem::temp_directory_path() / "endo-test-tracer-entry";
    std::filesystem::remove_all(tmpDir);
    auto const tracePath = tmpDir / "trace.jsonl";

    auto tracer = AgentTracer::create(tracePath);
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

TEST_CASE("AgentTracer.multiple_entries_one_per_line", "[agent]")
{
    auto const tmpDir = std::filesystem::temp_directory_path() / "endo-test-tracer-multi";
    std::filesystem::remove_all(tmpDir);
    auto const tracePath = tmpDir / "trace.jsonl";

    auto tracer = AgentTracer::create(tracePath);
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

TEST_CASE("AgentTracer.create_creates_parent_directories", "[agent]")
{
    auto const tmpDir = std::filesystem::temp_directory_path() / "endo-test-tracer-mkdir" / "nested" / "dirs";
    std::filesystem::remove_all(std::filesystem::temp_directory_path() / "endo-test-tracer-mkdir");
    auto const tracePath = tmpDir / "trace.jsonl";

    auto result = AgentTracer::create(tracePath);
    REQUIRE(result.has_value());
    CHECK(std::filesystem::exists(tracePath));

    std::filesystem::remove_all(std::filesystem::temp_directory_path() / "endo-test-tracer-mkdir");
}

TEST_CASE("AgentTracer.create_returns_error_for_invalid_path", "[agent]")
{
    // /proc is read-only on Linux, so writing there should fail
    auto result = AgentTracer::create("/proc/nonexistent/dir/trace.jsonl");
    CHECK_FALSE(result.has_value());
    CHECK_FALSE(result.error().empty());
}

TEST_CASE("AgentTracer.path_returns_configured_path", "[agent]")
{
    auto const tmpDir = std::filesystem::temp_directory_path() / "endo-test-tracer-path";
    std::filesystem::remove_all(tmpDir);
    auto const tracePath = tmpDir / "trace.jsonl";

    auto tracer = AgentTracer::create(tracePath);
    REQUIRE(tracer.has_value());
    CHECK(tracer->path() == tracePath);

    std::filesystem::remove_all(tmpDir);
}

TEST_CASE("AgentTracer.error_result_entry", "[agent]")
{
    auto const tmpDir = std::filesystem::temp_directory_path() / "endo-test-tracer-error";
    std::filesystem::remove_all(tmpDir);
    auto const tracePath = tmpDir / "trace.jsonl";

    auto tracer = AgentTracer::create(tracePath);
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

// ============================================================================
// New event type tests
// ============================================================================

TEST_CASE("AgentTracer.user_message_format", "[agent]")
{
    auto const tmpDir = std::filesystem::temp_directory_path() / "endo-test-tracer-usermsg";
    std::filesystem::remove_all(tmpDir);
    auto const tracePath = tmpDir / "trace.jsonl";

    auto tracer = AgentTracer::create(tracePath);
    REQUIRE(tracer.has_value());

    tracer->writeUserMessage("chat", "explain this code");

    auto const lines = readLines(tracePath);
    REQUIRE(lines.size() == 1);

    auto const doc = nlohmann::json::parse(lines[0]);
    CHECK(doc.at("type") == "user_message");
    CHECK(doc.at("mode") == "chat");
    CHECK(doc.at("content") == "explain this code");
    CHECK(doc.contains("timestamp"));

    std::filesystem::remove_all(tmpDir);
}

TEST_CASE("AgentTracer.llm_request_format", "[agent]")
{
    auto const tmpDir = std::filesystem::temp_directory_path() / "endo-test-tracer-llmreq";
    std::filesystem::remove_all(tmpDir);
    auto const tracePath = tmpDir / "trace.jsonl";

    auto tracer = AgentTracer::create(tracePath);
    REQUIRE(tracer.has_value());

    tracer->writeLlmRequest(2, 10, 3500);

    auto const lines = readLines(tracePath);
    REQUIRE(lines.size() == 1);

    auto const doc = nlohmann::json::parse(lines[0]);
    CHECK(doc.at("type") == "llm_request");
    CHECK(doc.at("iteration") == 2);
    CHECK(doc.at("message_count") == 10);
    CHECK(doc.at("token_estimate") == 3500);
    CHECK(doc.contains("timestamp"));

    std::filesystem::remove_all(tmpDir);
}

TEST_CASE("AgentTracer.llm_response_format", "[agent]")
{
    auto const tmpDir = std::filesystem::temp_directory_path() / "endo-test-tracer-llmres";
    std::filesystem::remove_all(tmpDir);
    auto const tracePath = tmpDir / "trace.jsonl";

    auto tracer = AgentTracer::create(tracePath);
    REQUIRE(tracer.has_value());

    auto const toolCalls = std::vector<ToolCall> {
        { .id = "call_0", .name = "read_file", .arguments = nlohmann::json { { "path", "/foo" } } },
        { .id = "call_1", .name = "glob", .arguments = nlohmann::json { { "pattern", "*.cpp" } } },
    };
    auto const usage = TokenUsage {
        .inputTokens = 1234,
        .outputTokens = 567,
        .cacheReadTokens = 100,
        .cacheCreationTokens = 50,
    };

    tracer->writeLlmResponse(
        1, true, 2, 18, std::chrono::milliseconds { 2400 }, "Here is the text.", toolCalls, usage);

    auto const lines = readLines(tracePath);
    REQUIRE(lines.size() == 1);

    auto const doc = nlohmann::json::parse(lines[0]);
    CHECK(doc.at("type") == "llm_response");
    CHECK(doc.at("iteration") == 1);
    CHECK(doc.at("has_tool_calls") == true);
    CHECK(doc.at("tool_count") == 2);
    CHECK(doc.at("text_length") == 18);
    CHECK(doc.at("duration_ms") == 2400);
    CHECK(doc.contains("timestamp"));

    // New fields: text content
    CHECK(doc.at("text") == "Here is the text.");

    // New fields: tool calls array
    REQUIRE(doc.at("tool_calls").is_array());
    REQUIRE(doc.at("tool_calls").size() == 2);
    CHECK(doc.at("tool_calls")[0].at("id") == "call_0");
    CHECK(doc.at("tool_calls")[0].at("name") == "read_file");
    CHECK(doc.at("tool_calls")[0].at("arguments").at("path") == "/foo");
    CHECK(doc.at("tool_calls")[1].at("id") == "call_1");
    CHECK(doc.at("tool_calls")[1].at("name") == "glob");

    // New fields: token usage
    REQUIRE(doc.contains("usage"));
    CHECK(doc.at("usage").at("input_tokens") == 1234);
    CHECK(doc.at("usage").at("output_tokens") == 567);
    CHECK(doc.at("usage").at("cache_read_tokens") == 100);
    CHECK(doc.at("usage").at("cache_creation_tokens") == 50);

    std::filesystem::remove_all(tmpDir);
}

TEST_CASE("AgentTracer.llm_response_without_usage", "[agent]")
{
    auto const tmpDir = std::filesystem::temp_directory_path() / "endo-test-tracer-llmres-nousage";
    std::filesystem::remove_all(tmpDir);
    auto const tracePath = tmpDir / "trace.jsonl";

    auto tracer = AgentTracer::create(tracePath);
    REQUIRE(tracer.has_value());

    auto const emptyToolCalls = std::vector<ToolCall> {};

    tracer->writeLlmResponse(
        0, false, 0, 5, std::chrono::milliseconds { 100 }, "Hello", emptyToolCalls, std::nullopt);

    auto const lines = readLines(tracePath);
    REQUIRE(lines.size() == 1);

    auto const doc = nlohmann::json::parse(lines[0]);
    CHECK(doc.at("text") == "Hello");
    CHECK(doc.at("tool_calls").empty());
    CHECK_FALSE(doc.contains("usage"));

    std::filesystem::remove_all(tmpDir);
}

TEST_CASE("AgentTracer.compaction_format", "[agent]")
{
    auto const tmpDir = std::filesystem::temp_directory_path() / "endo-test-tracer-compact";
    std::filesystem::remove_all(tmpDir);
    auto const tracePath = tmpDir / "trace.jsonl";

    auto tracer = AgentTracer::create(tracePath);
    REQUIRE(tracer.has_value());

    tracer->writeCompaction(25, 8, 12000, 4000);

    auto const lines = readLines(tracePath);
    REQUIRE(lines.size() == 1);

    auto const doc = nlohmann::json::parse(lines[0]);
    CHECK(doc.at("type") == "compaction");
    CHECK(doc.at("before_messages") == 25);
    CHECK(doc.at("after_messages") == 8);
    CHECK(doc.at("before_tokens") == 12000);
    CHECK(doc.at("after_tokens") == 4000);
    CHECK(doc.contains("timestamp"));

    std::filesystem::remove_all(tmpDir);
}

TEST_CASE("AgentTracer.error_format", "[agent]")
{
    auto const tmpDir = std::filesystem::temp_directory_path() / "endo-test-tracer-errfmt";
    std::filesystem::remove_all(tmpDir);
    auto const tracePath = tmpDir / "trace.jsonl";

    auto tracer = AgentTracer::create(tracePath);
    REQUIRE(tracer.has_value());

    tracer->writeError("ProviderError", "HTTP 429 rate limit");

    auto const lines = readLines(tracePath);
    REQUIRE(lines.size() == 1);

    auto const doc = nlohmann::json::parse(lines[0]);
    CHECK(doc.at("type") == "error");
    CHECK(doc.at("code") == "ProviderError");
    CHECK(doc.at("message") == "HTTP 429 rate limit");
    CHECK(doc.contains("timestamp"));

    std::filesystem::remove_all(tmpDir);
}
