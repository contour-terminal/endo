// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <tui/InputField.hpp>

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

    // Ctrl+A selects all
    (void) field.processEvent(charKey('a', Modifier::Ctrl));
    CHECK(field.hasSelection());
    CHECK(field.selectedText() == "hello world");
}

TEST_CASE("InputField.selection_cleared_on_unshifted_movement")
{
    InputField field;
    field.setText("hello");
    (void) field.processEvent(charKey('a', Modifier::Ctrl)); // Select all

    CHECK(field.hasSelection());

    // Unshifted movement clears selection
    (void) field.processEvent(specialKey(KeyCode::Left));
    CHECK_FALSE(field.hasSelection());
}

TEST_CASE("InputField.typing_replaces_selection")
{
    InputField field;
    field.setText("hello");
    (void) field.processEvent(charKey('a', Modifier::Ctrl)); // Select all

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

    // Yank it back
    (void) field.processEvent(charKey('y', Modifier::Ctrl));
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

TEST_CASE("InputField.ctrl_c_aborts")
{
    InputField field;
    field.setText("something");

    auto action = field.processEvent(charKey('c', Modifier::Ctrl));
    CHECK(action == InputFieldAction::Abort);
}

TEST_CASE("InputField.ctrl_c_copies_when_selection")
{
    InputField field;
    field.setText("hello");
    (void) field.processEvent(charKey('a', Modifier::Ctrl)); // Select all

    // Ctrl+C with selection should copy, not abort
    auto action = field.processEvent(charKey('c', Modifier::Ctrl));
    CHECK(action == InputFieldAction::Changed);
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
    (void) field.processEvent(charKey('a', Modifier::Ctrl)); // Select all

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
