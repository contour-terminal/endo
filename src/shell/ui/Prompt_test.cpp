// SPDX-License-Identifier: Apache-2.0
#include <shell/ui/Prompt.hpp>

#include <core/tui/MockTerminalOutput.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

using namespace endo;

namespace
{

/// Row of the top of the simulated prompt region on the mock screen.
constexpr auto RegionStart = 5;

/// Places the mock's cursor on the prompt's current input row.
void placeCursorOnInputRow(core::tui::MockTerminalOutput& out, detail::TransientGeometry const& geometry)
{
    auto const currentRow = geometry.topPad + geometry.aurora + geometry.chrome + geometry.cursorLine;
    // moveTo is 1-based; the mock reports 0-based rows.
    out.moveTo(RegionStart + currentRow + 1, 8);
}

/// Mock that records the order of the writes and erases it receives.
class RecordingOutput: public core::tui::MockTerminalOutput
{
  public:
    using MockTerminalOutput::MockTerminalOutput;

    void writeText(std::string_view text, core::tui::Style const& style) override
    {
        events.push_back("write:" + std::string(text));
        MockTerminalOutput::writeText(text, style);
    }

    void clearLine() override
    {
        events.emplace_back("clearLine");
        MockTerminalOutput::clearLine();
    }

    void clearToEndOfLine() override
    {
        events.emplace_back("clearToEndOfLine");
        MockTerminalOutput::clearToEndOfLine();
    }

    void moveDown(int n) override
    {
        events.emplace_back("moveDown");
        MockTerminalOutput::moveDown(n);
    }

    std::vector<std::string> events; ///< Calls in the order they were made.
};

/// Index of the first event equal to @p name, or the event count when absent.
std::size_t indexOf(std::vector<std::string> const& events, std::string_view name)
{
    return static_cast<std::size_t>(std::ranges::find(events, name) - events.begin());
}

} // namespace

TEST_CASE("Prompt.transient_collapse_deletes_aurora_row")
{
    auto const geometry = detail::TransientGeometry {
        .topPad = 1, .aurora = 1, .chrome = 2, .inputLines = 3, .cursorLine = 1, .botPad = 1
    };
    auto out = core::tui::MockTerminalOutput(80, 40);
    placeCursorOnInputRow(out, geometry);

    detail::collapseForTransient(out, geometry);

    CHECK(out.deletedLines() == 1);
    CHECK(out.cursorRow() == RegionStart + geometry.topPad);
    CHECK(out.cursorCol() == 0);
}

TEST_CASE("Prompt.transient_collapse_without_aurora")
{
    auto const geometry = detail::TransientGeometry {
        .topPad = 1, .aurora = 0, .chrome = 2, .inputLines = 3, .cursorLine = 1, .botPad = 1
    };
    auto out = core::tui::MockTerminalOutput(80, 40);
    placeCursorOnInputRow(out, geometry);

    detail::collapseForTransient(out, geometry);

    CHECK(out.deletedLines() == 0);
    CHECK(out.cursorRow() == RegionStart + geometry.topPad);
    CHECK(out.cursorCol() == 0);
}

TEST_CASE("Prompt.transient_marked_row_is_never_erased_whole")
{
    auto out = RecordingOutput(80, 40);

    auto const lines = detail::writeTransientLines(out, TransientMode::Minimal, "echo hi");

    REQUIRE(lines == 1);
    // The marked row is written, then cleared from where the content ends; no full-line clear.
    CHECK(indexOf(out.events, "clearLine") == out.events.size());
    REQUIRE(!out.events.empty());
    CHECK(out.events.front().starts_with("write:"));
    CHECK(indexOf(out.events, "clearToEndOfLine") > 0);
    CHECK(out.cursorCol() > 0);
}

TEST_CASE("Prompt.transient_marked_row_with_arrow")
{
    auto out = RecordingOutput(80, 40);

    auto const lines = detail::writeTransientLines(out, TransientMode::Arrow, "ls");

    REQUIRE(lines == 1);
    CHECK(indexOf(out.events, "clearLine") == out.events.size());
    CHECK(out.events.front().starts_with("write:"));
}

TEST_CASE("Prompt.transient_marked_row_with_empty_input")
{
    auto out = RecordingOutput(80, 40);

    auto const lines = detail::writeTransientLines(out, TransientMode::Minimal, "");

    CHECK(lines == 0);
    // A space is written first so the clear-to-end-of-line starts past column 1.
    REQUIRE(out.events.size() == 2);
    CHECK(out.events[0] == "write: ");
    CHECK(out.events[1] == "clearToEndOfLine");
    CHECK(out.cursorCol() == 1);
}

TEST_CASE("Prompt.transient_continuation_rows_are_cleared_before_writing")
{
    auto out = RecordingOutput(80, 40);

    auto const lines = detail::writeTransientLines(out, TransientMode::Minimal, "a\nb");

    REQUIRE(lines == 2);
    // The mark-less continuation row may be erased whole, but only after the first row is done.
    CHECK(indexOf(out.events, "clearToEndOfLine") < indexOf(out.events, "moveDown"));
    CHECK(indexOf(out.events, "moveDown") < indexOf(out.events, "clearLine"));
}

TEST_CASE("Prompt.transient_collapse_never_erases_the_marked_row")
{
    auto const geometry = detail::TransientGeometry {
        .topPad = 0, .aurora = 1, .chrome = 2, .inputLines = 1, .cursorLine = 0, .botPad = 0
    };
    auto out = RecordingOutput(80, 40);
    placeCursorOnInputRow(out, geometry);

    detail::collapseForTransient(out, geometry);

    CHECK(indexOf(out.events, "clearLine") == out.events.size());
}
