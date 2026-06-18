// SPDX-License-Identifier: Apache-2.0
#include <tui/completer/CompletionItem.hpp>
#include <tui/completer/CompletionProvider.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

#include <agent/ui/AgentInputComponent.hpp>

using namespace endo::agent;
using namespace std::chrono_literals;

namespace
{

/// @brief Creates a printable character key event.
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

/// @brief Creates a Tab key event.
tui::InputEvent tabEvent()
{
    return tui::KeyEvent { .key = tui::KeyCode::Tab, .modifiers = tui::Modifier::None };
}

/// @brief Creates a Ctrl+W key event (word-backward delete).
tui::InputEvent ctrlW()
{
    return tui::KeyEvent { .key = static_cast<tui::KeyCode>('w'),
                           .modifiers = tui::Modifier::Ctrl,
                           .codepoint = 'w' };
}

/// @brief Prefix-completion provider: completes any prefix of @p word to @p word.
///
/// Mirrors a real prefix completer just enough to drive the ghost-text lifecycle through the
/// production completion path (Completer::suggest), without a test-only injection seam.
class PrefixProvider: public tui::CompletionProvider
{
  public:
    explicit PrefixProvider(std::string word): _word(std::move(word)) {}

    std::vector<tui::CompletionItem> complete(std::string_view input, size_t /*cursor*/) override
    {
        if (!input.empty() && _word.starts_with(input) && input.size() < _word.size())
            return { tui::CompletionItem { .text = _word } };
        return {};
    }

  private:
    std::string _word;
};

/// @brief Wires @p comp with a prefix-completion provider for @p word (AgentInputComponent is
///        non-copyable, so it is configured in place rather than returned).
void suggestWith(AgentInputComponent& comp, std::string word)
{
    comp.addCompletionProvider(std::make_unique<PrefixProvider>(std::move(word)));
}

} // namespace

TEST_CASE("AgentInputComponent.tab_accepts_ghost_text", "[agent][ghost]")
{
    auto comp = AgentInputComponent();
    suggestWith(comp, "cmake --build");

    for (char const ch: std::string_view("cmake"))
        (void) comp.processInput(charEvent(ch));
    std::this_thread::sleep_for(110ms);
    comp.flushDeferredUpdates();
    REQUIRE(comp.inputField().ghostText() == " --build");

    // Tab with a ghost showing accepts the suggestion.
    (void) comp.processInput(tabEvent());
    CHECK(comp.inputField().text() == "cmake --build");
    CHECK(comp.inputField().ghostText().empty());
}

TEST_CASE("AgentInputComponent.tab_without_ghost_falls_through_to_completion", "[agent][ghost]")
{
    // Regression: when the pre-accept recompute clears a re-prepended guess the completer no longer
    // offers, Tab must NOT be swallowed — it must fall through to ordinary Tab completion.
    auto comp = AgentInputComponent();
    suggestWith(comp, "cmake --build");

    // Settle the full suggestion, then accept it so the buffer is the complete word.
    for (char const ch: std::string_view("cmake"))
        (void) comp.processInput(charEvent(ch));
    std::this_thread::sleep_for(110ms);
    comp.flushDeferredUpdates();
    (void) comp.processInput(tui::InputEvent { tui::KeyEvent { .key = tui::KeyCode::End } });
    REQUIRE(comp.inputField().text() == "cmake --build");

    // Word-delete the last word: the consumed-prefix seed restores "build" as ghost synchronously
    // (Ctrl+W stops at the non-word "--", leaving "cmake --").
    (void) comp.processInput(ctrlW());
    REQUIRE(comp.inputField().text() == "cmake --");
    REQUIRE(comp.inputField().ghostText() == "build");

    // Now type a char that makes the buffer NOT a prefix of the only candidate, so the completer
    // offers nothing. The displayed ghost is a stale guess; pressing Tab must recompute (clearing it)
    // and then fall through to completion rather than being swallowed.
    (void) comp.processInput(charEvent('z')); // buffer "cmake --z" — not a prefix of "cmake --build"
    (void) comp.processInput(tabEvent());
    // The ghost is gone (completer offered nothing for "cmake --z") and Tab did not accept anything.
    CHECK(comp.inputField().text() == "cmake --z");
    CHECK(comp.inputField().ghostText().empty());
}

TEST_CASE("AgentInputComponent.ctrl_e_accepts_ghost_text", "[agent][ghost]")
{
    auto comp = AgentInputComponent();
    suggestWith(comp, "cmake --build");

    for (char const ch: std::string_view("cmake"))
        (void) comp.processInput(charEvent(ch));
    std::this_thread::sleep_for(110ms);
    comp.flushDeferredUpdates();
    REQUIRE(comp.inputField().ghostText() == " --build");

    auto const ctrlE = tui::InputEvent { tui::KeyEvent {
        .key = static_cast<tui::KeyCode>('e'), .modifiers = tui::Modifier::Ctrl, .codepoint = 'e' } };
    (void) comp.processInput(ctrlE);
    CHECK(comp.inputField().text() == "cmake --build");
    CHECK(comp.inputField().ghostText().empty());
}

TEST_CASE("AgentInputComponent.ghost_restored_after_accept_then_word_backspace", "[agent][ghost]")
{
    // Accept the whole suggestion, then word-backward delete the last word: it must reappear as ghost
    // synchronously (no debounce gap) — the accept seeds the consumed-prefix memory.
    auto comp = AgentInputComponent();
    suggestWith(comp, "cmake --build install");

    for (char const ch: std::string_view("cmake"))
        (void) comp.processInput(charEvent(ch));
    std::this_thread::sleep_for(110ms);
    comp.flushDeferredUpdates();
    REQUIRE(comp.inputField().ghostText() == " --build install");

    (void) comp.processInput(tabEvent()); // accept
    REQUIRE(comp.inputField().text() == "cmake --build install");
    REQUIRE(comp.inputField().ghostText().empty());

    (void) comp.processInput(ctrlW());
    CHECK(comp.inputField().text() == "cmake --build ");
    CHECK(comp.inputField().ghostText() == "install"); // restored synchronously
}

TEST_CASE("AgentInputComponent.history_recall_then_backspace_does_not_resurrect_ghost", "[agent][ghost]")
{
    // After accepting a suggestion (which seeds the consumed-prefix memory), recalling a history
    // entry replaces the buffer wholesale. A subsequent end-of-buffer backspace must NOT restore a
    // ghost belonging to the previous buffer — historyPrev() must clear the ghost state.
    auto comp = AgentInputComponent();
    suggestWith(comp, "cmake --build install");
    comp.inputField().addHistory("ls -la");

    for (char const ch: std::string_view("cmake"))
        (void) comp.processInput(charEvent(ch));
    std::this_thread::sleep_for(110ms);
    comp.flushDeferredUpdates();
    (void) comp.processInput(tabEvent()); // accept -> _ghostConsumed = " --build install"
    REQUIRE(comp.inputField().text() == "cmake --build install");

    // Recall the history entry via Up arrow.
    (void) comp.processInput(tui::InputEvent { tui::KeyEvent { .key = tui::KeyCode::Up } });
    REQUIRE(comp.inputField().text() == "ls -la");
    REQUIRE(comp.inputField().ghostText().empty());

    // Backspace at end of the recalled line must not resurrect a previous-buffer suggestion.
    (void) comp.processInput(backspaceEvent());
    CHECK(comp.inputField().text() == "ls -l");
    CHECK(comp.inputField().ghostText().empty());
}
