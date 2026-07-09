// SPDX-License-Identifier: Apache-2.0
#include <tui/ImageProvider.hpp>
#include <tui/MarkdownRenderer.hpp>
#include <tui/TerminalOutput.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <ranges>
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

/// @brief The kind of a recorded TerminalOutput operation.
enum class OpKind : std::uint8_t
{
    Text,
    Raw,
    Sixel,
    Hyperlink,
    BeginHyperlink,
    EndHyperlink,
    SaveCursor,
    RestoreCursor,
    MoveRight,
    Linefeed,
    DoubleWidth,
    DoubleHeightTop,
    DoubleHeightBottom,
    SingleWidth,
};

/// @brief One recorded TerminalOutput operation, in emission order.
struct Op
{
    OpKind kind;
    std::string text; ///< Text/Raw/Sixel/Hyperlink payload.
    std::string url;  ///< Hyperlink target.
    int n = 0;        ///< MoveRight distance.
};

/// @brief A TerminalOutput that records an ordered operation log.
///
/// Unlike TextCapturingOutput this preserves *placement* (cursor save/restore,
/// horizontal offsets, row advancement), which is what image and centering tests
/// need to assert.
class RecordingOutput: public TerminalOutput
{
  public:
    std::vector<Op> ops;

    explicit RecordingOutput(int cols = 80): _cols(cols) {}

    void writeText(std::string_view t, [[maybe_unused]] Style const& style) override
    {
        ops.push_back({ .kind = OpKind::Text, .text = std::string(t) });
    }

    void writeRaw(std::string_view t) override
    {
        ops.push_back({ .kind = OpKind::Raw, .text = std::string(t) });
    }

    void writeSixel(std::string_view s) override
    {
        ops.push_back({ .kind = OpKind::Sixel, .text = std::string(s) });
    }

    void writeHyperlink(std::string_view t, std::string_view u, [[maybe_unused]] Style const& s) override
    {
        ops.push_back({ .kind = OpKind::Hyperlink, .text = std::string(t), .url = std::string(u) });
    }

    void beginHyperlink(std::string_view u) override
    {
        ops.push_back({ .kind = OpKind::BeginHyperlink, .url = std::string(u) });
    }

    void endHyperlink() override { ops.push_back({ .kind = OpKind::EndHyperlink }); }

    void saveCursor() override { ops.push_back({ .kind = OpKind::SaveCursor }); }

    void restoreCursor() override { ops.push_back({ .kind = OpKind::RestoreCursor }); }

    void moveRight(int n) override { ops.push_back({ .kind = OpKind::MoveRight, .n = n }); }

    void linefeed() override { ops.push_back({ .kind = OpKind::Linefeed }); }

    void setDoubleWidth() override { ops.push_back({ .kind = OpKind::DoubleWidth }); }

    void setDoubleHeightTop() override { ops.push_back({ .kind = OpKind::DoubleHeightTop }); }

    void setDoubleHeightBottom() override { ops.push_back({ .kind = OpKind::DoubleHeightBottom }); }

    void setSingleWidth() override { ops.push_back({ .kind = OpKind::SingleWidth }); }

    void flush() override {}

    [[nodiscard]] auto columns() const noexcept -> int override { return _cols; }

    /// @brief Counts recorded operations of the given kind.
    [[nodiscard]] auto count(OpKind kind) const -> std::size_t
    {
        return static_cast<std::size_t>(std::ranges::count(ops, kind, &Op::kind));
    }

    /// @brief Index of the first operation of the given kind, or ops.size() if absent.
    [[nodiscard]] auto indexOf(OpKind kind) const -> std::size_t
    {
        auto const it = std::ranges::find(ops, kind, &Op::kind);
        return static_cast<std::size_t>(std::ranges::distance(ops.begin(), it));
    }

    /// @brief Finds the first operation of the given kind, or nullptr.
    [[nodiscard]] auto find(OpKind kind) const -> Op const*
    {
        auto const it = std::ranges::find(ops, kind, &Op::kind);
        return it != ops.end() ? &*it : nullptr;
    }

    /// @brief All Text and Raw payloads concatenated, in order.
    [[nodiscard]] auto text() const -> std::string
    {
        auto result = std::string {};
        for (auto const& op: ops)
            if (op.kind == OpKind::Text || op.kind == OpKind::Raw)
                result += op.text;
        return result;
    }

  private:
    int _cols;
};

/// @brief An ImageProvider whose capability and result are set by the test.
class FakeImageProvider: public ImageProvider
{
  public:
    bool supports = true;        ///< Value reported by supportsSixel().
    Result<PreparedImage> next = ///< Result returned by prepare().
        PreparedImage { .sixel = "SIXELDATA", .cellWidth = 20, .cellHeight = 5 };
    std::string lastSrc;            ///< Source of the most recent prepare().
    std::optional<int> lastWidthPx; ///< Width of the most recent prepare().
    int prepareCalls = 0;           ///< Number of prepare() invocations.

    [[nodiscard]] auto supportsSixel() -> bool override { return supports; }

    [[nodiscard]] auto prepare(std::string_view src, std::optional<int> widthPx)
        -> Result<PreparedImage> override
    {
        ++prepareCalls;
        lastSrc = std::string(src);
        lastWidthPx = widthPx;
        return next;
    }
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

// ============================================================================
// OSC-8 hyperlink tests
// ============================================================================

TEST_CASE("MarkdownRenderer.link.absolute_url_is_hyperlinked")
{
    RecordingOutput output;
    MarkdownRenderer renderer(output);

    renderer.render("See [Docs](https://endo-lang.org/) now\n");

    REQUIRE(output.count(OpKind::BeginHyperlink) == 1);
    CHECK(output.find(OpKind::BeginHyperlink)->url == "https://endo-lang.org/");
    CHECK(output.count(OpKind::EndHyperlink) == 1);
    CHECK(output.text().find("Docs") != std::string::npos);
}

TEST_CASE("MarkdownRenderer.link.relative_url_is_not_hyperlinked")
{
    RecordingOutput output;
    MarkdownRenderer renderer(output);

    renderer.render("[LICENSE](LICENSE) and [Install](#installation)\n");

    CHECK(output.count(OpKind::BeginHyperlink) == 0);
    CHECK(output.count(OpKind::Hyperlink) == 0);
    CHECK(output.text().find("LICENSE") != std::string::npos);
    CHECK(output.text().find("Install") != std::string::npos);
}

TEST_CASE("MarkdownRenderer.link.autolink_url")
{
    RecordingOutput output;
    MarkdownRenderer renderer(output);

    renderer.render("Visit <https://example.com> today\n");

    REQUIRE(output.count(OpKind::Hyperlink) == 1);
    auto const* const link = output.find(OpKind::Hyperlink);
    CHECK(link->url == "https://example.com");
    CHECK(link->text == "https://example.com");
}

TEST_CASE("MarkdownRenderer.link.autolink_email_becomes_mailto")
{
    RecordingOutput output;
    MarkdownRenderer renderer(output);

    renderer.render("Mail <a@b.com> please\n");

    REQUIRE(output.count(OpKind::Hyperlink) == 1);
    auto const* const link = output.find(OpKind::Hyperlink);
    CHECK(link->url == "mailto:a@b.com");
    CHECK(link->text == "a@b.com");
}

TEST_CASE("MarkdownRenderer.link.badge_image_inside_link")
{
    RecordingOutput output;
    MarkdownRenderer renderer(output);

    // The README badge idiom. The naive scan used to emit "![Linux](https://ci)".
    renderer.render("[![Linux](badge.svg)](https://ci.example.com/job)\n");

    REQUIRE(output.count(OpKind::BeginHyperlink) == 1);
    CHECK(output.find(OpKind::BeginHyperlink)->url == "https://ci.example.com/job");
    CHECK(output.count(OpKind::EndHyperlink) == 1);
    CHECK(output.text().find("Linux") != std::string::npos);
    CHECK(output.text().find("badge.svg") == std::string::npos);
    CHECK(output.text().find("![") == std::string::npos);
}

TEST_CASE("MarkdownRenderer.link.badge_with_relative_target_is_not_hyperlinked")
{
    RecordingOutput output;
    MarkdownRenderer renderer(output);

    renderer.render("[![alt](x.png)](#install)\n");

    CHECK(output.count(OpKind::Hyperlink) == 0);
    CHECK(output.count(OpKind::BeginHyperlink) == 0);
    CHECK(output.text().find("alt") != std::string::npos);
}

TEST_CASE("MarkdownRenderer.link.label_keeps_inline_markdown")
{
    StyledCapturingOutput output;
    MarkdownRenderer renderer(output);

    renderer.render("[**bold**](https://x.com)\n");

    auto const* const span = output.findSpan("bold");
    REQUIRE(span != nullptr);
    CHECK(span->style.bold);
}

TEST_CASE("MarkdownRenderer.link.bare_url_is_not_autolinked")
{
    RecordingOutput output;
    MarkdownRenderer renderer(output);

    renderer.render("Go to https://example.com now\n");

    CHECK(output.count(OpKind::Hyperlink) == 0);
    CHECK(output.count(OpKind::BeginHyperlink) == 0);
    CHECK(output.text().find("https://example.com") != std::string::npos);
}

TEST_CASE("MarkdownRenderer.link.html_anchor_in_paragraph_is_hyperlinked")
{
    RecordingOutput output;
    MarkdownRenderer renderer(output);

    renderer.render("See <a href=\"https://endo-lang.org/\">docs</a> here.\n");

    // Regression: the attribute's "://" used to trip the autolink branch, emitting
    // a hyperlink whose URL was the literal `a href="https://endo-lang.org/"`.
    REQUIRE(output.count(OpKind::BeginHyperlink) == 1);
    CHECK(output.find(OpKind::BeginHyperlink)->url == "https://endo-lang.org/");
    CHECK(output.text().find("docs") != std::string::npos);
    CHECK(output.text().find("a href") == std::string::npos);
    CHECK(output.text().find("</a>") == std::string::npos);
}

TEST_CASE("MarkdownRenderer.link.html_anchor_with_relative_href_is_not_hyperlinked")
{
    RecordingOutput output;
    MarkdownRenderer renderer(output);

    renderer.render("<a href=\"LICENSE\">License</a>\n");

    CHECK(output.count(OpKind::BeginHyperlink) == 0);
    CHECK(output.text().find("License") != std::string::npos);
}

TEST_CASE("MarkdownRenderer.link.autolink_rejects_tag_like_content")
{
    RecordingOutput output;
    MarkdownRenderer renderer(output);

    // A quoted attribute containing "://" must never become an autolink target.
    renderer.render("<span title=\"https://x.com\">hi</span>\n");

    CHECK(output.count(OpKind::Hyperlink) == 0);
    CHECK(output.count(OpKind::BeginHyperlink) == 0);
}

TEST_CASE("MarkdownRenderer.image.inline_html_img_renders_alt_text")
{
    RecordingOutput output;
    FakeImageProvider provider;
    MarkdownRenderer renderer(output);
    renderer.setImageProvider(&provider);

    renderer.render("before <img src=\"x.png\" alt=\"pic\"> after\n");

    CHECK(output.count(OpKind::Sixel) == 0);
    CHECK(output.text() == "before pic after\n");
}

TEST_CASE("MarkdownRenderer.link.unterminated_renders_literally")
{
    RecordingOutput output;
    MarkdownRenderer renderer(output);

    renderer.render("[text](https://x.com\n");

    CHECK(output.count(OpKind::BeginHyperlink) == 0);
    CHECK(output.text().find("[text](https://x.com") != std::string::npos);
}

TEST_CASE("MarkdownRenderer.link.multiple_links_on_one_line")
{
    RecordingOutput output;
    MarkdownRenderer renderer(output);

    renderer.render("[A](https://a.com) - [B](https://b.com)\n");

    CHECK(output.count(OpKind::BeginHyperlink) == 2);
    CHECK(output.count(OpKind::EndHyperlink) == 2);
    CHECK(output.text().find(" - ") != std::string::npos);
}

TEST_CASE("MarkdownRenderer.link.inside_heading")
{
    RecordingOutput output;
    MarkdownRenderer renderer(output);

    renderer.render("# See [Docs](https://x.com)\n");

    CHECK(output.count(OpKind::BeginHyperlink) == 1);
}

// ============================================================================
// Centered HTML block tests
// ============================================================================

namespace
{
/// @brief Widths of every Raw op consisting only of spaces, i.e. every alignment pad.
[[nodiscard]] auto collectPadWidths(RecordingOutput const& output) -> std::vector<int>
{
    auto widths = std::vector<int> {};
    for (auto const& op: output.ops)
    {
        if (op.kind != OpKind::Raw || op.text.empty())
            continue;
        if (std::ranges::all_of(op.text, [](char c) { return c == ' '; }))
            widths.push_back(static_cast<int>(op.text.size()));
    }
    return widths;
}

/// @brief Width of the first alignment pad, or 0 when nothing was padded.
[[nodiscard]] auto firstPadWidth(RecordingOutput const& output) -> int
{
    auto const widths = collectPadWidths(output);
    return widths.empty() ? 0 : widths.front();
}
} // namespace

TEST_CASE("MarkdownRenderer.center.div_align_center")
{
    RecordingOutput output(80);
    MarkdownRenderer renderer(output);

    renderer.render("<div align=\"center\">\nHello\n</div>\n");

    CHECK(firstPadWidth(output) == (80 - 5) / 2);
    CHECK(output.text().find("Hello") != std::string::npos);
    CHECK(output.text().find("<div") == std::string::npos);
    CHECK(output.text().find("</div>") == std::string::npos);
}

TEST_CASE("MarkdownRenderer.center.p_align_center_same_line")
{
    RecordingOutput output(80);
    MarkdownRenderer renderer(output);

    renderer.render("<p align=\"center\">Hi</p>\n");

    CHECK(firstPadWidth(output) == (80 - 2) / 2);
    CHECK(output.text().find("Hi") != std::string::npos);
}

TEST_CASE("MarkdownRenderer.center.legacy_center_tag")
{
    RecordingOutput output(80);
    MarkdownRenderer renderer(output);

    renderer.render("<center>Hi</center>\n");

    CHECK(firstPadWidth(output) == (80 - 2) / 2);
}

TEST_CASE("MarkdownRenderer.center.single_quotes_and_case_insensitive")
{
    RecordingOutput output(80);
    MarkdownRenderer renderer(output);

    renderer.render("<DIV ALIGN='CENTER'>\nHi\n</DIV>\n");

    CHECK(firstPadWidth(output) == (80 - 2) / 2);
}

TEST_CASE("MarkdownRenderer.center.attribute_order_does_not_matter")
{
    RecordingOutput output(80);
    MarkdownRenderer renderer(output);

    renderer.render("<div class=\"hero\" align=\"center\">\nHi\n</div>\n");

    CHECK(firstPadWidth(output) == (80 - 2) / 2);
}

TEST_CASE("MarkdownRenderer.center.align_right")
{
    RecordingOutput output(80);
    MarkdownRenderer renderer(output);

    renderer.render("<div align=\"right\">\nHi\n</div>\n");

    CHECK(firstPadWidth(output) == 80 - 2);
}

TEST_CASE("MarkdownRenderer.center.align_left_has_no_padding")
{
    RecordingOutput output(80);
    MarkdownRenderer renderer(output);

    renderer.render("<div align=\"left\">\nHi\n</div>\n");

    CHECK(firstPadWidth(output) == 0);
    CHECK(output.text().find("Hi") != std::string::npos);
}

TEST_CASE("MarkdownRenderer.center.markdown_heading_inside_centered_div")
{
    RecordingOutput output(80);
    MarkdownRenderer renderer(output);

    renderer.render("<div align=\"center\">\n\n# Endo\n\n</div>\n");

    // Single-width heading: pad against the full 80 columns.
    CHECK(firstPadWidth(output) == (80 - 4) / 2);
    CHECK(output.text().find("Endo") != std::string::npos);
}

TEST_CASE("MarkdownRenderer.center.double_width_h1_uses_half_field")
{
    RecordingOutput output(80);
    MarkdownRenderer renderer(output);
    renderer.setFullWidthMode(true);

    renderer.render("<div align=\"center\">\n\n# Endo\n\n</div>\n");

    // Under DECDHL every cell is two columns wide, so the field is 40, not 80.
    CHECK(firstPadWidth(output) == ((80 / 2) - 4) / 2);
    CHECK(output.count(OpKind::DoubleHeightTop) == 1);
    CHECK(output.count(OpKind::DoubleHeightBottom) == 1);
}

TEST_CASE("MarkdownRenderer.center.double_width_h2_uses_half_field")
{
    RecordingOutput output(80);
    MarkdownRenderer renderer(output);
    renderer.setFullWidthMode(true);

    renderer.render("<div align=\"center\">\n\n## Endo\n\n</div>\n");

    CHECK(firstPadWidth(output) == ((80 / 2) - 4) / 2);
    CHECK(output.count(OpKind::DoubleWidth) == 1);
}

TEST_CASE("MarkdownRenderer.center.html_heading_tag_inside_div")
{
    RecordingOutput output(80);
    MarkdownRenderer renderer(output);

    renderer.render("<div align=\"center\">\n<h1>Endo</h1>\n</div>\n");

    CHECK(firstPadWidth(output) == (80 - 4) / 2);
    CHECK(output.text().find("Endo") != std::string::npos);
    CHECK(output.text().find("<h1>") == std::string::npos);
}

TEST_CASE("MarkdownRenderer.center.h1_align_center_standalone")
{
    RecordingOutput output(80);
    MarkdownRenderer renderer(output);

    renderer.render("<h1 align=\"center\">Endo</h1>\n");

    CHECK(firstPadWidth(output) == (80 - 4) / 2);
}

TEST_CASE("MarkdownRenderer.center.wide_cjk_text_measured_by_display_width")
{
    RecordingOutput output(80);
    MarkdownRenderer renderer(output);

    // Three CJK glyphs occupy six columns.
    renderer.render("<div align=\"center\">\n\xe4\xbd\xa0\xe5\xa5\xbd\xe5\x90\x97\n</div>\n");

    CHECK(firstPadWidth(output) == (80 - 6) / 2);
}

TEST_CASE("MarkdownRenderer.center.content_wider_than_terminal_has_no_padding")
{
    RecordingOutput output(4);
    MarkdownRenderer renderer(output);

    renderer.render("<div align=\"center\">\nHello world\n</div>\n");

    CHECK(firstPadWidth(output) == 0);
}

TEST_CASE("MarkdownRenderer.center.unterminated_block_still_renders")
{
    RecordingOutput output(80);
    MarkdownRenderer renderer(output);

    renderer.render("<div align=\"center\">\nHi\n");

    CHECK(output.text().find("Hi") != std::string::npos);
}

TEST_CASE("MarkdownRenderer.center.unterminated_block_does_not_leak_into_next_render")
{
    RecordingOutput output(80);
    MarkdownRenderer renderer(output);

    renderer.render("<div align=\"center\">\nHi\n");
    output.ops.clear();
    renderer.render("Plain\n");

    CHECK(firstPadWidth(output) == 0);
}

TEST_CASE("MarkdownRenderer.center.nested_p_inside_div_inherits_then_restores")
{
    RecordingOutput output(80);
    MarkdownRenderer renderer(output);

    renderer.render("<div align=\"center\">\n<p align=\"left\">Left</p>\nMid\n</div>\n");

    // The <p align="left"> must not center, but the following div line must.
    auto const pads = collectPadWidths(output);

    REQUIRE(pads.size() == 1); // only "Mid" is padded
    CHECK(pads[0] == (80 - 3) / 2);
}

TEST_CASE("MarkdownRenderer.center.div_inside_code_fence_is_literal")
{
    RecordingOutput output(80);
    MarkdownRenderer renderer(output);

    renderer.render("```\n<div align=\"center\">\n```\nPlain\n");

    CHECK(output.text().find("<div align=\"center\">") != std::string::npos);
    CHECK(firstPadWidth(output) == 0);
}

TEST_CASE("MarkdownRenderer.center.html_anchor_becomes_hyperlink")
{
    RecordingOutput output(80);
    MarkdownRenderer renderer(output);

    renderer.render("<p align=\"center\"><a href=\"https://x.com\">Docs</a></p>\n");

    REQUIRE(output.count(OpKind::BeginHyperlink) == 1);
    CHECK(output.find(OpKind::BeginHyperlink)->url == "https://x.com");
    CHECK(output.text().find("Docs") != std::string::npos);
}

TEST_CASE("MarkdownRenderer.center.br_splits_lines")
{
    RecordingOutput output(80);
    MarkdownRenderer renderer(output);

    renderer.render("<p align=\"center\">One<br>Two</p>\n");

    CHECK(output.text().find("One") != std::string::npos);
    CHECK(output.text().find("Two") != std::string::npos);
    CHECK(output.text().find("<br>") == std::string::npos);
}

// ============================================================================
// Left margin (indent)
// ============================================================================

TEST_CASE("MarkdownRenderer.indent.defaults_to_none")
{
    RecordingOutput output(80);
    MarkdownRenderer renderer(output);

    renderer.render("Hello\n");

    CHECK(output.text() == "Hello\n");
}

TEST_CASE("MarkdownRenderer.indent.paragraph")
{
    RecordingOutput output(80);
    MarkdownRenderer renderer(output);
    renderer.setIndent(2);

    renderer.render("Hello\n");

    CHECK(output.text() == "  Hello\n");
}

TEST_CASE("MarkdownRenderer.indent.negative_is_clamped_to_zero")
{
    RecordingOutput output(80);
    MarkdownRenderer renderer(output);
    renderer.setIndent(-5);

    renderer.render("Hello\n");

    CHECK(output.text() == "Hello\n");
}

TEST_CASE("MarkdownRenderer.indent.applies_to_every_block_kind")
{
    RecordingOutput output(80);
    MarkdownRenderer renderer(output);
    renderer.setIndent(3);

    renderer.render("# Head\n"
                    "Para\n"
                    "- item\n"
                    "> quote\n"
                    "```\n"
                    "code\n"
                    "```\n");

    auto const lines = output.text();
    CHECK(lines.find("   Head") != std::string::npos);
    CHECK(lines.find("   Para") != std::string::npos);
    CHECK(lines.find("   - item") != std::string::npos);
    CHECK(lines.find("   \xe2\x94\x82 quote") != std::string::npos); // "   │ quote"
    CHECK(lines.find("   code") != std::string::npos);
}

TEST_CASE("MarkdownRenderer.indent.blank_lines_stay_empty")
{
    RecordingOutput output(80);
    MarkdownRenderer renderer(output);
    renderer.setIndent(4);

    renderer.render("a\n\nb\n");

    // No trailing whitespace on the blank line.
    CHECK(output.text() == "    a\n\n    b\n");
}

TEST_CASE("MarkdownRenderer.indent.table_rows_are_indented")
{
    RecordingOutput output(80);
    MarkdownRenderer renderer(output);
    renderer.setIndent(2);

    renderer.render("| A | B |\n|---|---|\n| 1 | 2 |\n");

    for (auto const& line: output.text() | std::views::split('\n'))
    {
        auto const text = std::string(line.begin(), line.end());
        if (text.empty())
            continue;
        INFO("line: " << text);
        CHECK(text.starts_with("  "));
    }
}

TEST_CASE("MarkdownRenderer.indent.shrinks_the_field_available_for_centering")
{
    RecordingOutput output(80);
    MarkdownRenderer renderer(output);
    renderer.setIndent(10);

    renderer.render("<div align=\"center\">\nHi\n</div>\n");

    // Content field is 80 - 10 = 70; the pad is the indent plus the centering slack.
    CHECK(firstPadWidth(output) == 10 + ((70 - 2) / 2));
}

TEST_CASE("MarkdownRenderer.indent.right_alignment_ends_at_the_right_margin")
{
    RecordingOutput output(80);
    MarkdownRenderer renderer(output);
    renderer.setIndent(6);

    renderer.render("<div align=\"right\">\nHi\n</div>\n");

    // Indent + all remaining slack, so the text still ends at column 80.
    CHECK(firstPadWidth(output) == 6 + (74 - 2));
}

TEST_CASE("MarkdownRenderer.indent.double_width_heading_costs_two_columns_per_cell")
{
    RecordingOutput output(80);
    MarkdownRenderer renderer(output);
    renderer.setFullWidthMode(true);
    renderer.setIndent(2);

    renderer.render("<div align=\"center\">\n\n# Endo\n\n</div>\n");

    // A double-width line holds 40 cells; the indent consumes 2 of them.
    CHECK(firstPadWidth(output) == 2 + (((80 / 2) - 2 - 4) / 2));
    CHECK(output.count(OpKind::DoubleHeightTop) == 1);
}

TEST_CASE("MarkdownRenderer.indent.wider_than_terminal_does_not_underflow")
{
    RecordingOutput output(4);
    MarkdownRenderer renderer(output);
    renderer.setIndent(100);

    renderer.render("<div align=\"center\">\nHi\n</div>\n");

    // effectiveWidth() clamps to 0; the indent is still written, nothing wraps around.
    CHECK(firstPadWidth(output) == 100);
}

// ============================================================================
// Inline Sixel image tests
// ============================================================================

TEST_CASE("MarkdownRenderer.image.sixel_emitted_for_standalone_image")
{
    RecordingOutput output(80);
    FakeImageProvider provider;
    MarkdownRenderer renderer(output);
    renderer.setImageProvider(&provider);

    renderer.render("![alt](logo.png)\n");

    REQUIRE(output.count(OpKind::Sixel) == 1);
    CHECK(output.find(OpKind::Sixel)->text == "SIXELDATA");
    CHECK(output.text().find("alt") == std::string::npos);
    CHECK(provider.lastSrc == "logo.png");
}

TEST_CASE("MarkdownRenderer.image.commonmark_title_is_not_part_of_the_path")
{
    RecordingOutput output(80);
    FakeImageProvider provider;
    MarkdownRenderer renderer(output);
    renderer.setImageProvider(&provider);

    // Regression: the title used to be swallowed into the source path, so the
    // file was never found and the image silently degraded to alt text.
    renderer.render("![shot](docs/shot.png \"Screenshot\")\n");

    REQUIRE(output.count(OpKind::Sixel) == 1);
    CHECK(provider.lastSrc == "docs/shot.png");
}

TEST_CASE("MarkdownRenderer.image.single_quoted_title")
{
    RecordingOutput output(80);
    FakeImageProvider provider;
    MarkdownRenderer renderer(output);
    renderer.setImageProvider(&provider);

    renderer.render("![a](x.png 'Title')\n");

    CHECK(provider.lastSrc == "x.png");
}

TEST_CASE("MarkdownRenderer.image.angle_bracketed_path_with_spaces")
{
    RecordingOutput output(80);
    FakeImageProvider provider;
    MarkdownRenderer renderer(output);
    renderer.setImageProvider(&provider);

    renderer.render("![a](<my logo.png>)\n");

    CHECK(provider.lastSrc == "my logo.png");
}

TEST_CASE("MarkdownRenderer.link.title_is_stripped_from_url")
{
    RecordingOutput output;
    MarkdownRenderer renderer(output);

    renderer.render("[Docs](https://endo-lang.org/ \"The docs\")\n");

    REQUIRE(output.count(OpKind::BeginHyperlink) == 1);
    CHECK(output.find(OpKind::BeginHyperlink)->url == "https://endo-lang.org/");
    CHECK(output.text().find("The docs") == std::string::npos);
}

TEST_CASE("MarkdownRenderer.link.title_containing_a_paren_does_not_truncate")
{
    RecordingOutput output;
    MarkdownRenderer renderer(output);

    renderer.render("[D](https://x.com \"a) b\") tail\n");

    REQUIRE(output.count(OpKind::BeginHyperlink) == 1);
    CHECK(output.find(OpKind::BeginHyperlink)->url == "https://x.com");
    CHECK(output.text().find("tail") != std::string::npos);
}

TEST_CASE("MarkdownRenderer.image.sixel_payload_is_unframed")
{
    RecordingOutput output(80);
    FakeImageProvider provider;
    MarkdownRenderer renderer(output);
    renderer.setImageProvider(&provider);

    renderer.render("![a](x.png)\n");

    // TerminalOutput::writeSixel() adds the DCS framing; the renderer must not.
    auto const* const sixel = output.find(OpKind::Sixel);
    REQUIRE(sixel != nullptr);
    CHECK(sixel->text.find("\033P") == std::string::npos);
    CHECK(sixel->text.find("\033\\") == std::string::npos);
}

TEST_CASE("MarkdownRenderer.image.capability_off_falls_back_to_alt_text")
{
    RecordingOutput output(80);
    FakeImageProvider provider;
    provider.supports = false;
    MarkdownRenderer renderer(output);
    renderer.setImageProvider(&provider);

    renderer.render("![alt](logo.png)\n");

    CHECK(output.count(OpKind::Sixel) == 0);
    CHECK(provider.prepareCalls == 0);
    CHECK(output.text().find("alt") != std::string::npos);
}

TEST_CASE("MarkdownRenderer.image.no_provider_renders_alt_text")
{
    RecordingOutput output(80);
    MarkdownRenderer renderer(output);

    // Regression guard: this used to render the literal "!alt".
    renderer.render("![alt](logo.png)\n");

    CHECK(output.count(OpKind::Sixel) == 0);
    CHECK(output.text().find("alt") != std::string::npos);
    CHECK(output.text().find("!alt") == std::string::npos);
}

TEST_CASE("MarkdownRenderer.image.every_failure_produces_identical_output")
{
    auto renderWithError = [](std::string error) {
        auto output = RecordingOutput(80);
        auto provider = FakeImageProvider {};
        provider.next = std::unexpected(std::move(error));
        auto renderer = MarkdownRenderer(output);
        renderer.setImageProvider(&provider);
        renderer.render("![alt](logo.png)\n");
        return output.text();
    };

    auto const missing = renderWithError("no such file");
    auto const decode = renderWithError("stbi: corrupt PNG");
    auto const encode = renderWithError("sixel encode failed");

    CHECK(missing == decode);
    CHECK(decode == encode);
    CHECK(missing.find("alt") != std::string::npos);
}

TEST_CASE("MarkdownRenderer.image.placement_left_aligned_has_no_move_right")
{
    RecordingOutput output(80);
    FakeImageProvider provider;
    MarkdownRenderer renderer(output);
    renderer.setImageProvider(&provider);

    renderer.render("![a](x.png)\n");

    CHECK(output.count(OpKind::MoveRight) == 0);
    CHECK(output.count(OpKind::SaveCursor) == 1);
    CHECK(output.count(OpKind::RestoreCursor) == 1);
}

TEST_CASE("MarkdownRenderer.image.placement_centered_offset_is_exact")
{
    RecordingOutput output(80);
    FakeImageProvider provider; // cellWidth == 20
    MarkdownRenderer renderer(output);
    renderer.setImageProvider(&provider);

    renderer.render("<div align=\"center\">\n![a](x.png)\n</div>\n");

    REQUIRE(output.count(OpKind::MoveRight) == 1);
    CHECK(output.find(OpKind::MoveRight)->n == (80 - 20) / 2);
}

TEST_CASE("MarkdownRenderer.image.placement_clamped_when_wider_than_terminal")
{
    RecordingOutput output(10);
    FakeImageProvider provider; // cellWidth == 20 > 10 columns
    MarkdownRenderer renderer(output);
    renderer.setImageProvider(&provider);

    renderer.render("<div align=\"center\">\n![a](x.png)\n</div>\n");

    CHECK(output.count(OpKind::MoveRight) == 0);
    CHECK(output.count(OpKind::Sixel) == 1);
}

TEST_CASE("MarkdownRenderer.image.row_advancement_matches_cell_height")
{
    RecordingOutput output(80);
    FakeImageProvider provider; // cellHeight == 5
    MarkdownRenderer renderer(output);
    renderer.setImageProvider(&provider);

    renderer.render("![a](x.png)\n");

    CHECK(output.count(OpKind::Linefeed) == 5);
}

TEST_CASE("MarkdownRenderer.image.call_ordering_invariant")
{
    RecordingOutput output(80);
    FakeImageProvider provider;
    MarkdownRenderer renderer(output);
    renderer.setImageProvider(&provider);

    renderer.render("<div align=\"center\">\n![a](x.png)\n</div>\n");

    CHECK(output.indexOf(OpKind::SaveCursor) < output.indexOf(OpKind::MoveRight));
    CHECK(output.indexOf(OpKind::MoveRight) < output.indexOf(OpKind::Sixel));
    CHECK(output.indexOf(OpKind::Sixel) < output.indexOf(OpKind::RestoreCursor));
    CHECK(output.indexOf(OpKind::RestoreCursor) < output.indexOf(OpKind::Linefeed));
}

TEST_CASE("MarkdownRenderer.image.html_img_width_attribute_is_forwarded")
{
    RecordingOutput output(80);
    FakeImageProvider provider;
    MarkdownRenderer renderer(output);
    renderer.setImageProvider(&provider);

    renderer.render("<img src=\"logo.png\" width=\"200\">\n");

    REQUIRE(output.count(OpKind::Sixel) == 1);
    CHECK(provider.lastSrc == "logo.png");
    REQUIRE(provider.lastWidthPx.has_value());
    CHECK(*provider.lastWidthPx == 200);
}

TEST_CASE("MarkdownRenderer.image.html_img_percentage_width_is_auto")
{
    RecordingOutput output(80);
    FakeImageProvider provider;
    MarkdownRenderer renderer(output);
    renderer.setImageProvider(&provider);

    renderer.render("<img src=\"logo.png\" width=\"50%\">\n");

    CHECK_FALSE(provider.lastWidthPx.has_value());
}

TEST_CASE("MarkdownRenderer.image.html_img_centered_inside_paragraph")
{
    RecordingOutput output(80);
    FakeImageProvider provider;
    MarkdownRenderer renderer(output);
    renderer.setImageProvider(&provider);

    renderer.render("<p align=\"center\"><img src=\"logo.png\" width=\"120\"></p>\n");

    REQUIRE(output.count(OpKind::Sixel) == 1);
    REQUIRE(output.count(OpKind::MoveRight) == 1);
    CHECK(output.find(OpKind::MoveRight)->n == (80 - 20) / 2);
    REQUIRE(provider.lastWidthPx.has_value());
    CHECK(*provider.lastWidthPx == 120);
}

TEST_CASE("MarkdownRenderer.image.inline_image_renders_alt_text_only")
{
    RecordingOutput output(80);
    FakeImageProvider provider;
    MarkdownRenderer renderer(output);
    renderer.setImageProvider(&provider);

    renderer.render("text ![a](x.png) more\n");

    CHECK(output.count(OpKind::Sixel) == 0);
    CHECK(provider.prepareCalls == 0);
    CHECK(output.text() == "text a more\n");
}

TEST_CASE("MarkdownRenderer.image.image_in_list_item_renders_alt_text")
{
    RecordingOutput output(80);
    FakeImageProvider provider;
    MarkdownRenderer renderer(output);
    renderer.setImageProvider(&provider);

    renderer.render("- ![a](x.png)\n");

    CHECK(output.count(OpKind::Sixel) == 0);
    CHECK(output.text().find('a') != std::string::npos);
}

TEST_CASE("MarkdownRenderer.image.empty_alt_on_failure_emits_only_newline")
{
    RecordingOutput output(80);
    FakeImageProvider provider;
    provider.next = std::unexpected(std::string("boom"));
    MarkdownRenderer renderer(output);
    renderer.setImageProvider(&provider);

    renderer.render("![](x.png)\n");

    CHECK(output.count(OpKind::Sixel) == 0);
    CHECK(output.text() == "\n");
}

TEST_CASE("MarkdownRenderer.image.remote_source_is_still_delegated_to_provider")
{
    RecordingOutput output(80);
    FakeImageProvider provider;
    provider.next = std::unexpected(std::string("remote image not supported"));
    MarkdownRenderer renderer(output);
    renderer.setImageProvider(&provider);

    renderer.render("![alt](https://x.com/l.png)\n");

    CHECK(output.count(OpKind::Sixel) == 0);
    CHECK(output.text().find("alt") != std::string::npos);
}

TEST_CASE("MarkdownRenderer.image.indent_offsets_a_left_aligned_image")
{
    RecordingOutput output(80);
    FakeImageProvider provider;
    MarkdownRenderer renderer(output);
    renderer.setImageProvider(&provider);
    renderer.setIndent(3);

    renderer.render("![a](x.png)\n");

    REQUIRE(output.count(OpKind::MoveRight) == 1);
    CHECK(output.find(OpKind::MoveRight)->n == 3);
}

TEST_CASE("MarkdownRenderer.image.indent_shifts_a_centered_image")
{
    RecordingOutput output(80);
    FakeImageProvider provider; // cellWidth == 20
    MarkdownRenderer renderer(output);
    renderer.setImageProvider(&provider);
    renderer.setIndent(4);

    renderer.render("<div align=\"center\">\n![a](x.png)\n</div>\n");

    REQUIRE(output.count(OpKind::MoveRight) == 1);
    CHECK(output.find(OpKind::MoveRight)->n == 4 + ((76 - 20) / 2));
}

TEST_CASE("MarkdownRenderer.image.indent_offsets_alt_text_fallback")
{
    RecordingOutput output(80);
    FakeImageProvider provider;
    provider.supports = false;
    MarkdownRenderer renderer(output);
    renderer.setImageProvider(&provider);
    renderer.setIndent(2);

    renderer.render("![alt](x.png)\n");

    CHECK(output.text() == "  alt\n");
}
