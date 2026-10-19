// SPDX-License-Identifier: Apache-2.0
#include <shell/ui/Prompt.hpp>

#include <core/tui/MockTerminalOutput.hpp>

#include <catch2/catch_test_macros.hpp>

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
