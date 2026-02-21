// SPDX-License-Identifier: Apache-2.0
#include <tui/Theme.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <ranges>
#include <string>

#include <agent/tools/DiffRenderer.hpp>

using namespace endo::agent;

namespace
{

/// @brief Counts diff lines of a given type.
[[nodiscard]] auto countType(std::vector<DiffLine> const& lines, DiffLineType type) -> int
{
    return static_cast<int>(std::ranges::count_if(lines, [type](auto const& l) { return l.type == type; }));
}

} // namespace

TEST_CASE("DiffRenderer.identical_strings", "[agent][tools]")
{
    auto const diff = generateUnifiedDiff("hello\nworld\n", "hello\nworld\n");
    CHECK(diff.empty());
}

TEST_CASE("DiffRenderer.single_line_change", "[agent][tools]")
{
    auto const diff = generateUnifiedDiff("int y = 1;\n", "int y = 42;\n");
    REQUIRE(!diff.empty());
    CHECK(countType(diff, DiffLineType::Deletion) == 1);
    CHECK(countType(diff, DiffLineType::Addition) == 1);
    CHECK(countType(diff, DiffLineType::Hunk) == 1);

    // Check the content of the changed lines.
    auto const del =
        std::ranges::find_if(diff, [](auto const& l) { return l.type == DiffLineType::Deletion; });
    auto const add =
        std::ranges::find_if(diff, [](auto const& l) { return l.type == DiffLineType::Addition; });
    REQUIRE(del != diff.end());
    REQUIRE(add != diff.end());
    CHECK(del->text == "int y = 1;");
    CHECK(add->text == "int y = 42;");
}

TEST_CASE("DiffRenderer.multi_line_addition", "[agent][tools]")
{
    auto const diff = generateUnifiedDiff("line1\nline3\n", "line1\nline2\nline3\n");
    REQUIRE(!diff.empty());
    CHECK(countType(diff, DiffLineType::Addition) == 1);
    CHECK(countType(diff, DiffLineType::Deletion) == 0);

    auto const add =
        std::ranges::find_if(diff, [](auto const& l) { return l.type == DiffLineType::Addition; });
    REQUIRE(add != diff.end());
    CHECK(add->text == "line2");
}

TEST_CASE("DiffRenderer.multi_line_deletion", "[agent][tools]")
{
    auto const diff = generateUnifiedDiff("line1\nline2\nline3\n", "line1\nline3\n");
    REQUIRE(!diff.empty());
    CHECK(countType(diff, DiffLineType::Deletion) == 1);
    CHECK(countType(diff, DiffLineType::Addition) == 0);

    auto const del =
        std::ranges::find_if(diff, [](auto const& l) { return l.type == DiffLineType::Deletion; });
    REQUIRE(del != diff.end());
    CHECK(del->text == "line2");
}

TEST_CASE("DiffRenderer.mixed_changes_with_context", "[agent][tools]")
{
    auto const* const oldText = "a\nb\nc\nd\ne\nf\ng\n";
    auto const* const newText = "a\nb\nC\nd\ne\nF\ng\n";

    auto const diff = generateUnifiedDiff(oldText, newText);
    REQUIRE(!diff.empty());

    // Two deletions (c, f) and two additions (C, F).
    CHECK(countType(diff, DiffLineType::Deletion) == 2);
    CHECK(countType(diff, DiffLineType::Addition) == 2);

    // Context lines should be present.
    CHECK(countType(diff, DiffLineType::Context) > 0);
}

TEST_CASE("DiffRenderer.empty_old_string", "[agent][tools]")
{
    auto const diff = generateUnifiedDiff("", "new line 1\nnew line 2\n");
    REQUIRE(!diff.empty());
    // Trailing newline produces an extra empty line element, so 3 additions total.
    CHECK(countType(diff, DiffLineType::Addition) == 3);
    CHECK(countType(diff, DiffLineType::Deletion) == 0);
}

TEST_CASE("DiffRenderer.empty_new_string", "[agent][tools]")
{
    auto const diff = generateUnifiedDiff("old line 1\nold line 2\n", "");
    REQUIRE(!diff.empty());
    // Trailing newline produces an extra empty line element, so 3 deletions total.
    CHECK(countType(diff, DiffLineType::Deletion) == 3);
    CHECK(countType(diff, DiffLineType::Addition) == 0);
}

TEST_CASE("DiffRenderer.both_empty", "[agent][tools]")
{
    auto const diff = generateUnifiedDiff("", "");
    CHECK(diff.empty());
}

TEST_CASE("DiffRenderer.context_lines_parameter", "[agent][tools]")
{
    // Create text with a change far from the edges to test context control.
    auto oldText = std::string {};
    auto newText = std::string {};
    for (auto i: std::views::iota(1, 21))
    {
        oldText += std::format("line {}\n", i);
        if (i == 10)
            newText += "CHANGED\n";
        else
            newText += std::format("line {}\n", i);
    }

    // With 1 context line, we should have fewer context lines than with 3.
    auto const diff1 = generateUnifiedDiff(oldText, newText, 1);
    auto const diff3 = generateUnifiedDiff(oldText, newText, 3);

    auto const ctx1 = countType(diff1, DiffLineType::Context);
    auto const ctx3 = countType(diff3, DiffLineType::Context);
    CHECK(ctx1 < ctx3);
    CHECK(ctx1 == 2); // One line before, one after.
    CHECK(ctx3 == 6); // Three lines before, three after.
}

TEST_CASE("DiffRenderer.hunk_header_format", "[agent][tools]")
{
    auto const diff = generateUnifiedDiff("a\nb\nc\n", "a\nB\nc\n");
    REQUIRE(!diff.empty());

    auto const hunk = std::ranges::find_if(diff, [](auto const& l) { return l.type == DiffLineType::Hunk; });
    REQUIRE(hunk != diff.end());
    CHECK(hunk->text.starts_with("@@ -"));
    CHECK(hunk->text.ends_with(" @@"));
}

TEST_CASE("DiffRenderer.line_numbers_are_set", "[agent][tools]")
{
    auto const diff = generateUnifiedDiff("a\nb\nc\n", "a\nB\nc\n");
    REQUIRE(!diff.empty());

    for (auto const& line: diff)
    {
        switch (line.type)
        {
            case DiffLineType::Context:
                CHECK(line.oldLineNum > 0);
                CHECK(line.newLineNum > 0);
                break;
            case DiffLineType::Deletion:
                CHECK(line.oldLineNum > 0);
                CHECK(line.newLineNum == 0);
                break;
            case DiffLineType::Addition:
                CHECK(line.oldLineNum == 0);
                CHECK(line.newLineNum > 0);
                break;
            case DiffLineType::Hunk: break; // No line numbers for hunk headers.
        }
    }
}

TEST_CASE("DiffRenderer.large_edit_truncation", "[agent][tools]")
{
    // Generate a diff with more than LargeEditThreshold changed lines.
    auto oldText = std::string {};
    auto newText = std::string {};
    for (auto i: std::views::iota(0, LargeEditThreshold + 20))
    {
        oldText += std::format("old line {}\n", i);
        newText += std::format("new line {}\n", i);
    }

    auto diff = generateUnifiedDiff(oldText, newText);
    auto const changedLines =
        countType(diff, DiffLineType::Deletion) + countType(diff, DiffLineType::Addition);
    CHECK(changedLines > LargeEditThreshold);
}

TEST_CASE("DiffRenderer.no_trailing_newline", "[agent][tools]")
{
    auto const diff = generateUnifiedDiff("hello", "world");
    REQUIRE(!diff.empty());
    CHECK(countType(diff, DiffLineType::Deletion) == 1);
    CHECK(countType(diff, DiffLineType::Addition) == 1);
}

TEST_CASE("DiffRenderer.multiline_to_single", "[agent][tools]")
{
    auto const diff = generateUnifiedDiff("a\nb\nc\n", "x\n");
    REQUIRE(!diff.empty());
    CHECK(countType(diff, DiffLineType::Deletion) == 3);
    CHECK(countType(diff, DiffLineType::Addition) == 1);
}

TEST_CASE("DiffRenderer.single_to_multiline", "[agent][tools]")
{
    auto const diff = generateUnifiedDiff("x\n", "a\nb\nc\n");
    REQUIRE(!diff.empty());
    CHECK(countType(diff, DiffLineType::Deletion) == 1);
    CHECK(countType(diff, DiffLineType::Addition) == 3);
}
