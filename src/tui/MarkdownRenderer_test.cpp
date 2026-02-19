// SPDX-License-Identifier: Apache-2.0
#include <tui/MarkdownRenderer.hpp>
#include <tui/TerminalOutput.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

using namespace tui;

namespace
{
/// @brief A TerminalOutput subclass that captures written text for testing.
class TextCapturingOutput: public TerminalOutput
{
  public:
    std::string captured;

    void writeText(std::string_view text, [[maybe_unused]] Style const& style) override
    {
        captured.append(text);
    }

    void writeRaw(std::string_view text) override { captured.append(text); }

    void flush() override {}
};
} // namespace

// ============================================================================
// Table rendering tests
// ============================================================================

TEST_CASE("MarkdownRenderer.table.basic")
{
    TextCapturingOutput output;
    MarkdownRenderer renderer(output);

    renderer.render("| Name | Age |\n|------|-----|\n| Alice | 30 |\n");

    // Should contain box-drawing characters
    CHECK(output.captured.find("\xe2\x95\xad") != std::string::npos); // ╭ (top-left rounded)
    CHECK(output.captured.find("\xe2\x95\xae") != std::string::npos); // ╮ (top-right rounded)
    CHECK(output.captured.find("\xe2\x95\xb0") != std::string::npos); // ╰ (bottom-left rounded)
    CHECK(output.captured.find("\xe2\x95\xaf") != std::string::npos); // ╯ (bottom-right rounded)

    // Should contain the cell text
    CHECK(output.captured.find("Name") != std::string::npos);
    CHECK(output.captured.find("Age") != std::string::npos);
    CHECK(output.captured.find("Alice") != std::string::npos);
    CHECK(output.captured.find("30") != std::string::npos);
}

TEST_CASE("MarkdownRenderer.table.no_separator_renders_as_paragraph")
{
    TextCapturingOutput output;
    MarkdownRenderer renderer(output);

    renderer.render("| Not | A | Table |\n| Just | Pipe | Text |\n");

    // Should NOT contain rounded box-drawing corners since no separator
    CHECK(output.captured.find("\xe2\x95\xad") == std::string::npos); // No ╭
}

TEST_CASE("MarkdownRenderer.table.multiple_rows")
{
    TextCapturingOutput output;
    MarkdownRenderer renderer(output);

    renderer.render("| A | B |\n|---|---|\n| 1 | 2 |\n| 3 | 4 |\n");

    CHECK(output.captured.find("1") != std::string::npos);
    CHECK(output.captured.find("2") != std::string::npos);
    CHECK(output.captured.find("3") != std::string::npos);
    CHECK(output.captured.find("4") != std::string::npos);
}

TEST_CASE("MarkdownRenderer.table.alignment")
{
    TextCapturingOutput output;
    MarkdownRenderer renderer(output);

    renderer.render("| Left | Right | Center |\n|:-----|------:|:------:|\n| a | b | c |\n");

    // Just verify it renders without crashing and contains the text
    CHECK(output.captured.find("Left") != std::string::npos);
    CHECK(output.captured.find("Right") != std::string::npos);
    CHECK(output.captured.find("Center") != std::string::npos);
}

TEST_CASE("MarkdownRenderer.table.streaming")
{
    TextCapturingOutput output;
    MarkdownRenderer renderer(output);

    renderer.beginStream();
    renderer.feedToken("| A | B |\n");
    renderer.feedToken("|---|---|\n");
    renderer.feedToken("| 1 | 2 |\n");
    // End stream should flush the table
    renderer.endStream();

    CHECK(output.captured.find("\xe2\x95\xad") != std::string::npos); // ╭
    CHECK(output.captured.find("1") != std::string::npos);
    CHECK(output.captured.find("2") != std::string::npos);
}

TEST_CASE("MarkdownRenderer.table.followed_by_text")
{
    TextCapturingOutput output;
    MarkdownRenderer renderer(output);

    renderer.render("| H |\n|---|\n| D |\n\nParagraph after table.\n");

    CHECK(output.captured.find("H") != std::string::npos);
    CHECK(output.captured.find("D") != std::string::npos);
    CHECK(output.captured.find("Paragraph after table.") != std::string::npos);
}

TEST_CASE("MarkdownRenderer.table.inline_markdown_in_cells")
{
    TextCapturingOutput output;
    MarkdownRenderer renderer(output);

    renderer.render("| Header |\n|--------|\n| **bold** |\n");

    // The bold markers should be stripped, leaving just the text
    CHECK(output.captured.find("bold") != std::string::npos);
    // The ** markers should NOT appear literally
    CHECK(output.captured.find("**") == std::string::npos);
}
