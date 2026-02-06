// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <tui/Screen.hpp>

using namespace tui;

// ============================================================================
// InlineCursorMovement calculation tests
// ============================================================================

TEST_CASE("Screen.inlineCursor_initialRender")
{
    // First render with no previous content
    auto movement = calculateInlineCursorMovement(
        /*previousContentHeight=*/0,
        /*previousCursorRow=*/0,
        /*newContentHeight=*/1);

    CHECK(movement.moveUpToStart == 0);
    CHECK(movement.newLinesToEmit == 1);      // Need 1 line for content
    CHECK(movement.moveUpAfterNewlines == 1); // Move back up by 1
    CHECK(movement.rowsToClear == 0);
}

TEST_CASE("Screen.inlineCursor_contentGrows")
{
    // Content grows from 1 row (prompt) to 8 rows (prompt + popup)
    auto movement = calculateInlineCursorMovement(
        /*previousContentHeight=*/1,
        /*previousCursorRow=*/0,
        /*newContentHeight=*/8);

    CHECK(movement.moveUpToStart == 0);       // Already at row 0
    CHECK(movement.newLinesToEmit == 7);      // 8 - 1 = 7 new lines needed
    CHECK(movement.moveUpAfterNewlines == 7); // Move up by newLines, NOT contentHeight!
    CHECK(movement.rowsToClear == 0);         // No shrink
}

TEST_CASE("Screen.inlineCursor_contentShrinks")
{
    // Content shrinks from 8 rows (prompt + popup) to 1 row (just prompt)
    auto movement = calculateInlineCursorMovement(
        /*previousContentHeight=*/8,
        /*previousCursorRow=*/0,
        /*newContentHeight=*/1);

    CHECK(movement.moveUpToStart == 0);
    CHECK(movement.newLinesToEmit == 0); // No growth
    CHECK(movement.moveUpAfterNewlines == 0);
    CHECK(movement.rowsToClear == 7); // 8 - 1 = 7 rows to clear
}

TEST_CASE("Screen.inlineCursor_noChange")
{
    // Content height stays the same
    auto movement = calculateInlineCursorMovement(
        /*previousContentHeight=*/3,
        /*previousCursorRow=*/0,
        /*newContentHeight=*/3);

    CHECK(movement.moveUpToStart == 0);
    CHECK(movement.newLinesToEmit == 0);
    CHECK(movement.moveUpAfterNewlines == 0);
    CHECK(movement.rowsToClear == 0);
}

TEST_CASE("Screen.inlineCursor_cursorNotAtRowZero")
{
    // Cursor was left at row 2 (e.g., in a multi-line input)
    auto movement = calculateInlineCursorMovement(
        /*previousContentHeight=*/3,
        /*previousCursorRow=*/2,
        /*newContentHeight=*/3);

    CHECK(movement.moveUpToStart == 2); // Move from row 2 to row 0
    CHECK(movement.newLinesToEmit == 0);
    CHECK(movement.moveUpAfterNewlines == 0);
    CHECK(movement.rowsToClear == 0);
}

TEST_CASE("Screen.inlineCursor_growWithCursorOffset")
{
    // Content grows while cursor is not at row 0
    auto movement = calculateInlineCursorMovement(
        /*previousContentHeight=*/2,
        /*previousCursorRow=*/1,
        /*newContentHeight=*/5);

    CHECK(movement.moveUpToStart == 1);       // Move from row 1 to row 0
    CHECK(movement.newLinesToEmit == 3);      // 5 - 2 = 3 new lines
    CHECK(movement.moveUpAfterNewlines == 3); // Move up by newLines
    CHECK(movement.rowsToClear == 0);
}

TEST_CASE("Screen.inlineCursor_shrinkWithCursorOffset")
{
    // Content shrinks while cursor is not at row 0
    auto movement = calculateInlineCursorMovement(
        /*previousContentHeight=*/5,
        /*previousCursorRow=*/2,
        /*newContentHeight=*/2);

    CHECK(movement.moveUpToStart == 2); // Move from row 2 to row 0
    CHECK(movement.newLinesToEmit == 0);
    CHECK(movement.moveUpAfterNewlines == 0);
    CHECK(movement.rowsToClear == 3); // 5 - 2 = 3 rows to clear
}

TEST_CASE("Screen.inlineCursor_growShrinkCycle_noDrift")
{
    // Simulate the Tab press cycle that was causing cursor drift:
    // Initial (1 row) -> Grow (8 rows) -> Shrink (1 row) -> Grow (8 rows)
    // Each transition should be self-consistent with no cumulative drift

    int prevHeight = 0, prevCursor = 0;

    // Initial render
    auto m0 = calculateInlineCursorMovement(prevHeight, prevCursor, 1);
    CHECK(m0.newLinesToEmit == 1);
    CHECK(m0.moveUpAfterNewlines == 1);
    prevHeight = 1;
    prevCursor = 0;

    // Grow to 8 (popup shown)
    auto m1 = calculateInlineCursorMovement(prevHeight, prevCursor, 8);
    CHECK(m1.newLinesToEmit == 7);
    CHECK(m1.moveUpAfterNewlines == 7); // Critical: NOT 8
    prevHeight = 8;
    prevCursor = 0;

    // Shrink to 1 (popup hidden)
    auto m2 = calculateInlineCursorMovement(prevHeight, prevCursor, 1);
    CHECK(m2.newLinesToEmit == 0);
    CHECK(m2.rowsToClear == 7);
    prevHeight = 1;
    prevCursor = 0;

    // Grow again to 8 (popup shown again)
    auto m3 = calculateInlineCursorMovement(prevHeight, prevCursor, 8);
    CHECK(m3.newLinesToEmit == 7);
    CHECK(m3.moveUpAfterNewlines == 7); // Still NOT 8 - no drift!
    prevHeight = 8;
    prevCursor = 0;

    // Shrink again to 1
    auto m4 = calculateInlineCursorMovement(prevHeight, prevCursor, 1);
    CHECK(m4.rowsToClear == 7);

    // The key invariant: each grow emits N newlines and moves up N (not N+previousHeight)
    // This ensures no cumulative cursor drift across cycles
}

TEST_CASE("Screen.inlineCursor_shrinkToZero")
{
    // Edge case: content shrinks to 0 (all content removed)
    auto movement = calculateInlineCursorMovement(
        /*previousContentHeight=*/3,
        /*previousCursorRow=*/0,
        /*newContentHeight=*/0);

    CHECK(movement.moveUpToStart == 0);
    CHECK(movement.newLinesToEmit == 0);
    CHECK(movement.moveUpAfterNewlines == 0);
    CHECK(movement.rowsToClear == 0); // Don't clear when new content is 0
}

TEST_CASE("Screen.inlineCursor_largeGrowth")
{
    // Large content growth
    auto movement = calculateInlineCursorMovement(
        /*previousContentHeight=*/1,
        /*previousCursorRow=*/0,
        /*newContentHeight=*/100);

    CHECK(movement.newLinesToEmit == 99);
    CHECK(movement.moveUpAfterNewlines == 99); // NOT 100
    CHECK(movement.rowsToClear == 0);
}

// ============================================================================
// UnscrollMode configuration tests
// ============================================================================

TEST_CASE("Screen.unscrollMode_enumValues")
{
    // Verify UnscrollMode enum has expected values
    CHECK(static_cast<int>(UnscrollMode::Auto) == 0);
    CHECK(static_cast<int>(UnscrollMode::Enabled) == 1);
    CHECK(static_cast<int>(UnscrollMode::Disabled) == 2);
}

TEST_CASE("Screen.screenConfig_defaultUnscrollMode")
{
    // Verify default unscroll mode is Auto
    ScreenConfig config;
    CHECK(config.unscrollMode == UnscrollMode::Auto);
}

TEST_CASE("Screen.screenConfig_customUnscrollMode")
{
    // Verify unscroll mode can be set via ScreenConfig
    ScreenConfig configEnabled;
    configEnabled.unscrollMode = UnscrollMode::Enabled;
    CHECK(configEnabled.unscrollMode == UnscrollMode::Enabled);

    ScreenConfig configDisabled;
    configDisabled.unscrollMode = UnscrollMode::Disabled;
    CHECK(configDisabled.unscrollMode == UnscrollMode::Disabled);
}
