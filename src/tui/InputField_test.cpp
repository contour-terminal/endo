// SPDX-License-Identifier: Apache-2.0
#include <tui/Buffer.hpp>
#include <tui/Canvas.hpp>
#include <tui/InputField.hpp>
#include <tui/TextDecorator.hpp>
#include <tui/Theme.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace tui;

namespace
{

// Helper to create a KeyEvent for a printable character
KeyEvent charKey(char c, Modifier mod = Modifier::None)
{
    return KeyEvent { .key = keyCodeFromCodepoint(static_cast<char32_t>(c)),
                      .modifiers = mod,
                      .codepoint = static_cast<char32_t>(c) };
}

// Helper to create a KeyEvent for a special key
KeyEvent specialKey(KeyCode key, Modifier mod = Modifier::None)
{
    return KeyEvent { .key = key, .modifiers = mod, .codepoint = 0 };
}

} // namespace

// ============================================================================
// Basic editing tests
// ============================================================================

TEST_CASE("InputField.empty_initial_state")
{
    InputField field;
    CHECK(field.text().empty());
    CHECK(field.cursor() == 0);
    CHECK(field.lineCount() == 1);
    CHECK(field.cursorLine() == 0);
    CHECK(field.cursorColumn() == 0);
}

TEST_CASE("InputField.insert_characters")
{
    InputField field;

    (void) field.processEvent(charKey('h'));
    (void) field.processEvent(charKey('e'));
    (void) field.processEvent(charKey('l'));
    (void) field.processEvent(charKey('l'));
    (void) field.processEvent(charKey('o'));

    CHECK(field.text() == "hello");
    CHECK(field.cursor() == 5);
}

TEST_CASE("InputField.backspace")
{
    InputField field;
    field.setText("hello");

    (void) field.processEvent(specialKey(KeyCode::Backspace));
    CHECK(field.text() == "hell");
    CHECK(field.cursor() == 4);

    (void) field.processEvent(specialKey(KeyCode::Backspace));
    (void) field.processEvent(specialKey(KeyCode::Backspace));
    CHECK(field.text() == "he");
    CHECK(field.cursor() == 2);
}

TEST_CASE("InputField.delete_key")
{
    InputField field;
    field.setText("hello");

    // Move cursor to start
    (void) field.processEvent(specialKey(KeyCode::Home));
    CHECK(field.cursor() == 0);

    (void) field.processEvent(specialKey(KeyCode::Delete));
    CHECK(field.text() == "ello");

    (void) field.processEvent(specialKey(KeyCode::Delete));
    CHECK(field.text() == "llo");
}

TEST_CASE("InputField.clear")
{
    InputField field;
    field.setText("hello world");

    field.clear();
    CHECK(field.text().empty());
    CHECK(field.cursor() == 0);
}

TEST_CASE("InputField.setText")
{
    InputField field;
    field.setText("hello");
    CHECK(field.text() == "hello");
    CHECK(field.cursor() == 5); // Cursor at end

    field.setText("world");
    CHECK(field.text() == "world");
    CHECK(field.cursor() == 5);
}

// ============================================================================
// Cursor movement tests
// ============================================================================

TEST_CASE("InputField.cursor_movement_left_right")
{
    InputField field;
    field.setText("hello");

    // Cursor starts at end
    CHECK(field.cursor() == 5);

    // Move left
    (void) field.processEvent(specialKey(KeyCode::Left));
    CHECK(field.cursor() == 4);

    (void) field.processEvent(specialKey(KeyCode::Left));
    (void) field.processEvent(specialKey(KeyCode::Left));
    CHECK(field.cursor() == 2);

    // Move right
    (void) field.processEvent(specialKey(KeyCode::Right));
    CHECK(field.cursor() == 3);

    // Can't move past end
    (void) field.processEvent(specialKey(KeyCode::Right));
    (void) field.processEvent(specialKey(KeyCode::Right));
    (void) field.processEvent(specialKey(KeyCode::Right)); // Should stop at end
    CHECK(field.cursor() == 5);
}

TEST_CASE("InputField.cursor_home_end")
{
    InputField field;
    field.setText("hello world");

    (void) field.processEvent(specialKey(KeyCode::Home));
    CHECK(field.cursor() == 0);

    (void) field.processEvent(specialKey(KeyCode::End));
    CHECK(field.cursor() == 11);
}

TEST_CASE("InputField.word_movement")
{
    InputField field;
    field.setText("hello world test");
    (void) field.processEvent(specialKey(KeyCode::Home));

    // Move forward by word (Ctrl+Right)
    (void) field.processEvent(specialKey(KeyCode::Right, Modifier::Ctrl));
    CHECK(field.cursor() == 5); // After "hello"

    (void) field.processEvent(specialKey(KeyCode::Right, Modifier::Ctrl));
    CHECK(field.cursor() == 11); // After "world"

    // Move backward by word (Ctrl+Left)
    (void) field.processEvent(specialKey(KeyCode::Left, Modifier::Ctrl));
    CHECK(field.cursor() == 6); // Before "world"
}

// ============================================================================
// Selection tests
// ============================================================================

TEST_CASE("InputField.selection_with_shift_arrows")
{
    InputField field;
    field.setText("hello");
    (void) field.processEvent(specialKey(KeyCode::Home));

    // Select forward with Shift+Right
    (void) field.processEvent(specialKey(KeyCode::Right, Modifier::Shift));
    CHECK(field.hasSelection());
    CHECK(field.selectionStart() == 0);
    CHECK(field.selectionEnd() == 1);
    CHECK(field.selectedText() == "h");

    (void) field.processEvent(specialKey(KeyCode::Right, Modifier::Shift));
    (void) field.processEvent(specialKey(KeyCode::Right, Modifier::Shift));
    CHECK(field.selectedText() == "hel");
}

TEST_CASE("InputField.selection_shift_home_end")
{
    InputField field;
    field.setText("hello world");

    // Start in middle
    (void) field.processEvent(specialKey(KeyCode::Home));
    (void) field.processEvent(specialKey(KeyCode::Right));
    (void) field.processEvent(specialKey(KeyCode::Right));
    (void) field.processEvent(specialKey(KeyCode::Right));
    CHECK(field.cursor() == 3);

    // Shift+Home selects to start
    (void) field.processEvent(specialKey(KeyCode::Home, Modifier::Shift));
    CHECK(field.hasSelection());
    CHECK(field.selectedText() == "hel");

    // Clear and try Shift+End
    field.clearSelection();
    (void) field.processEvent(specialKey(KeyCode::Home));
    (void) field.processEvent(specialKey(KeyCode::Right));
    (void) field.processEvent(specialKey(KeyCode::Right));
    (void) field.processEvent(specialKey(KeyCode::Right));

    (void) field.processEvent(specialKey(KeyCode::End, Modifier::Shift));
    CHECK(field.hasSelection());
    CHECK(field.selectedText() == "lo world");
}

TEST_CASE("InputField.select_all")
{
    InputField field;
    field.setText("hello world");

    // Ctrl+Shift+A selects all
    (void) field.processEvent(charKey('a', Modifier::Ctrl | Modifier::Shift));
    CHECK(field.hasSelection());
    CHECK(field.selectedText() == "hello world");
}

TEST_CASE("InputField.selection_cleared_on_unshifted_movement")
{
    InputField field;
    field.setText("hello");
    (void) field.processEvent(charKey('a', Modifier::Ctrl | Modifier::Shift)); // Select all

    CHECK(field.hasSelection());

    // Unshifted movement clears selection
    (void) field.processEvent(specialKey(KeyCode::Left));
    CHECK_FALSE(field.hasSelection());
}

TEST_CASE("InputField.typing_replaces_selection")
{
    InputField field;
    field.setText("hello");
    (void) field.processEvent(charKey('a', Modifier::Ctrl | Modifier::Shift)); // Select all

    // Typing replaces selection
    (void) field.processEvent(charKey('x'));
    CHECK(field.text() == "x");
    CHECK_FALSE(field.hasSelection());
}

TEST_CASE("InputField.backspace_deletes_selection")
{
    InputField field;
    field.setText("hello world");
    (void) field.processEvent(specialKey(KeyCode::Home));

    // Select "hello"
    for (int i = 0; i < 5; ++i)
        (void) field.processEvent(specialKey(KeyCode::Right, Modifier::Shift));

    CHECK(field.selectedText() == "hello");

    // Backspace deletes selection
    (void) field.processEvent(specialKey(KeyCode::Backspace));
    CHECK(field.text() == " world");
    CHECK_FALSE(field.hasSelection());
}

// ============================================================================
// Undo/Redo tests
// ============================================================================

TEST_CASE("InputField.undo_single_operation")
{
    InputField field;

    CHECK_FALSE(field.canUndo());

    (void) field.processEvent(charKey('h'));
    (void) field.processEvent(charKey('i'));

    CHECK(field.text() == "hi");
    CHECK(field.canUndo());

    // Undo last character
    (void) field.processEvent(charKey('z', Modifier::Ctrl));
    CHECK(field.text() == "h");

    // Undo first character
    (void) field.processEvent(charKey('z', Modifier::Ctrl));
    CHECK(field.text().empty());
}

TEST_CASE("InputField.redo")
{
    InputField field;

    (void) field.processEvent(charKey('a'));
    (void) field.processEvent(charKey('b'));
    (void) field.processEvent(charKey('c'));
    CHECK(field.text() == "abc");

    // Undo twice
    (void) field.processEvent(charKey('z', Modifier::Ctrl));
    (void) field.processEvent(charKey('z', Modifier::Ctrl));
    CHECK(field.text() == "a");
    CHECK(field.canRedo());

    // Redo once
    (void) field.processEvent(charKey('z', Modifier::Ctrl | Modifier::Shift));
    CHECK(field.text() == "ab");

    // Redo again
    (void) field.processEvent(charKey('z', Modifier::Ctrl | Modifier::Shift));
    CHECK(field.text() == "abc");
    CHECK_FALSE(field.canRedo());
}

TEST_CASE("InputField.redo_ctrl_y")
{
    InputField field;

    (void) field.processEvent(charKey('a'));
    (void) field.processEvent(charKey('b'));
    (void) field.processEvent(charKey('c'));
    CHECK(field.text() == "abc");

    // Undo twice
    (void) field.processEvent(charKey('z', Modifier::Ctrl));
    (void) field.processEvent(charKey('z', Modifier::Ctrl));
    CHECK(field.text() == "a");

    // Redo with Ctrl+Y (modern keybinding)
    (void) field.processEvent(charKey('y', Modifier::Ctrl));
    CHECK(field.text() == "ab");

    // Redo again with Ctrl+Y
    (void) field.processEvent(charKey('y', Modifier::Ctrl));
    CHECK(field.text() == "abc");
}

TEST_CASE("InputField.new_edit_clears_redo_stack")
{
    InputField field;

    (void) field.processEvent(charKey('a'));
    (void) field.processEvent(charKey('b'));

    // Undo
    (void) field.processEvent(charKey('z', Modifier::Ctrl));
    CHECK(field.text() == "a");
    CHECK(field.canRedo());

    // New edit clears redo
    (void) field.processEvent(charKey('x'));
    CHECK(field.text() == "ax");
    CHECK_FALSE(field.canRedo());
}

TEST_CASE("InputField.clear_undo_history")
{
    InputField field;

    (void) field.processEvent(charKey('a'));
    (void) field.processEvent(charKey('b'));
    CHECK(field.canUndo());

    field.clearUndoHistory();
    CHECK_FALSE(field.canUndo());
    CHECK_FALSE(field.canRedo());
}

// ============================================================================
// Multiline tests
// ============================================================================

TEST_CASE("InputField.multiline_disabled_by_default")
{
    InputField field;
    CHECK_FALSE(field.isMultiline());
}

TEST_CASE("InputField.multiline_line_count")
{
    InputField field;
    field.setMultiline(true);
    field.setText("line1\nline2\nline3");

    CHECK(field.lineCount() == 3);
}

TEST_CASE("InputField.multiline_cursor_line_and_column")
{
    InputField field;
    field.setMultiline(true);
    field.setText("hello\nworld");

    // Cursor at end
    CHECK(field.cursorLine() == 1);
    CHECK(field.cursorColumn() == 5);

    // Move to start of buffer
    (void) field.processEvent(specialKey(KeyCode::Home, Modifier::Ctrl));
    CHECK(field.cursorLine() == 0);
    CHECK(field.cursorColumn() == 0);

    // Move right to 'e'
    (void) field.processEvent(specialKey(KeyCode::Right));
    CHECK(field.cursorLine() == 0);
    CHECK(field.cursorColumn() == 1);
}

TEST_CASE("InputField.multiline_lineAt")
{
    InputField field;
    field.setMultiline(true);
    field.setText("first\nsecond\nthird");

    CHECK(field.lineAt(0) == "first");
    CHECK(field.lineAt(1) == "second");
    CHECK(field.lineAt(2) == "third");
    CHECK(field.lineAt(3).empty());  // Out of range
    CHECK(field.lineAt(-1).empty()); // Negative index
}

TEST_CASE("InputField.multiline_up_down_movement")
{
    InputField field;
    field.setMultiline(true);
    field.setText("hello\nworld\ntest");

    // Start at end of last line
    CHECK(field.cursorLine() == 2);

    // Move up
    (void) field.processEvent(specialKey(KeyCode::Up));
    CHECK(field.cursorLine() == 1);

    (void) field.processEvent(specialKey(KeyCode::Up));
    CHECK(field.cursorLine() == 0);
}

TEST_CASE("InputField.multiline_insert_newline_with_alt_enter")
{
    InputField field;
    field.setMultiline(true);
    field.setText("hello");
    (void) field.processEvent(specialKey(KeyCode::End));

    // Alt+Enter inserts newline
    (void) field.processEvent(specialKey(KeyCode::Enter, Modifier::Alt));
    CHECK(field.text() == "hello\n");
    CHECK(field.lineCount() == 2);
}

TEST_CASE("InputField.multiline_insert_newline_with_shift_enter")
{
    InputField field;
    field.setMultiline(true);
    field.setText("hello");
    (void) field.processEvent(specialKey(KeyCode::End));

    // Shift+Enter inserts newline
    (void) field.processEvent(specialKey(KeyCode::Enter, Modifier::Shift));
    CHECK(field.text() == "hello\n");
    CHECK(field.lineCount() == 2);
}

TEST_CASE("InputField.multiline_multiple_lines_then_submit")
{
    InputField field;
    field.setMultiline(true);

    // Type "echo a"
    for (char c: std::string_view("echo a"))
        (void) field.processEvent(charKey(c));

    // Shift+Enter
    auto action = field.processEvent(specialKey(KeyCode::Enter, Modifier::Shift));
    CHECK(action == InputFieldAction::Changed);

    // Type "echo b"
    for (char c: std::string_view("echo b"))
        (void) field.processEvent(charKey(c));

    // Shift+Enter
    action = field.processEvent(specialKey(KeyCode::Enter, Modifier::Shift));
    CHECK(action == InputFieldAction::Changed);

    // Type "echo c"
    for (char c: std::string_view("echo c"))
        (void) field.processEvent(charKey(c));

    // Verify full content before submit
    CHECK(field.text() == "echo a\necho b\necho c");
    CHECK(field.lineCount() == 3);

    // Enter (submit)
    action = field.processEvent(specialKey(KeyCode::Enter));
    CHECK(action == InputFieldAction::Submit);

    // After submit, text should still be intact
    CHECK(field.text() == "echo a\necho b\necho c");
}

TEST_CASE("InputField.multiline_max_lines_limit")
{
    InputField field;
    field.setMultiline(true);
    field.setMaxLines(2);
    field.setText("line1");

    // First newline should work
    (void) field.processEvent(specialKey(KeyCode::Enter, Modifier::Alt));
    CHECK(field.lineCount() == 2);

    // Add some text
    (void) field.processEvent(charKey('x'));

    // Second newline should be blocked
    (void) field.processEvent(specialKey(KeyCode::Enter, Modifier::Alt));
    CHECK(field.lineCount() == 2); // Still 2
}

TEST_CASE("InputField.multiline_home_goes_to_line_start")
{
    InputField field;
    field.setMultiline(true);
    field.setText("hello\nworld");

    // Cursor at end of "world"
    CHECK(field.cursorLine() == 1);
    CHECK(field.cursorColumn() == 5);

    // Home goes to start of line, not buffer
    (void) field.processEvent(specialKey(KeyCode::Home));
    CHECK(field.cursorLine() == 1);
    CHECK(field.cursorColumn() == 0);
}

TEST_CASE("InputField.multiline_ctrl_home_goes_to_buffer_start")
{
    InputField field;
    field.setMultiline(true);
    field.setText("hello\nworld");

    // Ctrl+Home goes to buffer start
    (void) field.processEvent(specialKey(KeyCode::Home, Modifier::Ctrl));
    CHECK(field.cursorLine() == 0);
    CHECK(field.cursorColumn() == 0);
    CHECK(field.cursor() == 0);
}

// ============================================================================
// History tests
// ============================================================================

TEST_CASE("InputField.history_navigation")
{
    InputField field;
    field.addHistory("first");
    field.addHistory("second");
    field.addHistory("third");

    // Up arrow goes back in history
    (void) field.processEvent(specialKey(KeyCode::Up));
    CHECK(field.text() == "third");

    (void) field.processEvent(specialKey(KeyCode::Up));
    CHECK(field.text() == "second");

    (void) field.processEvent(specialKey(KeyCode::Up));
    CHECK(field.text() == "first");

    // Down arrow goes forward
    (void) field.processEvent(specialKey(KeyCode::Down));
    CHECK(field.text() == "second");
}

TEST_CASE("InputField.history_saves_current_line")
{
    InputField field;
    field.addHistory("history1");

    // Type something
    (void) field.processEvent(charKey('t'));
    (void) field.processEvent(charKey('e'));
    (void) field.processEvent(charKey('s'));
    (void) field.processEvent(charKey('t'));
    CHECK(field.text() == "test");

    // Go up to history
    (void) field.processEvent(specialKey(KeyCode::Up));
    CHECK(field.text() == "history1");

    // Go back down - should restore what was typed
    (void) field.processEvent(specialKey(KeyCode::Down));
    CHECK(field.text() == "test");
}

TEST_CASE("InputField.history_no_duplicates")
{
    InputField field;
    field.addHistory("same");
    field.addHistory("same"); // Should be ignored

    (void) field.processEvent(specialKey(KeyCode::Up));
    CHECK(field.text() == "same");

    // Should not have another "same" before this
    (void) field.processEvent(specialKey(KeyCode::Up));
    CHECK(field.text() == "same"); // Still the same entry
}

// ============================================================================
// Kill ring tests (Emacs-style)
// ============================================================================

TEST_CASE("InputField.kill_to_end_of_line")
{
    InputField field;
    field.setText("hello world");
    (void) field.processEvent(specialKey(KeyCode::Home));

    // Move to middle
    for (int i = 0; i < 5; ++i)
        (void) field.processEvent(specialKey(KeyCode::Right));

    // Ctrl+K kills to end
    (void) field.processEvent(charKey('k', Modifier::Ctrl));
    CHECK(field.text() == "hello");
}

TEST_CASE("InputField.kill_to_start_of_line")
{
    InputField field;
    field.setText("hello world");

    // Move to middle
    (void) field.processEvent(specialKey(KeyCode::Home));
    for (int i = 0; i < 6; ++i)
        (void) field.processEvent(specialKey(KeyCode::Right));

    // Ctrl+U kills to start
    (void) field.processEvent(charKey('u', Modifier::Ctrl));
    CHECK(field.text() == "world");
}

TEST_CASE("InputField.yank")
{
    InputField field;
    field.setText("hello");

    // Kill the text
    (void) field.processEvent(charKey('u', Modifier::Ctrl));
    CHECK(field.text().empty());

    // Yank it back with Ctrl+V (Paste, which uses kill ring)
    (void) field.processEvent(charKey('v', Modifier::Ctrl));
    CHECK(field.text() == "hello");
}

// ============================================================================
// Submit and abort tests
// ============================================================================

TEST_CASE("InputField.enter_submits")
{
    InputField field;
    field.setText("command");

    auto action = field.processEvent(specialKey(KeyCode::Enter));
    CHECK(action == InputFieldAction::Submit);
}

TEST_CASE("InputField.ctrl_c_copies_without_selection")
{
    InputField field;
    field.setText("something");

    // Ctrl+C without selection does nothing (no abort)
    auto action = field.processEvent(charKey('c', Modifier::Ctrl));
    CHECK(action == InputFieldAction::None);
}

TEST_CASE("InputField.ctrl_d_aborts_on_empty")
{
    InputField field;
    // Empty buffer

    // Ctrl+D on empty buffer returns Eof (abort)
    auto action = field.processEvent(charKey('d', Modifier::Ctrl));
    CHECK(action == InputFieldAction::Eof);
}

TEST_CASE("InputField.ctrl_d_deletes_char_when_not_empty")
{
    InputField field;
    field.setText("hello");

    // Move cursor to beginning
    (void) field.processEvent(specialKey(KeyCode::Home));

    // Ctrl+D on non-empty buffer deletes character at cursor
    auto action = field.processEvent(charKey('d', Modifier::Ctrl));
    CHECK(action == InputFieldAction::Changed);
    CHECK(field.text() == "ello");
}

TEST_CASE("InputField.ctrl_c_copies_when_selection")
{
    InputField field;
    field.setText("hello");
    (void) field.processEvent(charKey('a', Modifier::Ctrl | Modifier::Shift)); // Select all

    // Ctrl+C with selection should copy, not abort
    auto action = field.processEvent(charKey('c', Modifier::Ctrl));
    CHECK(action == InputFieldAction::Changed);

    // Clear selection and buffer, then paste to verify copy worked
    field.clear();
    (void) field.processEvent(charKey('v', Modifier::Ctrl)); // Paste
    CHECK(field.text() == "hello");
}

TEST_CASE("InputField.ctrl_d_eof_on_empty")
{
    InputField field;
    // Empty buffer

    auto action = field.processEvent(charKey('d', Modifier::Ctrl));
    CHECK(action == InputFieldAction::Eof);
}

TEST_CASE("InputField.ctrl_d_deletes_when_not_empty")
{
    InputField field;
    field.setText("hello");
    (void) field.processEvent(specialKey(KeyCode::Home));

    auto action = field.processEvent(charKey('d', Modifier::Ctrl));
    CHECK(action == InputFieldAction::Changed);
    CHECK(field.text() == "ello");
}

// ============================================================================
// Paste event tests
// ============================================================================

TEST_CASE("InputField.paste_event_inserts_text")
{
    InputField field;
    field.setText("hello ");

    PasteEvent paste { .text = "world" };
    auto action = field.processEvent(paste);

    CHECK(action == InputFieldAction::Changed);
    CHECK(field.text() == "hello world");
}

TEST_CASE("InputField.paste_replaces_selection")
{
    InputField field;
    field.setText("hello");
    (void) field.processEvent(charKey('a', Modifier::Ctrl | Modifier::Shift)); // Select all

    PasteEvent paste { .text = "goodbye" };
    (void) field.processEvent(paste);

    CHECK(field.text() == "goodbye");
}

// ============================================================================
// Click-to-position tests
// ============================================================================

TEST_CASE("InputField.setCursorFromClick_single_line")
{
    InputField field;
    field.setText("hello world");

    field.setCursorFromClick(0, 5);
    CHECK(field.cursor() == 5);
    CHECK_FALSE(field.hasSelection());
}

TEST_CASE("InputField.setCursorFromClick_multiline")
{
    InputField field;
    field.setMultiline(true);
    field.setText("hello\nworld");

    field.setCursorFromClick(1, 2);
    CHECK(field.cursorLine() == 1);
    CHECK(field.cursorColumn() == 2);
}

TEST_CASE("InputField.setCursorFromClick_extends_selection")
{
    InputField field;
    field.setText("hello world");
    (void) field.processEvent(specialKey(KeyCode::Home));

    // Click with extend selection
    field.setCursorFromClick(0, 5, true);
    CHECK(field.hasSelection());
    CHECK(field.selectedText() == "hello");
}

// ============================================================================
// Transpose test
// ============================================================================

TEST_CASE("InputField.transpose_characters")
{
    InputField field;
    // Ctrl+T defaults to AgentMode; bind it to Transpose for this test
    field.keyBindings().bind(KeyChord::fromChar('t', Modifier::Ctrl), EditAction::Transpose);
    field.setText("ab");

    // Ctrl+T transposes characters before cursor
    (void) field.processEvent(charKey('t', Modifier::Ctrl));
    CHECK(field.text() == "ba");
}

// ============================================================================
// UTF-8 tests
// ============================================================================

TEST_CASE("InputField.utf8_characters")
{
    InputField field;

    // Insert UTF-8 characters - e with acute (U+00E9)
    (void) field.processEvent(KeyEvent {
        .key = keyCodeFromCodepoint(U'\u00E9'), .modifiers = Modifier::None, .codepoint = U'\u00E9' });
    // e with grave (U+00E8)
    (void) field.processEvent(KeyEvent {
        .key = keyCodeFromCodepoint(U'\u00E8'), .modifiers = Modifier::None, .codepoint = U'\u00E8' });

    // Should have two grapheme clusters
    CHECK(field.text() == "\xC3\xA9\xC3\xA8"); // UTF-8 encoding of e with acute + e with grave
}

// ============================================================================
// Mouse handling tests
// ============================================================================

TEST_CASE("InputField.mouse_click_positions_cursor")
{
    InputField field;
    field.setText("hello world");

    // Single click at column 5 should position cursor there
    auto action = field.handleMouse(MouseEvent::Type::Press, 0, 5, Modifier::None);
    CHECK(action == InputFieldAction::Changed);
    CHECK(field.cursor() == 5);
    CHECK(!field.hasSelection());
}

TEST_CASE("InputField.mouse_click_with_shift_extends_selection")
{
    InputField field;
    field.setText("hello world");
    (void) field.processEvent(specialKey(KeyCode::Home)); // Cursor at start

    // Shift+click at column 5 should create selection
    auto action = field.handleMouse(MouseEvent::Type::Press, 0, 5, Modifier::Shift);
    CHECK(action == InputFieldAction::Changed);
    CHECK(field.hasSelection());
    CHECK(field.selectedText() == "hello");
}

TEST_CASE("InputField.mouse_drag_creates_selection")
{
    InputField field;
    field.setText("hello world");

    // Press at column 0
    (void) field.handleMouse(MouseEvent::Type::Press, 0, 0, Modifier::None);
    CHECK(!field.hasSelection());

    // Drag to column 5
    auto action = field.handleMouse(MouseEvent::Type::Move, 0, 5, Modifier::None);
    CHECK(action == InputFieldAction::Changed);
    CHECK(field.hasSelection());
    CHECK(field.selectedText() == "hello");

    // Release
    action = field.handleMouse(MouseEvent::Type::Release, 0, 5, Modifier::None);
    CHECK(action == InputFieldAction::None);
    CHECK(field.hasSelection()); // Selection remains
}

TEST_CASE("InputField.double_click_selects_word")
{
    InputField field;
    field.setText("hello world");

    // First click
    (void) field.handleMouse(MouseEvent::Type::Press, 0, 7, Modifier::None);
    (void) field.handleMouse(MouseEvent::Type::Release, 0, 7, Modifier::None);

    // Second click (double-click) - should select "world"
    auto action = field.handleMouse(MouseEvent::Type::Press, 0, 7, Modifier::None);
    CHECK(action == InputFieldAction::Changed);
    CHECK(field.hasSelection());
    CHECK(field.selectedText() == "world");
}

TEST_CASE("InputField.double_click_on_path_selects_component")
{
    InputField field;
    field.setText("/home/user/file.txt");

    // First click on "user"
    (void) field.handleMouse(MouseEvent::Type::Press, 0, 7, Modifier::None);
    (void) field.handleMouse(MouseEvent::Type::Release, 0, 7, Modifier::None);

    // Second click (double-click) - should select "user" not whole path
    auto action = field.handleMouse(MouseEvent::Type::Press, 0, 7, Modifier::None);
    CHECK(action == InputFieldAction::Changed);
    CHECK(field.hasSelection());
    CHECK(field.selectedText() == "user");
}

TEST_CASE("InputField.triple_click_selects_line")
{
    InputField field;
    field.setMultiline(true);
    field.setText("first line\nsecond line\nthird line");

    // First click on second line
    (void) field.handleMouse(MouseEvent::Type::Press, 1, 3, Modifier::None);
    (void) field.handleMouse(MouseEvent::Type::Release, 1, 3, Modifier::None);

    // Second click
    (void) field.handleMouse(MouseEvent::Type::Press, 1, 3, Modifier::None);
    (void) field.handleMouse(MouseEvent::Type::Release, 1, 3, Modifier::None);

    // Third click (triple-click) - should select entire second line
    auto action = field.handleMouse(MouseEvent::Type::Press, 1, 3, Modifier::None);
    CHECK(action == InputFieldAction::Changed);
    CHECK(field.hasSelection());
    CHECK(field.selectedText() == "second line\n");
}

TEST_CASE("InputField.click_count_resets_after_timeout")
{
    InputField field;
    field.setText("hello world");

    // First click
    (void) field.handleMouse(MouseEvent::Type::Press, 0, 5, Modifier::None);
    (void) field.handleMouse(MouseEvent::Type::Release, 0, 5, Modifier::None);
    CHECK(!field.hasSelection());

    // Wait... (simulated by just doing another single click at different position)
    // Click at different position should reset click count
    (void) field.handleMouse(MouseEvent::Type::Press, 0, 0, Modifier::None);
    CHECK(!field.hasSelection()); // Single click, no selection
}

TEST_CASE("InputField.select_word_fish_style")
{
    InputField field;
    field.setText("hello-world.txt");

    // Select word at position 0 (should select "hello")
    field.selectWord(0);
    CHECK(field.hasSelection());
    CHECK(field.selectedText() == "hello");

    // Clear and select word at position 6 (on "-", should select just "-")
    field.clearSelection();
    field.selectWord(5); // The hyphen position
    CHECK(field.hasSelection());
    // Since hyphen is not a word char, we select just the character
    CHECK(field.selectedText() == "-");
}

TEST_CASE("InputField.select_line")
{
    InputField field;
    field.setMultiline(true);
    field.setText("line one\nline two\nline three");

    // Select line 1 (second line)
    field.selectLine(1);
    CHECK(field.hasSelection());
    CHECK(field.selectedText() == "line two\n");

    // Select line 0 (first line)
    field.selectLine(0);
    CHECK(field.hasSelection());
    CHECK(field.selectedText() == "line one\n");

    // Select last line (no trailing newline)
    field.selectLine(2);
    CHECK(field.hasSelection());
    CHECK(field.selectedText() == "line three");
}

TEST_CASE("InputField.scroll_by")
{
    InputField field;
    field.setMultiline(true);
    field.setText("line1\nline2\nline3\nline4\nline5");

    CHECK(field.scrollOffset() == 0);

    // Scroll down
    field.scrollBy(2);
    CHECK(field.scrollOffset() == 2);

    // Scroll up
    field.scrollBy(-1);
    CHECK(field.scrollOffset() == 1);

    // Don't scroll past beginning
    field.scrollBy(-10);
    CHECK(field.scrollOffset() == 0);

    // Don't scroll past end (4 lines means max offset of 4)
    field.scrollBy(100);
    CHECK(field.scrollOffset() == 4); // 5 lines, max offset is 4
}

// ============================================================================
// CapsLock and Shift capitalization tests (Kitty keyboard protocol)
// ============================================================================

TEST_CASE("InputField.shift_capitalizes_letters")
{
    InputField field;

    // Shift+a should produce 'A' (Kitty sends lowercase codepoint + Shift modifier)
    (void) field.processEvent(charKey('a', Modifier::Shift));
    CHECK(field.text() == "A");

    field.clear();

    // Shift+z should produce 'Z'
    (void) field.processEvent(charKey('z', Modifier::Shift));
    CHECK(field.text() == "Z");
}

TEST_CASE("InputField.capslock_capitalizes_letters")
{
    InputField field;

    // CapsLock active + 'a' should produce 'A'
    (void) field.processEvent(charKey('a', Modifier::CapsLock));
    CHECK(field.text() == "A");

    field.clear();

    // CapsLock active + 'z' should produce 'Z'
    (void) field.processEvent(charKey('z', Modifier::CapsLock));
    CHECK(field.text() == "Z");
}

TEST_CASE("InputField.capslock_plus_shift_produces_lowercase")
{
    InputField field;

    // CapsLock + Shift should cancel out (XOR behavior) - produces lowercase
    (void) field.processEvent(charKey('a', Modifier::CapsLock | Modifier::Shift));
    CHECK(field.text() == "a");

    field.clear();

    (void) field.processEvent(charKey('z', Modifier::CapsLock | Modifier::Shift));
    CHECK(field.text() == "z");
}

TEST_CASE("InputField.capslock_does_not_affect_numbers")
{
    InputField field;

    // CapsLock should not affect non-letter characters
    (void) field.processEvent(charKey('1', Modifier::CapsLock));
    CHECK(field.text() == "1");

    (void) field.processEvent(charKey('!', Modifier::CapsLock));
    CHECK(field.text() == "1!");

    (void) field.processEvent(charKey('@', Modifier::CapsLock));
    CHECK(field.text() == "1!@");
}

TEST_CASE("InputField.capslock_does_not_affect_symbols")
{
    InputField field;

    // CapsLock should not affect symbols
    (void) field.processEvent(charKey('-', Modifier::CapsLock));
    (void) field.processEvent(charKey('=', Modifier::CapsLock));
    (void) field.processEvent(charKey('[', Modifier::CapsLock));
    CHECK(field.text() == "-=[");
}

TEST_CASE("InputField.mixed_capslock_typing")
{
    InputField field;

    // Simulate typing "Hello" with CapsLock on, using Shift for lowercase 'e' and 'o'
    (void) field.processEvent(charKey('h', Modifier::CapsLock));                   // H
    (void) field.processEvent(charKey('e', Modifier::CapsLock | Modifier::Shift)); // e (shift cancels caps)
    (void) field.processEvent(charKey('l', Modifier::CapsLock));                   // L
    (void) field.processEvent(charKey('l', Modifier::CapsLock));                   // L
    (void) field.processEvent(charKey('o', Modifier::CapsLock | Modifier::Shift)); // o (shift cancels caps)

    CHECK(field.text() == "HeLLo");
}

// ============================================================================
// Smart cursor movement tests (Ctrl+A / Ctrl+E)
// ============================================================================

TEST_CASE("InputField.smart_move_to_line_start_single_line")
{
    InputField field;
    field.setText("hello");

    // Ctrl+A goes to position 0 in single-line mode
    (void) field.processEvent(charKey('a', Modifier::Ctrl));
    CHECK(field.cursor() == 0);
}

TEST_CASE("InputField.smart_move_to_line_start_multiline_from_middle")
{
    InputField field;
    field.setMultiline(true);
    field.setText("hello\nworld");

    // Cursor at end of "world" (position 11)
    CHECK(field.cursor() == 11);

    // First Ctrl+A goes to start of current line ("world" starts at 6)
    (void) field.processEvent(charKey('a', Modifier::Ctrl));
    CHECK(field.cursor() == 6);
}

TEST_CASE("InputField.smart_move_to_line_start_multiline_already_at_start")
{
    InputField field;
    field.setMultiline(true);
    field.setText("hello\nworld");

    // Move to start of second line
    (void) field.processEvent(charKey('a', Modifier::Ctrl));
    CHECK(field.cursor() == 6);

    // Second Ctrl+A jumps to start of previous line
    (void) field.processEvent(charKey('a', Modifier::Ctrl));
    CHECK(field.cursor() == 0);
}

TEST_CASE("InputField.smart_move_to_line_start_already_at_buffer_start")
{
    InputField field;
    field.setMultiline(true);
    field.setText("hello\nworld");

    // Move to buffer start
    (void) field.processEvent(specialKey(KeyCode::Home, Modifier::Ctrl));
    CHECK(field.cursor() == 0);

    // Ctrl+A stays at 0 (nowhere to go)
    (void) field.processEvent(charKey('a', Modifier::Ctrl));
    CHECK(field.cursor() == 0);
}

TEST_CASE("InputField.smart_move_to_line_end_single_line")
{
    InputField field;
    field.setText("hello");

    // Move to start first
    (void) field.processEvent(specialKey(KeyCode::Home));
    CHECK(field.cursor() == 0);

    // Ctrl+E goes to buffer end in single-line mode
    (void) field.processEvent(charKey('e', Modifier::Ctrl));
    CHECK(field.cursor() == 5);
}

TEST_CASE("InputField.smart_move_to_line_end_multiline_from_middle")
{
    InputField field;
    field.setMultiline(true);
    field.setText("hello\nworld");

    // Move to buffer start
    (void) field.processEvent(specialKey(KeyCode::Home, Modifier::Ctrl));
    CHECK(field.cursor() == 0);

    // Move to middle of first line
    (void) field.processEvent(specialKey(KeyCode::Right));
    (void) field.processEvent(specialKey(KeyCode::Right));
    CHECK(field.cursor() == 2);

    // First Ctrl+E goes to end of current line ("hello" ends at 5)
    (void) field.processEvent(charKey('e', Modifier::Ctrl));
    CHECK(field.cursor() == 5);
}

TEST_CASE("InputField.smart_move_to_line_end_multiline_already_at_end")
{
    InputField field;
    field.setMultiline(true);
    field.setText("hello\nworld");

    // Move to buffer start, then to end of first line
    (void) field.processEvent(specialKey(KeyCode::Home, Modifier::Ctrl));
    (void) field.processEvent(charKey('e', Modifier::Ctrl));
    CHECK(field.cursor() == 5);

    // Second Ctrl+E jumps to end of next line
    (void) field.processEvent(charKey('e', Modifier::Ctrl));
    CHECK(field.cursor() == 11);
}

TEST_CASE("InputField.smart_move_to_line_end_already_at_buffer_end")
{
    InputField field;
    field.setMultiline(true);
    field.setText("hello\nworld");

    // Cursor already at buffer end (11)
    CHECK(field.cursor() == 11);

    // Ctrl+E stays at end (nowhere to go)
    (void) field.processEvent(charKey('e', Modifier::Ctrl));
    CHECK(field.cursor() == 11);
}

TEST_CASE("InputField.smart_move_three_lines")
{
    InputField field;
    field.setMultiline(true);
    field.setText("aaa\nbbb\nccc");

    // Cursor at end of "ccc" (position 11)
    CHECK(field.cursor() == 11);

    // Ctrl+A from end of third line -> start of third line (8)
    (void) field.processEvent(charKey('a', Modifier::Ctrl));
    CHECK(field.cursor() == 8);

    // Ctrl+A again -> start of second line (4)
    (void) field.processEvent(charKey('a', Modifier::Ctrl));
    CHECK(field.cursor() == 4);

    // Ctrl+A again -> start of first line (0)
    (void) field.processEvent(charKey('a', Modifier::Ctrl));
    CHECK(field.cursor() == 0);

    // Ctrl+A again -> stays at 0
    (void) field.processEvent(charKey('a', Modifier::Ctrl));
    CHECK(field.cursor() == 0);
}

// ============================================================================
// lineStartOffset tests
// ============================================================================

TEST_CASE("InputField.lineStartOffset_single_line")
{
    InputField field;
    field.setText("hello");
    CHECK(field.lineStartOffset(0) == 0);
}

TEST_CASE("InputField.lineStartOffset_multiline")
{
    InputField field;
    field.setMultiline(true);
    field.setText("aaa\nbbb\nccc");

    CHECK(field.lineStartOffset(0) == 0); // "aaa"
    CHECK(field.lineStartOffset(1) == 4); // "bbb" (after "aaa\n")
    CHECK(field.lineStartOffset(2) == 8); // "ccc" (after "aaa\nbbb\n")
}

TEST_CASE("InputField.lineStartOffset_empty_lines")
{
    InputField field;
    field.setMultiline(true);
    field.setText("a\n\nc");

    CHECK(field.lineStartOffset(0) == 0); // "a"
    CHECK(field.lineStartOffset(1) == 2); // "" (empty line)
    CHECK(field.lineStartOffset(2) == 3); // "c"
}

// ============================================================================
// Multiline rendering tests
// ============================================================================

TEST_CASE("InputField.multiline_render_basic")
{
    // Verify that multiline render puts content on the correct rows
    InputField field;
    field.setMultiline(true);
    field.setText("aaa\nbbb");

    Buffer buf(10, 40);
    auto theme = darkTheme();
    Canvas canvas(buf, Rect { .x = 0, .y = 0, .width = 40, .height = 10 }, theme);
    field.setArea(Rect { .x = 0, .y = 0, .width = 40, .height = 5 });
    field.render(canvas);

    // Row 0 should contain "aaa", row 1 should contain "bbb"
    CHECK(buf.at(0, 0).grapheme == "a");
    CHECK(buf.at(0, 1).grapheme == "a");
    CHECK(buf.at(0, 2).grapheme == "a");
    CHECK(buf.at(1, 0).grapheme == "b");
    CHECK(buf.at(1, 1).grapheme == "b");
    CHECK(buf.at(1, 2).grapheme == "b");
}

TEST_CASE("InputField.multiline_render_with_prompt")
{
    InputField field;
    field.setMultiline(true);
    field.setPrompt("> ");
    field.setContinuationPrompt("  ");
    field.setText("aaa\nbbb");

    Buffer buf(10, 40);
    auto theme = darkTheme();
    Canvas canvas(buf, Rect { .x = 0, .y = 0, .width = 40, .height = 10 }, theme);
    field.setArea(Rect { .x = 0, .y = 0, .width = 40, .height = 5 });
    field.render(canvas);

    // Row 0: "> " prompt then "aaa"
    CHECK(buf.at(0, 0).grapheme == ">");
    CHECK(buf.at(0, 1).grapheme == " ");
    CHECK(buf.at(0, 2).grapheme == "a");
    // Row 1: "  " continuation then "bbb"
    CHECK(buf.at(1, 0).grapheme == " ");
    CHECK(buf.at(1, 1).grapheme == " ");
    CHECK(buf.at(1, 2).grapheme == "b");
}

TEST_CASE("InputField.multiline_render_ghost_text_on_last_line")
{
    InputField field;
    field.setMultiline(true);
    field.setText("aaa\nbbb");
    field.setGhostText("ccc");

    Buffer buf(10, 40);
    auto theme = darkTheme();
    Canvas canvas(buf, Rect { .x = 0, .y = 0, .width = 40, .height = 10 }, theme);
    field.setArea(Rect { .x = 0, .y = 0, .width = 40, .height = 5 });
    field.render(canvas);

    // Ghost text appears after "bbb" on row 1 (last line)
    CHECK(buf.at(1, 3).grapheme == "c");
    CHECK(buf.at(1, 4).grapheme == "c");
    CHECK(buf.at(1, 5).grapheme == "c");
}

TEST_CASE("InputField.multiline_continuation_prompt")
{
    InputField field;
    field.setMultiline(true);
    field.setContinuationPrompt(".. ");
    CHECK(field.continuationPrompt() == ".. ");
}

// ============================================================================
// TextDecorator tests
// ============================================================================

TEST_CASE("InputField.multiline_auto_continuation_alignment")
{
    // When no continuation prompt is set, continuation lines should auto-align
    // to the same column as the first line's text (spaces matching prompt width).
    InputField field;
    field.setMultiline(true);
    field.setPrompt("> "); // 2-column prompt, NO setContinuationPrompt call
    field.setText("aaa\nbbb\nccc");

    Buffer buf(10, 40);
    auto theme = darkTheme();
    Canvas canvas(buf, Rect { .x = 0, .y = 0, .width = 40, .height = 10 }, theme);
    field.setArea(Rect { .x = 0, .y = 0, .width = 40, .height = 5 });
    field.render(canvas);

    // Row 0: "> aaa" — text starts at col 2
    CHECK(buf.at(0, 0).grapheme == ">");
    CHECK(buf.at(0, 2).grapheme == "a");
    // Row 1: "  bbb" — auto-continuation: 2 spaces, text at col 2
    CHECK(buf.at(1, 0).grapheme == " ");
    CHECK(buf.at(1, 1).grapheme == " ");
    CHECK(buf.at(1, 2).grapheme == "b");
    // Row 2: "  ccc" — same alignment
    CHECK(buf.at(2, 0).grapheme == " ");
    CHECK(buf.at(2, 1).grapheme == " ");
    CHECK(buf.at(2, 2).grapheme == "c");
}

TEST_CASE("InputField.decorator_foreground_applied")
{
    // Simple decorator that colors the first grapheme red
    struct TestDecorator: TextDecorator
    {
        mutable std::vector<std::size_t> queriedIndices;

        [[nodiscard]] auto foreground(TextPosition pos) const -> std::optional<RgbColor> override
        {
            queriedIndices.push_back(pos.graphemeIndex);
            if (pos.graphemeIndex == 0)
                return RgbColor { .r = 255, .g = 0, .b = 0 };
            return {};
        }
    };

    InputField field;
    field.setText("abc");

    TestDecorator decorator;
    field.setTextDecorator(&decorator);

    Buffer buf(10, 40);
    auto theme = darkTheme();
    Canvas canvas(buf, Rect { .x = 0, .y = 0, .width = 40, .height = 10 }, theme);
    field.setArea(Rect { .x = 0, .y = 0, .width = 40, .height = 1 });
    field.render(canvas);

    // Decorator should be queried for each grapheme (a, b, c)
    REQUIRE(decorator.queriedIndices.size() == 3);
    CHECK(decorator.queriedIndices[0] == 0);
    CHECK(decorator.queriedIndices[1] == 1);
    CHECK(decorator.queriedIndices[2] == 2);

    // First grapheme should have red foreground
    CHECK(std::holds_alternative<RgbColor>(buf.at(0, 0).style.fg));
    auto const& fg = std::get<RgbColor>(buf.at(0, 0).style.fg);
    CHECK(fg.r == 255);
    CHECK(fg.g == 0);
    CHECK(fg.b == 0);
}

TEST_CASE("InputField.decorator_background_applied")
{
    struct TestDecorator: TextDecorator
    {
        mutable int callCount = 0;

        [[nodiscard]] auto background(int /*displayCol*/) const -> std::optional<RgbColor> override
        {
            ++callCount;
            return RgbColor { .r = 42, .g = 42, .b = 42 };
        }
    };

    InputField field;
    field.setText("ab");

    TestDecorator decorator;
    field.setTextDecorator(&decorator);

    Buffer buf(10, 40);
    auto theme = darkTheme();
    Canvas canvas(buf, Rect { .x = 0, .y = 0, .width = 40, .height = 10 }, theme);
    field.setArea(Rect { .x = 0, .y = 0, .width = 40, .height = 1 });
    field.render(canvas);

    // Background decorator should be called during rendering
    CHECK(decorator.callCount > 0);

    // Text cells should have the decorator background
    CHECK(std::holds_alternative<RgbColor>(buf.at(0, 0).style.bg));
    auto const& bg = std::get<RgbColor>(buf.at(0, 0).style.bg);
    CHECK(bg.r == 42);
}

TEST_CASE("InputField.decorator_cleared_with_nullptr")
{
    struct TestDecorator: TextDecorator
    {
        mutable int callCount = 0;

        [[nodiscard]] auto foreground(TextPosition /*pos*/) const -> std::optional<RgbColor> override
        {
            ++callCount;
            return RgbColor { .r = 255, .g = 0, .b = 0 };
        }
    };

    InputField field;
    field.setText("a");

    TestDecorator decorator;
    field.setTextDecorator(&decorator);
    field.setTextDecorator(nullptr); // Clear decorator

    Buffer buf(10, 40);
    auto theme = darkTheme();
    Canvas canvas(buf, Rect { .x = 0, .y = 0, .width = 40, .height = 10 }, theme);
    field.setArea(Rect { .x = 0, .y = 0, .width = 40, .height = 1 });
    field.render(canvas);

    // Decorator should NOT be called after being cleared
    CHECK(decorator.callCount == 0);
}

TEST_CASE("InputField.decorator_multiline_global_grapheme_index")
{
    // Verify that the decorator receives global (buffer-wide) grapheme indices in multiline mode
    struct TestDecorator: TextDecorator
    {
        mutable std::vector<std::size_t> queriedIndices;

        [[nodiscard]] auto foreground(TextPosition pos) const -> std::optional<RgbColor> override
        {
            queriedIndices.push_back(pos.graphemeIndex);
            return {};
        }
    };

    InputField field;
    field.setMultiline(true);
    field.setText("ab\ncd"); // 2 graphemes + newline + 2 graphemes = indices 0,1,(2=newline),3,4

    TestDecorator decorator;
    field.setTextDecorator(&decorator);

    Buffer buf(10, 40);
    auto theme = darkTheme();
    Canvas canvas(buf, Rect { .x = 0, .y = 0, .width = 40, .height = 10 }, theme);
    field.setArea(Rect { .x = 0, .y = 0, .width = 40, .height = 5 });
    field.render(canvas);

    // Should have queried indices for a(0), b(1), c(3), d(4) — index 2 is the newline (skipped)
    REQUIRE(decorator.queriedIndices.size() == 4);
    CHECK(decorator.queriedIndices[0] == 0); // 'a'
    CHECK(decorator.queriedIndices[1] == 1); // 'b'
    CHECK(decorator.queriedIndices[2] == 3); // 'c' (index 2 = newline)
    CHECK(decorator.queriedIndices[3] == 4); // 'd'
}
