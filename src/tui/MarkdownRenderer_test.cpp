// SPDX-License-Identifier: Apache-2.0
#include <tui/MarkdownRenderer.hpp>
#include <tui/TerminalOutput.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>
#include <vector>

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

/// @brief A styled text span captured during rendering.
struct StyledSpan
{
    std::string text;
    Style style;
    bool isRaw; ///< true if written via writeRaw (no style).
};

/// @brief A TerminalOutput subclass that captures style information alongside text.
class StyledCapturingOutput: public TerminalOutput
{
  public:
    std::vector<StyledSpan> spans;

    void writeText(std::string_view text, Style const& style) override
    {
        spans.push_back({ .text = std::string(text), .style = style, .isRaw = false });
    }

    void writeRaw(std::string_view text) override
    {
        spans.push_back({ .text = std::string(text), .style = {}, .isRaw = true });
    }

    void flush() override {}

    /// @brief Finds the first span containing the given substring.
    /// @return Pointer to the span, or nullptr if not found.
    [[nodiscard]] auto findSpan(std::string_view substr) const -> StyledSpan const*
    {
        for (auto const& span: spans)
            if (span.text.find(substr) != std::string::npos)
                return &span;
        return nullptr;
    }
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

    CHECK(output.captured.find('1') != std::string::npos);
    CHECK(output.captured.find('2') != std::string::npos);
    CHECK(output.captured.find('3') != std::string::npos);
    CHECK(output.captured.find('4') != std::string::npos);
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
    CHECK(output.captured.find('1') != std::string::npos);
    CHECK(output.captured.find('2') != std::string::npos);
}

TEST_CASE("MarkdownRenderer.table.followed_by_text")
{
    TextCapturingOutput output;
    MarkdownRenderer renderer(output);

    renderer.render("| H |\n|---|\n| D |\n\nParagraph after table.\n");

    CHECK(output.captured.find('H') != std::string::npos);
    CHECK(output.captured.find('D') != std::string::npos);
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

TEST_CASE("MarkdownRenderer.table.maxWidth_wraps_cells")
{
    TextCapturingOutput output;
    MarkdownRenderer renderer(output);
    renderer.setMaxWidth(40);

    renderer.render(
        "| Tool | Description |\n|:-----|:------------|\n| search | Search files in the entire project "
        "directory tree |\n");

    // Should contain box-drawing characters (table rendered)
    CHECK(output.captured.find("\xe2\x95\xad") != std::string::npos); // ╭
    // Should contain the text (possibly wrapped)
    CHECK(output.captured.find("search") != std::string::npos);
    CHECK(output.captured.find("Search") != std::string::npos);
    // Description should be wrapped, so "directory" appears on a separate line
    CHECK(output.captured.find("directory") != std::string::npos);
}

TEST_CASE("MarkdownRenderer.table.maxWidth_zero_means_unconstrained")
{
    TextCapturingOutput output;
    MarkdownRenderer renderer(output);
    renderer.setMaxWidth(0); // explicitly unconstrained (default)

    renderer.render("| A | B |\n|---|---|\n| hello | world |\n");

    // Should render normally
    CHECK(output.captured.find("hello") != std::string::npos);
    CHECK(output.captured.find("world") != std::string::npos);
}

TEST_CASE("MarkdownRenderer.table.bold_cells_aligned_borders")
{
    TextCapturingOutput output;
    MarkdownRenderer renderer(output);

    // Table where one cell has bold markdown — borders should still align.
    renderer.render("| Feature | Status |\n|---------|--------|\n| **Functional Core** | Done |\n| Plain "
                    "text | Pending |\n");

    // Split captured output into lines and find rows with vertical borders.
    auto const& text = output.captured;
    auto lines = std::vector<std::string> {};
    auto pos = std::size_t { 0 };
    while (pos < text.size())
    {
        auto const nl = text.find('\n', pos);
        if (nl == std::string::npos)
        {
            lines.emplace_back(text.substr(pos));
            break;
        }
        lines.emplace_back(text.substr(pos, nl - pos));
        pos = nl + 1;
    }

    // Collect lengths of all horizontal border lines (they contain ─).
    // All content rows should have the same rendered length as the borders.
    auto borderLengths = std::vector<std::size_t> {};
    for (auto const& line: lines)
    {
        // Horizontal border lines contain ─ (UTF-8: \xe2\x94\x80)
        if (line.find("\xe2\x94\x80") != std::string::npos)
            borderLengths.push_back(line.size());
    }

    // There should be exactly 3 horizontal borders (top, header-separator, bottom).
    REQUIRE(borderLengths.size() == 3);
    // All three must have the same byte length.
    CHECK(borderLengths[0] == borderLengths[1]);
    CHECK(borderLengths[1] == borderLengths[2]);

    // Check that content rows also have the same byte length as borders.
    // Content rows contain │ (UTF-8: \xe2\x94\x82).
    auto contentLengths = std::vector<std::size_t> {};
    for (auto const& line: lines)
    {
        if (line.find("\xe2\x94\x82") != std::string::npos)
            contentLengths.push_back(line.size());
    }

    // All content rows must have the same byte length.
    REQUIRE(contentLengths.size() >= 2);
    for (auto len: contentLengths)
        CHECK(len == contentLengths[0]);
}

// ============================================================================
// Inline code rendering tests
// ============================================================================

TEST_CASE("MarkdownRenderer.inline_code.single_backtick_has_background")
{
    StyledCapturingOutput output;
    MarkdownRenderer renderer(output);

    renderer.render("Use `code` here\n");

    auto const* span = output.findSpan("code");
    REQUIRE(span != nullptr);
    CHECK_FALSE(span->isRaw);
    // Should have a background color set (0x323440)
    CHECK(std::holds_alternative<RgbColor>(span->style.bg));
    auto const bg = std::get<RgbColor>(span->style.bg);
    CHECK(bg.r == 0x32);
    CHECK(bg.g == 0x34);
    CHECK(bg.b == 0x40);
}

TEST_CASE("MarkdownRenderer.inline_code.double_backtick")
{
    TextCapturingOutput output;
    MarkdownRenderer renderer(output);

    renderer.render("Use ``code with ` backtick`` here\n");

    CHECK(output.captured.find("code with ` backtick") != std::string::npos);
}

TEST_CASE("MarkdownRenderer.inline_code.double_backtick_space_stripping")
{
    TextCapturingOutput output;
    MarkdownRenderer renderer(output);

    renderer.render("Use `` `code` `` here\n");

    // CommonMark space stripping: leading+trailing spaces stripped, leaving "`code`"
    CHECK(output.captured.find("`code`") != std::string::npos);
}

TEST_CASE("MarkdownRenderer.inline_code.unmatched_backtick_literal")
{
    TextCapturingOutput output;
    MarkdownRenderer renderer(output);

    renderer.render("This has a ` stray backtick\n");

    // Unmatched backtick should appear literally
    CHECK(output.captured.find('`') != std::string::npos);
    CHECK(output.captured.find("stray backtick") != std::string::npos);
}

TEST_CASE("MarkdownRenderer.table.constrained_width_long_tool_names_aligned")
{
    TextCapturingOutput output;
    MarkdownRenderer renderer(output);
    renderer.setMaxWidth(60);

    // Simulate the /tools table: narrow tool names, wide descriptions.
    renderer.render("| Tool | Description |\n|:-----|:------------|\n"
                    "| list_directory | List files and directories in a specified path |\n"
                    "| shell_execute | Execute a shell command and return the output |\n"
                    "| web_fetch | Fetch content from a URL |\n");

    // Split into lines.
    auto const& text = output.captured;
    auto lines = std::vector<std::string> {};
    auto pos = std::size_t { 0 };
    while (pos < text.size())
    {
        auto const nl = text.find('\n', pos);
        if (nl == std::string::npos)
        {
            if (pos < text.size())
                lines.emplace_back(text.substr(pos));
            break;
        }
        lines.emplace_back(text.substr(pos, nl - pos));
        pos = nl + 1;
    }

    // Border lines (containing ─) should all have the same byte length.
    auto borderLengths = std::vector<std::size_t> {};
    for (auto const& line: lines)
    {
        if (line.find("\xe2\x94\x80") != std::string::npos)
            borderLengths.push_back(line.size());
    }
    REQUIRE(borderLengths.size() == 3); // top, header-separator, bottom
    CHECK(borderLengths[0] == borderLengths[1]);
    CHECK(borderLengths[1] == borderLengths[2]);

    // Content lines (containing │) should all have the same byte length.
    auto contentLengths = std::vector<std::size_t> {};
    for (auto const& line: lines)
    {
        if (line.find("\xe2\x94\x82") != std::string::npos)
            contentLengths.push_back(line.size());
    }
    REQUIRE(contentLengths.size() >= 4); // header + at least 3 data rows (possibly more from wrapping)
    for (auto len: contentLengths)
        CHECK(len == contentLengths[0]);

    // Tool names should appear intact (not truncated).
    CHECK(output.captured.find("list_directory") != std::string::npos);
    CHECK(output.captured.find("shell_execute") != std::string::npos);
    CHECK(output.captured.find("web_fetch") != std::string::npos);
}
