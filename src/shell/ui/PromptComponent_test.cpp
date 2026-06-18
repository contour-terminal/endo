// SPDX-License-Identifier: Apache-2.0
#include <tui/InputEvent.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string_view>
#include <thread>

#include "PromptComponent.hpp"

using namespace endo;

namespace
{

/// @brief Creates a Ctrl+D key event (EOF on empty input).
tui::InputEvent ctrlD()
{
    return tui::KeyEvent { .key = static_cast<tui::KeyCode>('d'),
                           .modifiers = tui::Modifier::Ctrl,
                           .codepoint = 'd' };
}

/// @brief Creates a regular character key event.
tui::InputEvent charEvent(char ch)
{
    return tui::KeyEvent { .key = static_cast<tui::KeyCode>(ch),
                           .modifiers = tui::Modifier::None,
                           .codepoint = static_cast<char32_t>(ch) };
}

/// @brief Creates a Backspace key event.
tui::InputEvent backspaceEvent()
{
    return tui::KeyEvent { .key = tui::KeyCode::Backspace, .modifiers = tui::Modifier::None };
}

/// @brief Builds a prefix-consistent suggest function: completes any prefix of @p word to @p word.
///
/// Mirrors a real prefix completer just enough to exercise the ghost-text lifecycle, and lets a
/// test count how often the (notionally expensive) completer is consulted.
PromptComponent::SuggestFn prefixSuggest(std::string word, int* calls = nullptr)
{
    return [word = std::move(word), calls](std::string_view text,
                                           std::size_t /*cursor*/) -> std::optional<std::string> {
        if (calls)
            ++*calls;
        if (!text.empty() && word.starts_with(text) && text.size() < word.size())
            return word.substr(text.size());
        return std::nullopt;
    };
}

} // namespace

TEST_CASE("PromptComponent.ctrl_d_first_press_shows_hint", "[prompt]")
{
    auto comp = PromptComponent();
    auto const action = comp.processInput(ctrlD());
    CHECK(action == PromptComponent::Action::Changed);
    CHECK(comp.inputField().hasGhostText());
}

TEST_CASE("PromptComponent.ctrl_d_double_press_exits", "[prompt]")
{
    auto comp = PromptComponent();
    auto const first = comp.processInput(ctrlD());
    CHECK(first == PromptComponent::Action::Changed);

    auto const second = comp.processInput(ctrlD());
    CHECK(second == PromptComponent::Action::Eof);
}

TEST_CASE("PromptComponent.ctrl_d_after_timeout_shows_hint_again", "[prompt]")
{
    auto comp = PromptComponent();
    // Use a short timeout for testing
    auto config = comp.promptConfig();
    config.exitConfirmTimeoutMs = 50;
    comp.setPromptConfig(std::move(config));

    auto const first = comp.processInput(ctrlD());
    CHECK(first == PromptComponent::Action::Changed);

    // Wait for timeout to expire
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    comp.flushDeferredUpdates();

    // Next Ctrl+D should show hint again (not exit)
    auto const third = comp.processInput(ctrlD());
    CHECK(third == PromptComponent::Action::Changed);
    CHECK(comp.inputField().hasGhostText());
}

TEST_CASE("PromptComponent.typing_after_ctrl_d_clears_hint", "[prompt]")
{
    auto comp = PromptComponent();
    auto const first = comp.processInput(ctrlD());
    CHECK(first == PromptComponent::Action::Changed);
    CHECK(comp.inputField().hasGhostText());

    // Type a character — this clears the hint
    auto const typed = comp.processInput(charEvent('a'));
    CHECK(typed == PromptComponent::Action::Changed);
    // Ghost text should be cleared (the exit hint was dismissed)
    CHECK_FALSE(comp.inputField().hasGhostText());
}

// ============================================================================
// Ghost-text first-deletion flicker (typing the last suggestion char, then backspace)
// ============================================================================

TEST_CASE("PromptComponent.ghost_text_no_flicker_on_first_backspace", "[prompt]")
{
    // Reproduces the first-deletion flicker: a stable ghost is showing; the user types the final
    // char of the suggestion (trimming the ghost to empty) and immediately backspaces, BEFORE the
    // 100ms ghost-text debounce fires. Without the consumed-prefix restore, the ghost would be
    // blank for that frame and only reappear after the debounce — a visible flash.
    auto comp = PromptComponent();
    comp.setSuggestFn(prefixSuggest("git checkout"));

    for (char const ch: std::string_view("git checkou"))
        (void) comp.processInput(charEvent(ch));
    std::this_thread::sleep_for(std::chrono::milliseconds(110)); // let the ghost debounce elapse
    comp.flushDeferredUpdates();                                 // settle: ghost is "t" and stable
    REQUIRE(comp.inputField().text() == "git checkou");
    REQUIRE(comp.inputField().ghostText() == "t");

    // Type the final 't' — trims the ghost to empty (suggestion fully consumed).
    (void) comp.processInput(charEvent('t'));
    CHECK(comp.inputField().text() == "git checkout");
    CHECK(comp.inputField().ghostText().empty());

    // Immediate backspace, no debounce flush in between: the ghost must already be correct.
    (void) comp.processInput(backspaceEvent());
    CHECK(comp.inputField().text() == "git checkou");
    CHECK(comp.inputField().ghostText() == "t"); // restored synchronously — no flash
}

TEST_CASE("PromptComponent.ghost_text_first_backspace_survives_debounce_recompute", "[prompt]")
{
    // The synchronously restored ghost must agree with what the debounced recompute produces, so the
    // later flush is a no-op rather than a second visible change.
    int calls = 0;
    auto comp = PromptComponent();
    comp.setSuggestFn(prefixSuggest("git checkout", &calls));

    for (char const ch: std::string_view("git checkou"))
        (void) comp.processInput(charEvent(ch));
    std::this_thread::sleep_for(std::chrono::milliseconds(110));
    comp.flushDeferredUpdates();
    (void) comp.processInput(charEvent('t'));
    (void) comp.processInput(backspaceEvent());
    REQUIRE(comp.inputField().ghostText() == "t");

    // Let the debounce fire: recompute for "git checkou" yields the same "t" — ghost unchanged.
    std::this_thread::sleep_for(std::chrono::milliseconds(110));
    comp.flushDeferredUpdates();
    CHECK(comp.inputField().ghostText() == "t");
}

TEST_CASE("PromptComponent.ctrl_d_immediate_exit_when_disabled", "[prompt]")
{
    auto comp = PromptComponent();
    auto config = comp.promptConfig();
    config.exitConfirmTimeoutMs = 0;
    comp.setPromptConfig(std::move(config));

    auto const action = comp.processInput(ctrlD());
    CHECK(action == PromptComponent::Action::Eof);
}

TEST_CASE("PromptComponent.exitHintTimeoutMs_no_hint", "[prompt]")
{
    auto comp = PromptComponent();
    CHECK(comp.exitHintTimeoutMs() == -1);
}

TEST_CASE("PromptComponent.exitHintTimeoutMs_with_active_hint", "[prompt]")
{
    auto comp = PromptComponent();
    (void) comp.processInput(ctrlD());
    auto const timeout = comp.exitHintTimeoutMs();
    CHECK(timeout > 0);
    CHECK(timeout <= 1000);
}

// ============================================================================
// onHover position mapping tests (verifies screenToSourcePosition via public API)
// ============================================================================

// ============================================================================
// Mouse click-to-cursor positioning tests (verifies onEvent → handleMouseEvent)
// ============================================================================

TEST_CASE("PromptComponent.mouse_click_positions_cursor", "[prompt]")
{
    auto comp = PromptComponent();
    auto config = comp.promptConfig();
    config.layout = PromptLayoutKind::SingleLine;
    comp.setPromptConfig(std::move(config));
    comp.setPrompt("$ ");
    comp.inputField().setText("Hello World");

    // Layout: HorizontalMargin(1) + leftBarWidth(1) + PaddingAfterBar(1) + promptText("$ "=2) = 5
    // Input line at row = topPadding(1) + auroraFadeHeight(0) + chromeHeight(0) = 1
    // Component-relative 1-based: text starts at column 6 (5 + 1 for 1-based)
    // Click at grapheme 5 ("W") → column = 5 + 5 + 1 = 11
    auto mousePress = tui::MouseEvent {
        .type = tui::MouseEvent::Type::Press,
        .button = 0,
        .x = 11, // 1-based component-relative: 5 (field origin) + 5 (graphemes) + 1 (1-based)
        .y = 2,  // 1-based: row 1 (0-based) + 1
        .modifiers = tui::Modifier::None,
    };
    auto const result = comp.onEvent(tui::InputEvent { mousePress });
    CHECK(result == tui::EventResult::Handled);
    CHECK(comp.inputField().cursor() == 5); // Byte position of "W" in "Hello World"
}

TEST_CASE("PromptComponent.mouse_click_in_prompt_area_snaps_to_col0", "[prompt]")
{
    auto comp = PromptComponent();
    auto config = comp.promptConfig();
    config.layout = PromptLayoutKind::SingleLine;
    comp.setPromptConfig(std::move(config));
    comp.setPrompt("$ ");
    comp.inputField().setText("Hello");

    // Click in the prompt decoration area (before text starts)
    auto mousePress = tui::MouseEvent {
        .type = tui::MouseEvent::Type::Press,
        .button = 0,
        .x = 2, // In the bar/margin area
        .y = 2,
        .modifiers = tui::Modifier::None,
    };
    auto const result = comp.onEvent(tui::InputEvent { mousePress });
    CHECK(result == tui::EventResult::Handled);
    CHECK(comp.inputField().cursor() == 0); // Snapped to beginning
}

TEST_CASE("PromptComponent.mouse_click_beyond_text_snaps_to_end", "[prompt]")
{
    auto comp = PromptComponent();
    auto config = comp.promptConfig();
    config.layout = PromptLayoutKind::SingleLine;
    comp.setPromptConfig(std::move(config));
    comp.setPrompt("$ ");
    comp.inputField().setText("Hi");

    // Click far beyond text end
    auto mousePress = tui::MouseEvent {
        .type = tui::MouseEvent::Type::Press,
        .button = 0,
        .x = 50, // Way past "Hi"
        .y = 2,
        .modifiers = tui::Modifier::None,
    };
    auto const result = comp.onEvent(tui::InputEvent { mousePress });
    CHECK(result == tui::EventResult::Handled);
    CHECK(comp.inputField().cursor() == 2); // End of "Hi"
}

TEST_CASE("PromptComponent.mouse_non_mouse_event_ignored", "[prompt]")
{
    auto comp = PromptComponent();
    // Key events should be ignored by onEvent (handled by processInput instead)
    auto const result = comp.onEvent(charEvent('a'));
    CHECK(result == tui::EventResult::Ignored);
}

TEST_CASE("PromptComponent.onHover_withinText_returnsHoverInfo", "[prompt]")
{
    auto comp = PromptComponent();
    // SingleLine layout: chromeHeight=0, topPadding=1, auroraFadeHeight=0.
    auto config = comp.promptConfig();
    config.layout = PromptLayoutKind::SingleLine;
    comp.setPromptConfig(std::move(config));
    comp.setPrompt("$ ");
    comp.inputField().setText("let x = 5");

    // totalPromptWidth = HorizontalMargin(1) + leftBarWidth(1) + PaddingAfterBar(1) + displayWidth("$ ")(2) =
    // 5 Input line at y = topPadding(1) + auroraFadeHeight(0) + chromeHeight(0) = 1 x=5 maps to first
    // character 'l' of "let" — keyword with known hover info
    auto const result = comp.onHover(5, 1);
    REQUIRE(result.has_value());
    CHECK(result->text.find("let") != std::string::npos);
}

TEST_CASE("PromptComponent.onHover_beforePrompt_returnsNullopt", "[prompt]")
{
    auto comp = PromptComponent();
    auto config = comp.promptConfig();
    config.layout = PromptLayoutKind::SingleLine;
    comp.setPromptConfig(std::move(config));
    comp.setPrompt("$ ");
    comp.inputField().setText("let x = 5");

    // x=2 is within the prompt decoration area (before totalPromptWidth=5)
    auto const result = comp.onHover(2, 1);
    CHECK_FALSE(result.has_value());
}

TEST_CASE("PromptComponent.onHover_aboveInputLine_returnsNullopt", "[prompt]")
{
    auto comp = PromptComponent();
    auto config = comp.promptConfig();
    config.layout = PromptLayoutKind::SingleLine;
    comp.setPromptConfig(std::move(config));
    comp.setPrompt("$ ");
    comp.inputField().setText("let x = 5");

    // y=0 is the top padding row, before the input line at y=1
    auto const result = comp.onHover(5, 0);
    CHECK_FALSE(result.has_value());
}

TEST_CASE("PromptComponent.onHover_belowInputLine_returnsNullopt", "[prompt]")
{
    auto comp = PromptComponent();
    auto config = comp.promptConfig();
    config.layout = PromptLayoutKind::SingleLine;
    comp.setPromptConfig(std::move(config));
    comp.setPrompt("$ ");
    comp.inputField().setText("let x = 5");

    // y=2 is below the single input line (at y=1)
    auto const result = comp.onHover(5, 2);
    CHECK_FALSE(result.has_value());
}
