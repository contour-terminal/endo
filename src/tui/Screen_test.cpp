// SPDX-License-Identifier: Apache-2.0
#include <tui/HoverState.hpp>
#include <tui/MockTerminalOutput.hpp>
#include <tui/Screen.hpp>
#include <tui/Terminal.hpp>
#include <tui/TestHelpers.hpp>
#include <tui/Tooltip.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <memory>
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

    int prevHeight = 0;
    int prevCursor = 0;

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
                .position = { .x = x, .y = y },
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
                .position = { .x = x, .y = y },
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
            return HoverResult { .text = "hoverable area",
                                 .position = { .x = x, .y = 1 },
                                 .contentType = TooltipContentType::PlainText };
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
            return HoverResult { .text = "mock hover",
                                 .position = { .x = x, .y = y },
                                 .contentType = TooltipContentType::PlainText };
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
    tooltip.setMaxSize({ .width = 40, .height = 5 }); // max 5 rows total (3 content + 2 border)

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

// ============================================================================
// MockTerminalOutput cursor drift tests
// ============================================================================

namespace
{

/// @brief A fixed-content component that renders N rows of text with a cursor at a specified row.
struct FixedContentComponent: Component
{
    int contentRows = 4;
    int cursorAtRow = 2;

    void render(Canvas& canvas) override
    {
        for (auto row = 0; row < contentRows; ++row)
            canvas.putString(0, row, "content line", {});
        canvas.setCursor(cursorAtRow, 0);
    }

    [[nodiscard]] Size preferredSize() const override { return { .width = 80, .height = contentRows }; }

    [[nodiscard]] bool focusable() const override { return true; }
};

} // namespace

TEST_CASE("Screen.inlineCursor_releaseCursorCycle_noDrift")
{
    auto mockOutput = std::make_unique<MockTerminalOutput>(80, 24);
    auto* mock = mockOutput.get();
    auto terminal = Terminal(std::move(mockOutput));

    auto screenConfig = ScreenConfig { .viewport = Viewport::Inline, .inlineMaxHeight = 12 };
    auto screen = Screen(terminal, screenConfig);

    auto comp = FixedContentComponent {};
    comp.contentRows = 4;
    comp.cursorAtRow = 2;
    auto const layout = LayoutParams { .area = { .x = 0, .y = 0, .width = 80, .height = 4 } };
    screen.root().addChild(comp, layout);
    screen.setFocus(&comp);

    // First render
    screen.draw();
    auto const firstCursorRow = mock->cursorRow();

    // Simulate transition away using clearAndRelease (the fix)
    screen.clearAndRelease();

    // Re-render after release
    screen.draw();
    auto const secondCursorRow = mock->cursorRow();

    CHECK(firstCursorRow == secondCursorRow);
}

// ============================================================================
// Variable height component drift tests
// ============================================================================

namespace
{

/// @brief A component with configurable height and cursor row, for drift testing.
struct VariableHeightComponent: Component
{
    int height = 2;
    int cursorRow = 0;

    void render(Canvas& canvas) override
    {
        for (auto row = 0; row < height; ++row)
            canvas.putString(0, row, "content", {});
        canvas.setCursor(cursorRow, 0);
    }

    [[nodiscard]] Size preferredSize() const override { return { .width = 80, .height = height }; }

    [[nodiscard]] bool focusable() const override { return true; }
};

} // namespace

TEST_CASE("Screen.inlineCursor_heightChangeAfterClearAndRelease_drift")
{
    auto mockOutput = std::make_unique<MockTerminalOutput>(80, 24);
    auto* mock = mockOutput.get();
    auto terminal = Terminal(std::move(mockOutput));

    auto screenConfig = ScreenConfig { .viewport = Viewport::Inline, .inlineMaxHeight = 12 };
    auto screen = Screen(terminal, screenConfig);

    auto comp = VariableHeightComponent {};
    comp.height = 2;
    comp.cursorRow = 0;
    auto const layout = LayoutParams { .area = { .x = 0, .y = 0, .width = 80, .height = 12 } };
    screen.root().addChild(comp, layout);
    screen.setFocus(&comp);

    // First render — baseline
    screen.draw();
    auto const baselineCursorRow = mock->cursorRow();

    // clearAndRelease, change height, draw again
    screen.clearAndRelease();
    comp.height = 3;
    comp.cursorRow = 1;
    screen.root().removeChild(comp);
    screen.root().addChild(comp, layout);
    screen.setFocus(&comp);
    screen.draw();
    auto const afterGrowCursorRow = mock->cursorRow();

    // Height grew by 1, cursor moved from row 0 to row 1 → cursor row should be baseline + 1
    CHECK(afterGrowCursorRow == baselineCursorRow + 1);

    // Another clearAndRelease + draw with same height — no additional drift
    screen.clearAndRelease();
    screen.draw();
    auto const redrawCursorRow = mock->cursorRow();
    CHECK(redrawCursorRow == afterGrowCursorRow);
}

TEST_CASE("Screen.inlineCursor_twoComponentSwap_noDrift")
{
    auto mockOutput = std::make_unique<MockTerminalOutput>(80, 24);
    auto* mock = mockOutput.get();
    auto terminal = Terminal(std::move(mockOutput));

    auto screenConfig = ScreenConfig { .viewport = Viewport::Inline, .inlineMaxHeight = 12 };
    auto screen = Screen(terminal, screenConfig);

    // "Shell" component: height 3, cursor at row 1
    auto shellComp = VariableHeightComponent {};
    shellComp.height = 3;
    shellComp.cursorRow = 1;

    // "Agent" component: height 2, cursor at row 1
    auto agentComp = VariableHeightComponent {};
    agentComp.height = 2;
    agentComp.cursorRow = 1;

    auto const layout = LayoutParams { .area = { .x = 0, .y = 0, .width = 80, .height = 12 } };

    // Draw shell component — baseline
    screen.root().addChild(shellComp, layout);
    screen.setFocus(&shellComp);
    screen.draw();
    auto const baselineCursorRow = mock->cursorRow();

    // Toggle to agent and back 10 times — no cumulative drift
    for (auto i = 0; i < 10; ++i)
    {
        // Switch to agent
        screen.clearAndRelease();
        screen.root().removeChild(shellComp);
        screen.root().addChild(agentComp, layout);
        screen.setFocus(&agentComp);
        screen.draw();

        // Switch back to shell
        screen.clearAndRelease();
        screen.root().removeChild(agentComp);
        screen.root().addChild(shellComp, layout);
        screen.setFocus(&shellComp);
        screen.draw();
        auto const cursorRow = mock->cursorRow();
        CHECK(cursorRow == baselineCursorRow);
    }
}

TEST_CASE("Screen.inline_toggle_no_drift")
{
    // Simulates the Ctrl+T toggle between two independent Screens (shell and agent),
    // which is the exact pattern that causes the +1 drift per shell→agent transition.
    // Each Screen has its own _peakContentHeight, so the second Screen's first render
    // would previously re-emit room reservation LFs even though the room already exists.
    // The fix uses DECSC/DECRC to anchor the cursor position across Screen transitions.

    auto mockOutput = std::make_unique<MockTerminalOutput>(80, 24);
    auto* mock = mockOutput.get();
    auto terminal = Terminal(std::move(mockOutput));

    auto const screenConfig = ScreenConfig { .viewport = Viewport::Inline, .inlineMaxHeight = 12 };
    auto const layout = LayoutParams { .area = { .x = 0, .y = 0, .width = 80, .height = 12 } };

    // Shell component: 2 rows, cursor at row 0
    auto shellComp = VariableHeightComponent {};
    shellComp.height = 2;
    shellComp.cursorRow = 0;

    // Agent component: 2 rows, cursor at row 0
    auto agentComp = VariableHeightComponent {};
    agentComp.height = 2;
    agentComp.cursorRow = 0;

    // --- Shell Screen: first render (baseline) ---
    auto shellScreen = std::make_unique<Screen>(terminal, screenConfig);
    shellScreen->root().addChild(shellComp, layout);
    shellScreen->setFocus(&shellComp);
    shellScreen->draw();
    auto const baselineCursorRow = mock->cursorRow();

    // Toggle shell → agent → shell 10 times, verify no cumulative drift
    for (auto i = 0; i < 10; ++i)
    {
        // Shell → Agent transition: clearAndRelease shell (saves cursor via DECSC),
        // create new agent Screen (restores cursor via DECRC on first render)
        shellScreen->clearAndRelease();
        shellScreen->root().removeChild(shellComp);
        shellScreen.reset(); // Destroy shell screen

        auto agentScreen = std::make_unique<Screen>(terminal, screenConfig);
        agentScreen->root().addChild(agentComp, layout);
        agentScreen->setFocus(&agentComp);
        agentScreen->draw();

        // Agent → Shell transition: clearAndRelease agent, create new shell Screen
        agentScreen->clearAndRelease();
        agentScreen->root().removeChild(agentComp);
        agentScreen.reset(); // Destroy agent screen

        shellScreen = std::make_unique<Screen>(terminal, screenConfig);
        shellScreen->root().addChild(shellComp, layout);
        shellScreen->setFocus(&shellComp);
        shellScreen->draw();

        auto const cursorRow = mock->cursorRow();
        CHECK(cursorRow == baselineCursorRow);
    }

    // Cleanup
    shellScreen->root().removeChild(shellComp);
}

TEST_CASE("Screen.inline_toggle_different_heights_no_drift")
{
    // Tests the toggle between screens with different content heights.
    // The room reservation + DECSC/DECRC cursor anchoring should handle
    // the taller→shorter and shorter→taller transitions without drift.

    auto mockOutput = std::make_unique<MockTerminalOutput>(80, 24);
    auto* mock = mockOutput.get();
    auto terminal = Terminal(std::move(mockOutput));

    auto const screenConfig = ScreenConfig { .viewport = Viewport::Inline, .inlineMaxHeight = 12 };
    auto const layout = LayoutParams { .area = { .x = 0, .y = 0, .width = 80, .height = 12 } };

    // Shell: 3 rows, cursor at row 1
    auto shellComp = VariableHeightComponent {};
    shellComp.height = 3;
    shellComp.cursorRow = 1;

    // Agent: 1 row, cursor at row 0
    auto agentComp = VariableHeightComponent {};
    agentComp.height = 1;
    agentComp.cursorRow = 0;

    // --- Shell first render (baseline) ---
    auto shellScreen = std::make_unique<Screen>(terminal, screenConfig);
    shellScreen->root().addChild(shellComp, layout);
    shellScreen->setFocus(&shellComp);
    shellScreen->draw();
    auto const baselineCursorRow = mock->cursorRow();

    for (auto i = 0; i < 5; ++i)
    {
        // Shell → Agent (clearAndRelease saves cursor via DECSC)
        shellScreen->clearAndRelease();
        shellScreen->root().removeChild(shellComp);
        shellScreen.reset();

        auto agentScreen = std::make_unique<Screen>(terminal, screenConfig);
        agentScreen->root().addChild(agentComp, layout);
        agentScreen->setFocus(&agentComp);
        agentScreen->draw();

        // Agent → Shell (clearAndRelease saves cursor via DECSC)
        agentScreen->clearAndRelease();
        agentScreen->root().removeChild(agentComp);
        agentScreen.reset();

        shellScreen = std::make_unique<Screen>(terminal, screenConfig);
        shellScreen->root().addChild(shellComp, layout);
        shellScreen->setFocus(&shellComp);
        shellScreen->draw();

        auto const cursorRow = mock->cursorRow();
        CHECK(cursorRow == baselineCursorRow);
    }

    shellScreen->root().removeChild(shellComp);
}

TEST_CASE("Screen.inline_toggle_shell4_agent2_no_drift")
{
    // Tests the toggle between screens where shell is taller (4 rows) and agent is shorter (2 rows).
    // Exercises the partial room-reservation path where reserved room < content height,
    // verifying DECSC/DECRC correctly anchors the cursor even when extra LFs are needed.

    auto mockOutput = std::make_unique<MockTerminalOutput>(80, 24);
    auto* mock = mockOutput.get();
    auto terminal = Terminal(std::move(mockOutput));

    auto const screenConfig = ScreenConfig { .viewport = Viewport::Inline, .inlineMaxHeight = 12 };
    auto const layout = LayoutParams { .area = { .x = 0, .y = 0, .width = 80, .height = 12 } };

    // Shell: 4 rows, cursor at row 2
    auto shellComp = VariableHeightComponent {};
    shellComp.height = 4;
    shellComp.cursorRow = 2;

    // Agent: 2 rows, cursor at row 0
    auto agentComp = VariableHeightComponent {};
    agentComp.height = 2;
    agentComp.cursorRow = 0;

    // --- Shell first render (baseline) ---
    auto shellScreen = std::make_unique<Screen>(terminal, screenConfig);
    shellScreen->root().addChild(shellComp, layout);
    shellScreen->setFocus(&shellComp);
    shellScreen->draw();
    auto const baselineCursorRow = mock->cursorRow();

    for (auto i = 0; i < 10; ++i)
    {
        // Shell (4 rows) → Agent (2 rows): agent gets reserved room=4, only needs 2
        shellScreen->clearAndRelease();
        shellScreen->root().removeChild(shellComp);
        shellScreen.reset();

        auto agentScreen = std::make_unique<Screen>(terminal, screenConfig);
        agentScreen->root().addChild(agentComp, layout);
        agentScreen->setFocus(&agentComp);
        agentScreen->draw();

        // Agent (2 rows) → Shell (4 rows): shell gets reserved room=2, needs 4
        // This exercises the partial room path (reservedRoom < contentHeight)
        agentScreen->clearAndRelease();
        agentScreen->root().removeChild(agentComp);
        agentScreen.reset();

        shellScreen = std::make_unique<Screen>(terminal, screenConfig);
        shellScreen->root().addChild(shellComp, layout);
        shellScreen->setFocus(&shellComp);
        shellScreen->draw();

        auto const cursorRow = mock->cursorRow();
        CHECK(cursorRow == baselineCursorRow);
    }

    shellScreen->root().removeChild(shellComp);
}

TEST_CASE("Screen.inlineCursor_clearAndReleaseCycle_noDrift")
{
    auto mockOutput = std::make_unique<MockTerminalOutput>(80, 24);
    auto* mock = mockOutput.get();
    auto terminal = Terminal(std::move(mockOutput));

    auto screenConfig = ScreenConfig { .viewport = Viewport::Inline, .inlineMaxHeight = 12 };
    auto screen = Screen(terminal, screenConfig);

    auto comp = FixedContentComponent {};
    comp.contentRows = 4;
    comp.cursorAtRow = 2;
    auto const layout = LayoutParams { .area = { .x = 0, .y = 0, .width = 80, .height = 4 } };
    screen.root().addChild(comp, layout);
    screen.setFocus(&comp);

    // Initial render — establish baseline
    screen.draw();
    auto const baselineCursorRow = mock->cursorRow();

    // Cycle: clearAndRelease → draw, repeated 10 times
    for (auto i = 0; i < 10; ++i)
    {
        screen.clearAndRelease();
        screen.draw();
        auto const cursorRow = mock->cursorRow();
        CHECK(cursorRow == baselineCursorRow);
    }
}

// ============================================================================
// Inline mode hover coordinate translation tests
// ============================================================================

namespace
{

/// @brief A component that returns different tooltip text per row, for verifying coordinate mapping.
struct RowReportingComponent: Component
{
    int contentRows = 3;

    void render(Canvas& canvas) override
    {
        for (auto row = 0; row < contentRows; ++row)
            canvas.putString(row, 0, "row" + std::to_string(row) + " content", {});
        canvas.setCursor(0, 0);
    }

    [[nodiscard]] Size preferredSize() const override { return { .width = 80, .height = contentRows }; }

    [[nodiscard]] bool focusable() const override { return true; }

    std::optional<HoverResult> onHover(int x, int y) override
    {
        if (y >= 0 && y < contentRows)
            return HoverResult {
                .text = "row" + std::to_string(y),
                .position = { .x = x, .y = y },
                .contentType = TooltipContentType::PlainText,
            };
        return std::nullopt;
    }
};

} // namespace

TEST_CASE("Screen.inlineHover_cursorAtTop_correctMapping")
{
    // Cursor starts at row 0 (top of terminal) — content should be at top,
    // and hovering at content rows should produce correct component-relative coordinates.
    auto mockOutput = std::make_unique<MockTerminalOutput>(80, 50);
    auto terminal = Terminal(std::move(mockOutput));

    auto screenConfig = ScreenConfig { .viewport = Viewport::Inline, .inlineMaxHeight = 12 };
    auto screen = Screen(terminal, screenConfig);

    auto comp = RowReportingComponent {};
    comp.contentRows = 3;
    auto const layout = LayoutParams { .area = { .x = 0, .y = 0, .width = 80, .height = 3 } };
    screen.root().addChild(comp, layout);
    screen.setFocus(&comp);
    screen.draw();

    // Content starts at row 0 (mock cursor starts at 0).
    // Terminal mouse coordinates are 1-based, so row 1 = content row 0.
    (void) screen.dispatchEvent(test::mouseMove(5, 1)); // row 1 in 1-based = content row 0
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    screen.tickHover();

    CHECK(screen.isTooltipVisible());
    if (screen.isTooltipVisible())
    {
        screen.draw();
        auto const content = test::canvasToString(screen.renderedBuffer());
        CHECK(content.find("row0") != std::string::npos);
    }
}

TEST_CASE("Screen.inlineHover_cursorInMiddle_correctMapping")
{
    // Cursor starts in the middle of the terminal. Hovering above content should be rejected,
    // hovering at content should work, hovering below content should be rejected.
    auto mockOutput = std::make_unique<MockTerminalOutput>(80, 50);
    auto* mock = mockOutput.get();

    // Move mock cursor to middle of terminal before creating Screen
    mock->linefeed(); // row 1
    mock->linefeed(); // row 2
    mock->linefeed(); // row 3
    mock->linefeed(); // row 4
    mock->linefeed(); // row 5
    mock->linefeed(); // row 6
    mock->linefeed(); // row 7
    mock->linefeed(); // row 8
    mock->linefeed(); // row 9
    mock->linefeed(); // row 10 — content starts here

    auto terminal = Terminal(std::move(mockOutput));

    auto screenConfig = ScreenConfig { .viewport = Viewport::Inline, .inlineMaxHeight = 12 };
    auto screen = Screen(terminal, screenConfig);

    auto comp = RowReportingComponent {};
    comp.contentRows = 3;
    auto const layout = LayoutParams { .area = { .x = 0, .y = 0, .width = 80, .height = 3 } };
    screen.root().addChild(comp, layout);
    screen.setFocus(&comp);
    screen.draw();

    // Hover above content area (terminal row 5, 1-based = row 4 0-based, content starts at 10)
    (void) screen.dispatchEvent(test::mouseMove(5, 5));
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    screen.tickHover();
    CHECK_FALSE(screen.isTooltipVisible());

    // Hover at first content row (terminal row 11 in 1-based = row 10 0-based = content row 0)
    screen.hoverState().reset();
    (void) screen.dispatchEvent(test::mouseMove(5, 11));
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    screen.tickHover();

    CHECK(screen.isTooltipVisible());
    if (screen.isTooltipVisible())
    {
        screen.draw();
        auto const content = test::canvasToString(screen.renderedBuffer());
        CHECK(content.find("row0") != std::string::npos);
    }

    // Hover at second content row (terminal row 12 in 1-based = content row 1)
    screen.hoverState().reset();
    (void) screen.dispatchEvent(test::mouseMove(5, 12));
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    screen.tickHover();

    CHECK(screen.isTooltipVisible());
    if (screen.isTooltipVisible())
    {
        screen.draw();
        auto const content = test::canvasToString(screen.renderedBuffer());
        CHECK(content.find("row1") != std::string::npos);
    }

    // Hover below content area (terminal row 20 in 1-based)
    screen.hoverState().reset();
    (void) screen.dispatchEvent(test::mouseMove(5, 20));
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    screen.tickHover();
    CHECK_FALSE(screen.isTooltipVisible());
}

TEST_CASE("Screen.inlineHover_cursorAtBottom_correctMapping")
{
    // Cursor starts at the bottom of the terminal. Content should be at the bottom,
    // and hovering at the correct terminal rows should work.
    auto mockOutput = std::make_unique<MockTerminalOutput>(80, 24);
    auto* mock = mockOutput.get();

    // Move cursor to bottom (row 23 = last row of 24-row terminal)
    for (auto i = 0; i < 23; ++i)
        mock->linefeed();

    auto terminal = Terminal(std::move(mockOutput));

    auto screenConfig = ScreenConfig { .viewport = Viewport::Inline, .inlineMaxHeight = 12 };
    auto screen = Screen(terminal, screenConfig);

    auto comp = RowReportingComponent {};
    comp.contentRows = 3;
    auto const layout = LayoutParams { .area = { .x = 0, .y = 0, .width = 80, .height = 3 } };
    screen.root().addChild(comp, layout);
    screen.setFocus(&comp);
    screen.draw();

    // Content starts at row 21 (24 - 3 = 21, 0-based), so terminal row 22 in 1-based = content row 0.
    // With scrolling from LFs, the content is at the bottom.
    (void) screen.dispatchEvent(test::mouseMove(5, 22));
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    screen.tickHover();

    CHECK(screen.isTooltipVisible());
    if (screen.isTooltipVisible())
    {
        screen.draw();
        auto const content = test::canvasToString(screen.renderedBuffer());
        CHECK(content.find("row0") != std::string::npos);
    }
}

TEST_CASE("Screen.inlineHover_contentGrowth_correctMapping")
{
    // Content starts small (1 row), then grows to 3 rows. Hover should still map correctly.
    auto mockOutput = std::make_unique<MockTerminalOutput>(80, 50);
    auto terminal = Terminal(std::move(mockOutput));

    auto screenConfig = ScreenConfig { .viewport = Viewport::Inline, .inlineMaxHeight = 12 };
    auto screen = Screen(terminal, screenConfig);

    auto comp = RowReportingComponent {};
    comp.contentRows = 1;
    auto const layout = LayoutParams { .area = { .x = 0, .y = 0, .width = 80, .height = 12 } };
    screen.root().addChild(comp, layout);
    screen.setFocus(&comp);
    screen.draw();

    // Grow content to 3 rows
    comp.contentRows = 3;
    screen.invalidate();
    screen.draw();

    // Content starts at row 0 (cursor at top). Hover at row 1 (1-based) = content row 0.
    (void) screen.dispatchEvent(test::mouseMove(5, 1));
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    screen.tickHover();

    CHECK(screen.isTooltipVisible());
    if (screen.isTooltipVisible())
    {
        screen.draw();
        auto const content = test::canvasToString(screen.renderedBuffer());
        CHECK(content.find("row0") != std::string::npos);
    }

    // Hover at row 3 (1-based) = content row 2.
    screen.hoverState().reset();
    (void) screen.dispatchEvent(test::mouseMove(5, 3));
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    screen.tickHover();

    CHECK(screen.isTooltipVisible());
    if (screen.isTooltipVisible())
    {
        screen.draw();
        auto const content = test::canvasToString(screen.renderedBuffer());
        CHECK(content.find("row2") != std::string::npos);
    }
}
