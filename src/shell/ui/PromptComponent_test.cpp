// SPDX-License-Identifier: Apache-2.0
#include <tui/InputEvent.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
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
