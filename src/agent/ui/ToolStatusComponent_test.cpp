// SPDX-License-Identifier: Apache-2.0
#include <tui/Buffer.hpp>
#include <tui/Canvas.hpp>
#include <tui/Theme.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>

#include <agent/ui/ToolStatusComponent.hpp>

using namespace endo::agent;
using namespace std::chrono_literals;

namespace
{

auto makeToolCall(std::string name, nlohmann::json args = {}) -> ToolCall
{
    return ToolCall {
        .id = "test-id",
        .name = std::move(name),
        .arguments = std::move(args),
    };
}

auto makeToolResult(std::string name,
                    std::string content = "ok",
                    bool isError = false,
                    std::chrono::milliseconds duration = 1200ms) -> ToolResultMessage
{
    return ToolResultMessage {
        .name = std::move(name),
        .content = std::move(content),
        .isError = isError,
        .duration = duration,
    };
}

} // namespace

TEST_CASE("ToolStatusComponent.toolStarted_adds_entry", "[agent][ui]")
{
    auto comp = ToolStatusComponent {};
    CHECK_FALSE(comp.hasEntries());
    CHECK(comp.preferredSize().height == 0);

    comp.toolStarted(makeToolCall("read_file", { { "path", "src/main.cpp" } }));
    CHECK(comp.hasEntries());
    CHECK(comp.preferredSize().height == 1);
}

TEST_CASE("ToolStatusComponent.toolCompleted_marks_entry", "[agent][ui]")
{
    auto comp = ToolStatusComponent {};
    comp.toolStarted(makeToolCall("read_file"));
    comp.toolCompleted(makeToolResult("read_file", "file contents here", false, 800ms));

    CHECK(comp.hasEntries());
    CHECK(comp.preferredSize().height == 1);
}

TEST_CASE("ToolStatusComponent.clear_removes_all_entries", "[agent][ui]")
{
    auto comp = ToolStatusComponent {};
    comp.toolStarted(makeToolCall("read_file"));
    comp.toolCompleted(makeToolResult("read_file"));
    comp.toolStarted(makeToolCall("glob"));

    CHECK(comp.hasEntries());
    comp.clear();
    CHECK_FALSE(comp.hasEntries());
    CHECK(comp.preferredSize().height == 0);
}

TEST_CASE("ToolStatusComponent.preferredSize_counts_correctly", "[agent][ui]")
{
    auto comp = ToolStatusComponent {};

    // Empty
    CHECK(comp.preferredSize().height == 0);

    // One active
    comp.toolStarted(makeToolCall("read_file"));
    CHECK(comp.preferredSize().height == 1);

    // One completed + one active
    comp.toolCompleted(makeToolResult("read_file"));
    comp.toolStarted(makeToolCall("glob"));
    CHECK(comp.preferredSize().height == 2);

    // Two completed + one active
    comp.toolCompleted(makeToolResult("glob"));
    comp.toolStarted(makeToolCall("grep"));
    CHECK(comp.preferredSize().height == 3);
}

TEST_CASE("ToolStatusComponent.max_visible_completed_capping", "[agent][ui]")
{
    auto comp = ToolStatusComponent {};

    // Add 7 completed tools + 1 active
    for (auto i = 0; i < 7; ++i)
    {
        auto const name = "tool_" + std::to_string(i);
        comp.toolStarted(makeToolCall(name));
        comp.toolCompleted(makeToolResult(name));
    }
    comp.toolStarted(makeToolCall("active_tool"));

    // Should cap at MaxVisibleCompleted(5) completed + 1 active = 6
    CHECK(comp.preferredSize().height == 6);
}

TEST_CASE("ToolStatusComponent.error_tool_result", "[agent][ui]")
{
    auto comp = ToolStatusComponent {};
    comp.toolStarted(makeToolCall("read_file", { { "path", "/nonexistent" } }));
    comp.toolCompleted(makeToolResult("read_file", "File not found", true, 50ms));

    CHECK(comp.hasEntries());
    CHECK(comp.preferredSize().height == 1);
}

TEST_CASE("ToolStatusComponent.render_does_not_crash", "[agent][ui]")
{
    auto comp = ToolStatusComponent {};
    auto const theme = tui::darkTheme();

    // Render empty
    {
        auto buffer = tui::Buffer(1, 80);
        auto canvas = tui::Canvas(buffer, tui::Rect { .x = 0, .y = 0, .width = 80, .height = 1 }, theme);
        comp.render(canvas); // Should not crash
    }

    // Render with active entry
    comp.toolStarted(makeToolCall("shell_execute", { { "command", "cmake --build" } }));
    {
        auto buffer = tui::Buffer(1, 80);
        auto canvas = tui::Canvas(buffer, tui::Rect { .x = 0, .y = 0, .width = 80, .height = 1 }, theme);
        comp.render(canvas); // Should not crash
    }

    // Render with completed entry
    comp.toolCompleted(makeToolResult("shell_execute", "Build complete", false, 3200ms));
    {
        auto buffer = tui::Buffer(1, 80);
        auto canvas = tui::Canvas(buffer, tui::Rect { .x = 0, .y = 0, .width = 80, .height = 1 }, theme);
        comp.render(canvas); // Should not crash
    }

    // Render with error entry
    comp.toolStarted(makeToolCall("read_file", { { "path", "/no" } }));
    comp.toolCompleted(makeToolResult("read_file", "Not found", true, 100ms));
    {
        auto buffer = tui::Buffer(2, 80);
        auto canvas = tui::Canvas(buffer, tui::Rect { .x = 0, .y = 0, .width = 80, .height = 2 }, theme);
        comp.render(canvas); // Should not crash
    }
}

TEST_CASE("ToolStatusComponent.formatElapsed", "[agent][ui]")
{
    CHECK(ToolStatusComponent::formatElapsed(0ms) == "0.0s");
    CHECK(ToolStatusComponent::formatElapsed(300ms) == "0.3s");
    CHECK(ToolStatusComponent::formatElapsed(1200ms) == "1.2s");
    CHECK(ToolStatusComponent::formatElapsed(3200ms) == "3.2s");
    CHECK(ToolStatusComponent::formatElapsed(9999ms) == "9.9s");
    CHECK(ToolStatusComponent::formatElapsed(60000ms) == "1m 0s");
    CHECK(ToolStatusComponent::formatElapsed(72000ms) == "1m 12s");
    CHECK(ToolStatusComponent::formatElapsed(125000ms) == "2m 5s");
}

TEST_CASE("ToolStatusComponent.formatSize", "[agent][ui]")
{
    CHECK(ToolStatusComponent::formatSize(0) == "0 B");
    CHECK(ToolStatusComponent::formatSize(42) == "42 B");
    CHECK(ToolStatusComponent::formatSize(1023) == "1023 B");
    CHECK(ToolStatusComponent::formatSize(1024) == "1.0 KB");
    CHECK(ToolStatusComponent::formatSize(4915) == "4.8 KB");
    CHECK(ToolStatusComponent::formatSize(1048576) == "1.0 MB");
    CHECK(ToolStatusComponent::formatSize(1048576 + 104858) == "1.1 MB"); // ~1.1 MB
    CHECK(ToolStatusComponent::formatSize(2202010) == "2.1 MB");
}

TEST_CASE("ToolStatusComponent.formatArgsSummary", "[agent][ui]")
{
    SECTION("shell_execute shows command with dollar prompt")
    {
        auto const result =
            ToolStatusComponent::formatArgsSummary("shell_execute", { { "command", "cmake --build" } });
        CHECK(result == "$ cmake --build");
    }

    SECTION("endo_execute shows source with dollar prompt")
    {
        auto const result =
            ToolStatusComponent::formatArgsSummary("endo_execute", { { "source", "echo hello" } });
        CHECK(result == "$ echo hello");
    }

    SECTION("shell_execute truncates multi-line")
    {
        auto const result =
            ToolStatusComponent::formatArgsSummary("shell_execute", { { "command", "line1\nline2\nline3" } });
        CHECK(result == "$ line1...");
    }

    SECTION("read_file shows path")
    {
        auto const result =
            ToolStatusComponent::formatArgsSummary("read_file", { { "path", "src/main.cpp" } });
        CHECK(result == "src/main.cpp");
    }

    SECTION("glob shows pattern")
    {
        auto const result = ToolStatusComponent::formatArgsSummary("glob", { { "pattern", "**/*.cpp" } });
        CHECK(result == "**/*.cpp");
    }

    SECTION("grep shows pattern and path")
    {
        auto const result =
            ToolStatusComponent::formatArgsSummary("grep", { { "pattern", "TODO" }, { "path", "src/" } });
        CHECK(result == "TODO src/");
    }

    SECTION("edit_file shows path")
    {
        auto const result = ToolStatusComponent::formatArgsSummary(
            "edit_file", { { "path", "src/foo.cpp" }, { "old_string", "hello" }, { "new_string", "world" } });
        CHECK(result == "src/foo.cpp");
    }

    SECTION("write_file shows path")
    {
        auto const result = ToolStatusComponent::formatArgsSummary(
            "write_file", { { "path", "test.txt" }, { "content", "lots of content here" } });
        CHECK(result == "test.txt");
    }

    SECTION("unknown tool shows compact JSON with truncated large fields")
    {
        auto const result = ToolStatusComponent::formatArgsSummary(
            "some_tool", { { "content", std::string(500, 'x') }, { "key", "value" } });
        CHECK(result.find("<500 chars>") != std::string::npos);
        CHECK(result.find("\"key\":\"value\"") != std::string::npos);
    }

    SECTION("empty arguments returns empty string")
    {
        CHECK(ToolStatusComponent::formatArgsSummary("read_file", nlohmann::json {}).empty());
        CHECK(ToolStatusComponent::formatArgsSummary("read_file", nlohmann::json(nullptr)).empty());
    }
}

TEST_CASE("ToolStatusComponent.spinnerTimeoutMs", "[agent][ui]")
{
    auto comp = ToolStatusComponent {};

    // No entries → no spinner
    CHECK(comp.spinnerTimeoutMs() == -1);

    // Active entry → spinner timeout
    comp.toolStarted(makeToolCall("read_file"));
    CHECK(comp.spinnerTimeoutMs() > 0);

    // Completed entry → no spinner
    comp.toolCompleted(makeToolResult("read_file"));
    CHECK(comp.spinnerTimeoutMs() == -1);
}

TEST_CASE("ToolStatusComponent.tickSpinner_only_when_active", "[agent][ui]")
{
    auto comp = ToolStatusComponent {};

    // No active entry → tick returns false
    CHECK_FALSE(comp.tickSpinner());

    // Active entry but too soon → false (spinner has interval)
    comp.toolStarted(makeToolCall("read_file"));
    // First tick may or may not change depending on timing, just verify no crash
    (void) comp.tickSpinner();

    // After completion → false
    comp.toolCompleted(makeToolResult("read_file"));
    CHECK_FALSE(comp.tickSpinner());
}

TEST_CASE("ToolStatusComponent.multiple_tools_sequential", "[agent][ui]")
{
    auto comp = ToolStatusComponent {};

    // Tool 1: start and complete
    comp.toolStarted(makeToolCall("read_file"));
    comp.toolCompleted(makeToolResult("read_file", "content", false, 500ms));

    // Tool 2: start and complete
    comp.toolStarted(makeToolCall("glob"));
    comp.toolCompleted(makeToolResult("glob", "file1.cpp\nfile2.cpp", false, 200ms));

    // Tool 3: active
    comp.toolStarted(makeToolCall("grep"));

    CHECK(comp.preferredSize().height == 3); // 2 completed + 1 active
}
