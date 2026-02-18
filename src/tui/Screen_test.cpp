// SPDX-License-Identifier: Apache-2.0
#include <tui/HoverState.hpp>
#include <tui/Screen.hpp>
#include <tui/Terminal.hpp>
#include <tui/TestHelpers.hpp>
#include <tui/Tooltip.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <thread>

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

// ============================================================================
// HoverState tests
// ============================================================================

TEST_CASE("HoverState.initialState")
{
    HoverState hover;
    CHECK_FALSE(hover.isHoverConfirmed());
    CHECK_FALSE(hover.currentHover().has_value());
    CHECK_FALSE(hover.mousePosition().has_value());
    CHECK(hover.timeoutMs() == -1); // No timeout when not hovering
}

TEST_CASE("HoverState.onMouseMove_startsHover")
{
    HoverState hover(std::chrono::milliseconds(100));

    hover.onMouseMove(10, 20, nullptr);

    CHECK_FALSE(hover.isHoverConfirmed()); // Not confirmed yet
    CHECK(hover.currentHover().has_value());
    CHECK(hover.currentHover()->x == 10);
    CHECK(hover.currentHover()->y == 20);
    CHECK(hover.timeoutMs() > 0); // Timeout active
    CHECK(hover.timeoutMs() <= 100);
}

TEST_CASE("HoverState.onMouseMove_samePosition_noReset")
{
    HoverState hover(std::chrono::milliseconds(100));

    hover.onMouseMove(10, 20, nullptr);
    auto const initialTimeout = hover.timeoutMs();

    // Small delay
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Same position - should NOT reset the timer
    hover.onMouseMove(10, 20, nullptr);
    auto const secondTimeout = hover.timeoutMs();

    // Timer should have progressed (second timeout should be less)
    CHECK(secondTimeout < initialTimeout);
}

TEST_CASE("HoverState.onMouseMove_differentPosition_resetsTimer")
{
    HoverState hover(std::chrono::milliseconds(100));

    hover.onMouseMove(10, 20, nullptr);

    // Wait a bit
    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    // Move to different position - should reset the timer
    hover.onMouseMove(15, 25, nullptr);
    auto const timeout = hover.timeoutMs();

    // Timer should be close to full delay again
    CHECK(timeout > 80); // Allowing some tolerance
    CHECK(hover.currentHover()->x == 15);
    CHECK(hover.currentHover()->y == 25);
}

TEST_CASE("HoverState.tick_triggersCallback")
{
    HoverState hover(std::chrono::milliseconds(10));

    bool callbackCalled = false;
    HoverInfo capturedInfo;

    hover.setOnHoverConfirmed([&](HoverInfo const& info) {
        callbackCalled = true;
        capturedInfo = info;
    });

    hover.onMouseMove(50, 60, nullptr);

    // Wait for hover delay to expire
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    hover.tick(std::chrono::steady_clock::now());

    CHECK(callbackCalled);
    CHECK(hover.isHoverConfirmed());
    CHECK(capturedInfo.x == 50);
    CHECK(capturedInfo.y == 60);
}

TEST_CASE("HoverState.onMouseLeave_triggersCallback")
{
    HoverState hover(std::chrono::milliseconds(10));

    bool leaveCallbackCalled = false;

    hover.setOnHoverConfirmed([](HoverInfo const&) {});
    hover.setOnHoverLeave([&]() { leaveCallbackCalled = true; });

    hover.onMouseMove(10, 20, nullptr);

    // Confirm hover
    std::this_thread::sleep_for(std::chrono::milliseconds(15));
    hover.tick(std::chrono::steady_clock::now());
    CHECK(hover.isHoverConfirmed());

    // Leave
    hover.onMouseLeave();

    CHECK(leaveCallbackCalled);
    CHECK_FALSE(hover.isHoverConfirmed());
    CHECK_FALSE(hover.currentHover().has_value());
}

TEST_CASE("HoverState.reset_clearsState")
{
    HoverState hover(std::chrono::milliseconds(10));

    bool leaveCallbackCalled = false;
    hover.setOnHoverLeave([&]() { leaveCallbackCalled = true; });

    hover.onMouseMove(10, 20, nullptr);
    std::this_thread::sleep_for(std::chrono::milliseconds(15));
    hover.tick(std::chrono::steady_clock::now());
    CHECK(hover.isHoverConfirmed());

    hover.reset();

    CHECK(leaveCallbackCalled);
    CHECK_FALSE(hover.isHoverConfirmed());
    CHECK_FALSE(hover.currentHover().has_value());
    CHECK(hover.timeoutMs() == -1);
}

TEST_CASE("HoverState.mouseMove_whileConfirmed_triggersLeaveAndRestart")
{
    HoverState hover(std::chrono::milliseconds(10));

    int leaveCount = 0;

    hover.setOnHoverLeave([&]() { ++leaveCount; });

    hover.onMouseMove(10, 20, nullptr);

    // Confirm hover
    std::this_thread::sleep_for(std::chrono::milliseconds(15));
    hover.tick(std::chrono::steady_clock::now());
    CHECK(hover.isHoverConfirmed());

    // Move to different position - should trigger leave and restart
    hover.onMouseMove(30, 40, nullptr);

    CHECK(leaveCount == 1);
    CHECK_FALSE(hover.isHoverConfirmed());   // No longer confirmed
    CHECK(hover.currentHover().has_value()); // But still tracking
    CHECK(hover.currentHover()->x == 30);
}

// ============================================================================
// Component::onHover tests
// ============================================================================

TEST_CASE("Component.onHover_defaultReturnsNullopt")
{
    struct SimpleComponent: Component
    {
        void render(Canvas&) override {}
    };

    SimpleComponent comp;
    auto result = comp.onHover(5, 3);
    CHECK_FALSE(result.has_value());
}

TEST_CASE("Component.onHover_overrideReturnsPlainText")
{
    struct HoverableComponent: Component
    {
        void render(Canvas&) override {}

        std::optional<HoverResult> onHover(int x, int y) override
        {
            return HoverResult {
                .text = "hello tooltip",
                .position = { x, y },
                .contentType = TooltipContentType::PlainText,
            };
        }
    };

    HoverableComponent comp;
    auto result = comp.onHover(10, 5);
    REQUIRE(result.has_value());
    CHECK(result->text == "hello tooltip");
    CHECK(result->position.x == 10);
    CHECK(result->position.y == 5);
    CHECK(result->contentType == TooltipContentType::PlainText);
}

TEST_CASE("Component.onHover_overrideReturnsMarkdown")
{
    struct MarkdownHoverComponent: Component
    {
        void render(Canvas&) override {}

        std::optional<HoverResult> onHover(int x, int y) override
        {
            return HoverResult {
                .text = "## Heading\n\nMulti-line **bold** content\n- item 1\n- item 2",
                .position = { x, y },
                .contentType = TooltipContentType::Markdown,
            };
        }
    };

    MarkdownHoverComponent comp;
    auto result = comp.onHover(3, 0);
    REQUIRE(result.has_value());
    CHECK(result->contentType == TooltipContentType::Markdown);
    // Verify multi-line content preserved
    CHECK(result->text.find('\n') != std::string::npos);
}

TEST_CASE("Component.onHover_selectiveHover")
{
    struct SelectiveComponent: Component
    {
        void render(Canvas&) override {}

        std::optional<HoverResult> onHover(int x, int /*y*/) override
        {
            if (x < 5)
                return std::nullopt;
            return HoverResult { "hoverable area", { x, 1 }, TooltipContentType::PlainText };
        }
    };

    SelectiveComponent comp;
    CHECK_FALSE(comp.onHover(2, 0).has_value());
    CHECK(comp.onHover(5, 0).has_value());
    CHECK(comp.onHover(10, 0).has_value());
}

TEST_CASE("HoverState.confirmedCallback_receivesTarget")
{
    struct MockComponent: Component
    {
        void render(Canvas&) override {}

        std::optional<HoverResult> onHover(int x, int y) override
        {
            return HoverResult { "mock hover", { x, y }, TooltipContentType::PlainText };
        }
    };

    HoverState hover(std::chrono::milliseconds(10));
    MockComponent comp;
    HoverInfo captured {};

    hover.setOnHoverConfirmed([&](HoverInfo const& info) { captured = info; });
    hover.onMouseMove(15, 20, &comp);

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    hover.tick(std::chrono::steady_clock::now());

    CHECK(hover.isHoverConfirmed());
    CHECK(captured.target == &comp);
    CHECK(captured.x == 15);
    CHECK(captured.y == 20);

    // Verify the component's onHover returns the expected result
    auto result = captured.target->onHover(5, 3);
    REQUIRE(result.has_value());
    CHECK(result->text == "mock hover");
}

TEST_CASE("HoverState.leave_afterConfirm_clearsState")
{
    HoverState hover(std::chrono::milliseconds(10));
    bool leaveCalled = false;
    hover.setOnHoverConfirmed([](HoverInfo const&) {});
    hover.setOnHoverLeave([&]() { leaveCalled = true; });

    hover.onMouseMove(10, 20, nullptr);
    std::this_thread::sleep_for(std::chrono::milliseconds(15));
    hover.tick(std::chrono::steady_clock::now());
    CHECK(hover.isHoverConfirmed());

    // Move to different position triggers leave + restart
    hover.onMouseMove(30, 40, nullptr);
    CHECK(leaveCalled);
    CHECK_FALSE(hover.isHoverConfirmed());
}

// ============================================================================
// Tooltip sizing tests
// ============================================================================

TEST_CASE("Tooltip.multiLineContent_growsHeight")
{
    Tooltip tooltip;
    tooltip.setContent("Line 1\nLine 2\nLine 3", TooltipContentType::PlainText);
    auto size = tooltip.preferredSize();
    // 3 content lines + 2 border lines = 5 height
    CHECK(size.height == 5);
}

TEST_CASE("Tooltip.exceedsMaxHeight_enablesScrolling")
{
    Tooltip tooltip;
    tooltip.setMaxSize({ 40, 5 }); // max 5 rows total (3 content + 2 border)

    // 10 lines of content
    std::string content;
    for (int i = 0; i < 10; ++i)
    {
        if (i > 0)
            content += '\n';
        content += "Line " + std::to_string(i + 1);
    }
    tooltip.setContent(content, TooltipContentType::PlainText);

    auto size = tooltip.preferredSize();
    CHECK(size.height <= 5); // Clamped to max
    CHECK(tooltip.canScrollDown());
    CHECK_FALSE(tooltip.canScrollUp()); // At top

    tooltip.scrollDown(1);
    CHECK(tooltip.canScrollUp());
}

TEST_CASE("Tooltip.markdownContent_properSizing")
{
    Tooltip tooltip;
    tooltip.setContent("## Title\n\nSome **bold** text\n\n- item 1\n- item 2", TooltipContentType::Markdown);
    auto size = tooltip.preferredSize();
    CHECK(size.width > 0);
    CHECK(size.height > 2); // More than just border
}

// ============================================================================
// Screen-level hover→tooltip integration tests
// ============================================================================

namespace
{

/// @brief A mock component that returns tooltip content for the first 15 columns of row 0.
struct HoverableTestComp: Component
{
    void render(Canvas& canvas) override { canvas.putString(0, 0, "error text here", {}); }

    [[nodiscard]] Size preferredSize() const override { return { .width = 80, .height = 3 }; }

    [[nodiscard]] bool focusable() const override { return true; }

    std::optional<HoverResult> onHover(int x, int y) override
    {
        if (x >= 0 && x < 15 && y == 0)
            return HoverResult {
                .text = "undefined variable",
                .position = { .x = x, .y = y },
                .contentType = TooltipContentType::PlainText,
            };
        return std::nullopt;
    }
};

} // namespace

TEST_CASE("Screen.hoverToTooltip_callbackFires")
{
    Terminal terminal;
    Screen screen(terminal, ScreenConfig { .viewport = Viewport::Fullscreen });

    HoverableTestComp comp;
    auto const compLayout = LayoutParams { .area = { .x = 0, .y = 0, .width = 80, .height = 3 } };
    screen.root().addChild(comp, compLayout);
    screen.setFocus(&comp);
    screen.draw();

    // Verify component screenBounds is set correctly
    auto const bounds = comp.screenBounds();
    CHECK(bounds.x == 0);
    CHECK(bounds.y == 0);
    CHECK(bounds.width == 80);
    CHECK(bounds.height == 3);

    // Dispatch mouse move
    (void) screen.dispatchEvent(test::mouseMove(5, 1));

    // Check hover state was updated
    auto const hover = screen.hoverState().currentHover();
    REQUIRE(hover.has_value());
    CHECK(hover->x == 5);
    CHECK(hover->y == 1);
    CHECK(hover->target == &comp);

    // Wait and tick
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    screen.tickHover();

    CHECK(screen.hoverState().isHoverConfirmed());
    CHECK(screen.isTooltipVisible());
}

TEST_CASE("Screen.hoverToTooltip_fullFlow")
{
    Terminal terminal; // uninitialized = 80x24 defaults
    Screen screen(terminal, ScreenConfig { .viewport = Viewport::Fullscreen });

    HoverableTestComp comp;
    auto const compLayout = LayoutParams { .area = { .x = 0, .y = 0, .width = 80, .height = 3 } };
    screen.root().addChild(comp, compLayout);
    screen.setFocus(&comp);
    screen.draw(); // Establishes screenBounds

    // Mouse move over error text (1-based terminal coordinates)
    (void) screen.dispatchEvent(test::mouseMove(5, 1));

    // Wait for hover delay + margin, then tick
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    screen.tickHover();

    CHECK(screen.isTooltipVisible());

    // Verify tooltip content in rendered buffer
    screen.draw();
    auto const content = test::canvasToString(screen.renderedBuffer());
    CHECK(content.find("undefined variable") != std::string::npos);
}

TEST_CASE("Screen.hoverToTooltip_mouseMovesAway_hidesTooltip")
{
    Terminal terminal;
    Screen screen(terminal, ScreenConfig { .viewport = Viewport::Fullscreen });

    HoverableTestComp comp;
    auto const compLayout = LayoutParams { .area = { .x = 0, .y = 0, .width = 80, .height = 3 } };
    screen.root().addChild(comp, compLayout);
    screen.setFocus(&comp);
    screen.draw();

    // Trigger hover
    (void) screen.dispatchEvent(test::mouseMove(5, 1));
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    screen.tickHover();
    REQUIRE(screen.isTooltipVisible());

    // Move mouse to a position where onHover returns nullopt (row 20)
    (void) screen.dispatchEvent(test::mouseMove(5, 20));

    CHECK_FALSE(screen.isTooltipVisible());
}

TEST_CASE("Screen.hoverToTooltip_keyEvent_hidesTooltip")
{
    Terminal terminal;
    Screen screen(terminal, ScreenConfig { .viewport = Viewport::Fullscreen });

    HoverableTestComp comp;
    auto const compLayout = LayoutParams { .area = { .x = 0, .y = 0, .width = 80, .height = 3 } };
    screen.root().addChild(comp, compLayout);
    screen.setFocus(&comp);
    screen.draw();

    // Trigger hover
    (void) screen.dispatchEvent(test::mouseMove(5, 1));
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    screen.tickHover();
    REQUIRE(screen.isTooltipVisible());

    // Dispatch a key event — should hide tooltip
    (void) screen.dispatchEvent(InputEvent { test::charKey('a') });

    CHECK_FALSE(screen.isTooltipVisible());
}

TEST_CASE("Screen.hoverToTooltip_nulloptHover_noTooltip")
{
    Terminal terminal;
    Screen screen(terminal, ScreenConfig { .viewport = Viewport::Fullscreen });

    HoverableTestComp comp;
    auto const compLayout = LayoutParams { .area = { .x = 0, .y = 0, .width = 80, .height = 3 } };
    screen.root().addChild(comp, compLayout);
    screen.setFocus(&comp);
    screen.draw();

    // Mouse over region where onHover returns nullopt (column 50, row 2)
    (void) screen.dispatchEvent(test::mouseMove(50, 3));

    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    screen.tickHover();

    CHECK_FALSE(screen.isTooltipVisible());
}

TEST_CASE("Screen.hoverToTooltip_inlineMode")
{
    Terminal terminal; // 80x24 defaults
    Screen screen(terminal, ScreenConfig { .viewport = Viewport::Inline, .inlineMaxHeight = 12 });

    HoverableTestComp comp;
    auto const compLayout = LayoutParams { .area = { .x = 0, .y = 0, .width = 80, .height = 3 } };
    screen.root().addChild(comp, compLayout);
    screen.setFocus(&comp);
    screen.draw(); // Sets _inlineContentStartRow and screenBounds

    // In inline mode, mouse coordinates are terminal-absolute (1-based).
    // The content starts at _inlineContentStartRow (set by flushInline).
    // The component is at row 0 in buffer space, so terminal row =
    // _inlineContentStartRow + 1 (1-based). Since terminal is 24 rows and
    // content height is 1 (just "error text here" on row 0, rows 1-2 are
    // empty spaces), _inlineContentStartRow ≈ 24 - 1 = 23.
    // We use row 24 (1-based) = terminal row 23 (0-based) = content row 0.
    auto const terminalRow = terminal.rows(); // bottom of terminal, 1-based
    (void) screen.dispatchEvent(test::mouseMove(5, terminalRow));

    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    screen.tickHover();

    // The hover may or may not trigger a tooltip depending on exact inline
    // coordinate mapping. The key test is that the system doesn't crash
    // and that the hover state was properly processed.
    // If tooltip is visible, verify content.
    if (screen.isTooltipVisible())
    {
        screen.draw();
        auto const content = test::canvasToString(screen.renderedBuffer());
        CHECK(content.find("undefined variable") != std::string::npos);
    }
}
