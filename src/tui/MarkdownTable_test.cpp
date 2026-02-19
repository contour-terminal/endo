// SPDX-License-Identifier: Apache-2.0
#include <tui/MarkdownTable.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace tui;

// ============================================================================
// detectTableRow tests
// ============================================================================

TEST_CASE("MarkdownTable.detectTableRow.basic")
{
    CHECK(detectTableRow("| A | B |"));
    CHECK(detectTableRow("| A |"));
    CHECK(detectTableRow("|A|B|"));
    CHECK(detectTableRow("  | A | B |")); // leading whitespace
}

TEST_CASE("MarkdownTable.detectTableRow.not_a_row")
{
    CHECK_FALSE(detectTableRow(""));
    CHECK_FALSE(detectTableRow("Hello world"));
    CHECK_FALSE(detectTableRow("- list item"));
    CHECK_FALSE(detectTableRow("# heading"));
}

// ============================================================================
// detectTableSeparator tests
// ============================================================================

TEST_CASE("MarkdownTable.detectTableSeparator.basic")
{
    CHECK(detectTableSeparator("|---|---|"));
    CHECK(detectTableSeparator("| --- | --- |"));
    CHECK(detectTableSeparator("|:---|---:|"));
    CHECK(detectTableSeparator("|:---:|:---:|"));
    CHECK(detectTableSeparator("| :--- | ---: | :---: |"));
    CHECK(detectTableSeparator("|---------|"));
}

TEST_CASE("MarkdownTable.detectTableSeparator.not_a_separator")
{
    CHECK_FALSE(detectTableSeparator("| A | B |"));
    CHECK_FALSE(detectTableSeparator("|   |   |"));
    CHECK_FALSE(detectTableSeparator(""));
    CHECK_FALSE(detectTableSeparator("Hello"));
    CHECK_FALSE(detectTableSeparator("| :abc: |"));
}

// ============================================================================
// splitTableRow tests
// ============================================================================

TEST_CASE("MarkdownTable.splitTableRow.basic")
{
    auto const cells = splitTableRow("| Alice | 30 | Berlin |");
    REQUIRE(cells.size() == 3);
    CHECK(cells[0] == "Alice");
    CHECK(cells[1] == "30");
    CHECK(cells[2] == "Berlin");
}

TEST_CASE("MarkdownTable.splitTableRow.no_trailing_pipe")
{
    auto const cells = splitTableRow("| A | B ");
    REQUIRE(cells.size() == 2);
    CHECK(cells[0] == "A");
    CHECK(cells[1] == "B");
}

TEST_CASE("MarkdownTable.splitTableRow.empty_cells")
{
    auto const cells = splitTableRow("| | B | |");
    REQUIRE(cells.size() == 3);
    CHECK(cells[0].empty());
    CHECK(cells[1] == "B");
    CHECK(cells[2].empty());
}

TEST_CASE("MarkdownTable.splitTableRow.single_cell")
{
    auto const cells = splitTableRow("| Hello |");
    REQUIRE(cells.size() == 1);
    CHECK(cells[0] == "Hello");
}

// ============================================================================
// parseTableAlignments tests
// ============================================================================

TEST_CASE("MarkdownTable.parseTableAlignments.all_types")
{
    auto const aligns = parseTableAlignments("| :--- | ---: | :---: | --- |");
    REQUIRE(aligns.size() == 4);
    CHECK(aligns[0] == TableAlignment::Left);
    CHECK(aligns[1] == TableAlignment::Right);
    CHECK(aligns[2] == TableAlignment::Center);
    CHECK(aligns[3] == TableAlignment::Left);
}

TEST_CASE("MarkdownTable.parseTableAlignments.default_left")
{
    auto const aligns = parseTableAlignments("|---|---|");
    REQUIRE(aligns.size() == 2);
    CHECK(aligns[0] == TableAlignment::Left);
    CHECK(aligns[1] == TableAlignment::Left);
}

// ============================================================================
// computeColumnWidths tests
// ============================================================================

TEST_CASE("MarkdownTable.computeColumnWidths.basic")
{
    auto table = ParsedTable {};
    table.headers = { "Name", "Age", "City" };
    table.columnCount = 3;
    table.rows = { { "Alice", "30", "Berlin" }, { "Bob", "25", "London" } };
    table.alignments = { TableAlignment::Left, TableAlignment::Right, TableAlignment::Center };

    auto const widths = computeColumnWidths(table);
    REQUIRE(widths.size() == 3);
    CHECK(widths[0] == 5); // "Alice" is widest
    CHECK(widths[1] == 3); // "Age" == "30" both width 3
    CHECK(widths[2] == 6); // "Berlin"/"London" are widest
}

TEST_CASE("MarkdownTable.computeColumnWidths.minimum_3")
{
    auto table = ParsedTable {};
    table.headers = { "A", "B" };
    table.columnCount = 2;
    table.rows = { { "x", "y" } };
    table.alignments = { TableAlignment::Left, TableAlignment::Left };

    auto const widths = computeColumnWidths(table);
    CHECK(widths[0] == 3); // minimum
    CHECK(widths[1] == 3); // minimum
}

// ============================================================================
// alignCell tests
// ============================================================================

TEST_CASE("MarkdownTable.alignCell.left")
{
    CHECK(alignCell("Hi", 6, TableAlignment::Left) == "Hi    ");
}

TEST_CASE("MarkdownTable.alignCell.right")
{
    CHECK(alignCell("Hi", 6, TableAlignment::Right) == "    Hi");
}

TEST_CASE("MarkdownTable.alignCell.center")
{
    CHECK(alignCell("Hi", 6, TableAlignment::Center) == "  Hi  ");
}

TEST_CASE("MarkdownTable.alignCell.center_odd_padding")
{
    CHECK(alignCell("Hi", 7, TableAlignment::Center) == "  Hi   ");
}

TEST_CASE("MarkdownTable.alignCell.exact_width")
{
    CHECK(alignCell("Hello", 5, TableAlignment::Left) == "Hello");
    CHECK(alignCell("Hello", 5, TableAlignment::Right) == "Hello");
    CHECK(alignCell("Hello", 5, TableAlignment::Center) == "Hello");
}
