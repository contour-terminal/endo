// SPDX-License-Identifier: Apache-2.0
#include <string>

#if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wold-style-cast"
#endif
#include <libunicode/utf8_grapheme_segmenter.h>
#if defined(__clang__)
    #pragma clang diagnostic pop
#endif

#include <tui/InputField.hpp>

namespace tui
{

namespace
{
    /// @brief Encodes a Unicode codepoint as UTF-8.
    /// @param cp The codepoint to encode.
    /// @return UTF-8 encoded string.
    auto encodeUtf8(char32_t cp) -> std::string
    {
        auto result = std::string {};
        if (cp < 0x80)
        {
            result += static_cast<char>(cp);
        }
        else if (cp < 0x800)
        {
            result += static_cast<char>(0xC0 | (cp >> 6));
            result += static_cast<char>(0x80 | (cp & 0x3F));
        }
        else if (cp < 0x10000)
        {
            result += static_cast<char>(0xE0 | (cp >> 12));
            result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            result += static_cast<char>(0x80 | (cp & 0x3F));
        }
        else if (cp < 0x110000)
        {
            result += static_cast<char>(0xF0 | (cp >> 18));
            result += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            result += static_cast<char>(0x80 | (cp & 0x3F));
        }
        return result;
    }

    /// @brief Advances past one UTF-8 codepoint.
    auto nextUtf8(std::string_view s, std::size_t pos) -> std::size_t
    {
        if (pos >= s.size())
            return pos;
        ++pos;
        while (pos < s.size() && (static_cast<unsigned char>(s[pos]) & 0xC0) == 0x80)
            ++pos;
        return pos;
    }

    /// @brief Moves back one UTF-8 codepoint.
    auto prevUtf8(std::string_view s, std::size_t pos) -> std::size_t
    {
        if (pos == 0)
            return 0;
        --pos;
        while (pos > 0 && (static_cast<unsigned char>(s[pos]) & 0xC0) == 0x80)
            --pos;
        return pos;
    }
} // namespace

auto InputField::processEvent(InputEvent const& event) -> InputFieldAction
{
    if (auto const* key = std::get_if<KeyEvent>(&event))
        return handleKey(*key);

    if (auto const* paste = std::get_if<PasteEvent>(&event))
    {
        // Save undo state before any changes
        saveUndoState();
        // Delete selection first if any (paste replaces selection)
        if (hasSelection())
            deleteSelection();
        insertText(paste->text);
        _lastWasKill = false;
        return InputFieldAction::Changed;
    }

    return InputFieldAction::None;
}

auto InputField::text() const noexcept -> std::string_view
{
    return _buffer;
}

auto InputField::cursor() const noexcept -> std::size_t
{
    return _cursor;
}

void InputField::setPrompt(std::string_view prompt)
{
    _prompt = std::string(prompt);
}

auto InputField::prompt() const noexcept -> std::string_view
{
    return _prompt;
}

void InputField::clear()
{
    _buffer.clear();
    _cursor = 0;
    clearSelection();
}

void InputField::setText(std::string_view text)
{
    _buffer = std::string(text);
    _cursor = _buffer.size();
    clearSelection();
}

void InputField::addHistory(std::string entry)
{
    if (entry.empty())
        return;
    // Avoid consecutive duplicates
    if (!_history.empty() && _history.back() == entry)
        return;
    _history.push_back(std::move(entry));
    if (_history.size() > _maxHistory)
        _history.erase(_history.begin());
    _historyIndex = _history.size();
}

void InputField::setMaxHistory(std::size_t n)
{
    _maxHistory = n;
    while (_history.size() > _maxHistory)
        _history.erase(_history.begin());
}

auto InputField::handleKey(KeyEvent const& key) -> InputFieldAction
{
    auto const ctrl = hasModifier(key.modifiers, Modifier::Ctrl);
    auto const alt = hasModifier(key.modifiers, Modifier::Alt);
    auto const shift = hasModifier(key.modifiers, Modifier::Shift);

    // Special keys
    switch (key.key)
    {
        case KeyCode::Enter:
            _lastWasKill = false;
            // In multiline mode: Shift+Enter OR Alt+Enter inserts newline
            // Alt+Enter works universally (ESC prefix), Shift+Enter requires Kitty protocol
            if (_multiline && (shift || alt))
            {
                saveUndoState();
                // Delete selection first if any
                if (hasSelection())
                    deleteSelection();
                insertNewline();
                return InputFieldAction::Changed;
            }
            clearSelection();
            return InputFieldAction::Submit;
        case KeyCode::Tab:
            // Tab could be expanded in the future; for now insert literal tab
            _lastWasKill = false;
            return InputFieldAction::None;
        case KeyCode::Escape: _lastWasKill = false; return InputFieldAction::None;
        case KeyCode::Backspace:
            _lastWasKill = false;
            if (hasSelection())
            {
                saveUndoState();
                deleteSelection();
                return InputFieldAction::Changed;
            }
            if (ctrl || alt)
            {
                killWordBackward();
                return InputFieldAction::Changed;
            }
            deleteCharBackward();
            return InputFieldAction::Changed;
        case KeyCode::Delete:
            _lastWasKill = false;
            if (hasSelection())
            {
                saveUndoState();
                deleteSelection();
                return InputFieldAction::Changed;
            }
            deleteChar();
            return InputFieldAction::Changed;
        case KeyCode::Up:
            _lastWasKill = false;
            if (_multiline)
            {
                if (shift)
                    moveWithSelection(&InputField::moveUp);
                else
                {
                    clearSelection();
                    moveUp(); // moveUp() falls back to historyPrev() on first line
                }
            }
            else
            {
                clearSelection();
                historyPrev();
            }
            return InputFieldAction::Changed;
        case KeyCode::Down:
            _lastWasKill = false;
            if (_multiline)
            {
                if (shift)
                    moveWithSelection(&InputField::moveDown);
                else
                {
                    clearSelection();
                    moveDown(); // moveDown() falls back to historyNext() on last line
                }
            }
            else
            {
                clearSelection();
                historyNext();
            }
            return InputFieldAction::Changed;
        case KeyCode::Left:
            _lastWasKill = false;
            if (shift)
            {
                if (ctrl)
                    moveWithSelection(&InputField::moveBackwardWord);
                else
                    moveWithSelection(&InputField::moveBackwardChar);
            }
            else
            {
                clearSelection();
                if (ctrl)
                    moveBackwardWord();
                else
                    moveBackwardChar();
            }
            return InputFieldAction::Changed;
        case KeyCode::Right:
            _lastWasKill = false;
            if (shift)
            {
                if (ctrl)
                    moveWithSelection(&InputField::moveForwardWord);
                else
                    moveWithSelection(&InputField::moveForwardChar);
            }
            else
            {
                clearSelection();
                if (ctrl)
                    moveForwardWord();
                else
                    moveForwardChar();
            }
            return InputFieldAction::Changed;
        case KeyCode::Home:
            _lastWasKill = false;
            if (shift)
            {
                if (_multiline && ctrl)
                    moveWithSelection(&InputField::moveToBufferStart);
                else if (_multiline)
                    moveWithSelection(&InputField::moveToLineStart);
                else
                    moveWithSelection(&InputField::moveToStart);
            }
            else
            {
                clearSelection();
                if (_multiline && ctrl)
                    moveToBufferStart();
                else if (_multiline)
                    moveToLineStart();
                else
                    moveToStart();
            }
            return InputFieldAction::Changed;
        case KeyCode::End:
            _lastWasKill = false;
            if (shift)
            {
                if (_multiline && ctrl)
                    moveWithSelection(&InputField::moveToBufferEnd);
                else if (_multiline)
                    moveWithSelection(&InputField::moveToLineEnd);
                else
                    moveWithSelection(&InputField::moveToEnd);
            }
            else
            {
                clearSelection();
                if (_multiline && ctrl)
                    moveToBufferEnd();
                else if (_multiline)
                    moveToLineEnd();
                else
                    moveToEnd();
            }
            return InputFieldAction::Changed;
        default: break;
    }

    // Ctrl+letter combinations
    if (ctrl && key.codepoint != 0)
    {
        switch (key.codepoint)
        {
            case 'a':
                _lastWasKill = false;
                // Ctrl+A: Select all (GUI-style) takes precedence over Emacs move-to-start
                selectAll();
                return InputFieldAction::Changed;
            case 'e':
                _lastWasKill = false;
                if (_multiline)
                    moveToLineEnd();
                else
                    moveToEnd();
                return InputFieldAction::Changed;
            case 'f':
                moveForwardChar();
                _lastWasKill = false;
                return InputFieldAction::Changed;
            case 'b':
                moveBackwardChar();
                _lastWasKill = false;
                return InputFieldAction::Changed;
            case 'p':
                _lastWasKill = false;
                if (_multiline)
                    moveUp();
                else
                    historyPrev();
                return InputFieldAction::Changed;
            case 'n':
                _lastWasKill = false;
                if (_multiline)
                    moveDown();
                else
                    historyNext();
                return InputFieldAction::Changed;
            case 'k': killToEnd(); return InputFieldAction::Changed;
            case 'u': killToStart(); return InputFieldAction::Changed;
            case 'w': killWordBackward(); return InputFieldAction::Changed;
            case 'y':
                yank();
                _lastWasKill = false;
                return InputFieldAction::Changed;
            case 't':
                transpose();
                _lastWasKill = false;
                return InputFieldAction::Changed;
            case 'd':
                if (_buffer.empty())
                    return InputFieldAction::Eof;
                deleteChar();
                _lastWasKill = false;
                return InputFieldAction::Changed;
            case 'c':
                // Ctrl+C: Copy if selection, otherwise abort
                if (hasSelection())
                {
                    copySelection();
                    _lastWasKill = false;
                    return InputFieldAction::Changed;
                }
                return InputFieldAction::Abort;
            case 'x':
                // Ctrl+X: Cut selection
                if (hasSelection())
                {
                    saveUndoState();
                    cutSelection();
                    _lastWasKill = false;
                    return InputFieldAction::Changed;
                }
                return InputFieldAction::None;
            case 'z':
                _lastWasKill = false;
                // Ctrl+Shift+Z: Redo, Ctrl+Z: Undo
                if (shift)
                {
                    if (redo())
                        return InputFieldAction::Changed;
                }
                else
                {
                    if (undo())
                        return InputFieldAction::Changed;
                }
                return InputFieldAction::None;
            default: break;
        }
    }

    // Alt+letter combinations
    if (alt && key.codepoint != 0)
    {
        switch (key.codepoint)
        {
            case 'f':
                moveForwardWord();
                _lastWasKill = false;
                return InputFieldAction::Changed;
            case 'b':
                moveBackwardWord();
                _lastWasKill = false;
                return InputFieldAction::Changed;
            case 'd': killWord(); return InputFieldAction::Changed;
            case 'y':
                yankPop();
                _lastWasKill = false;
                return InputFieldAction::Changed;
            default: break;
        }
    }

    // Printable codepoint
    if (key.codepoint != 0 && !ctrl && !alt && isPrintable(key.key))
    {
        // Save undo state before any changes
        saveUndoState();

        // Delete selection first if any (typing replaces selection)
        if (hasSelection())
            deleteSelection();

        auto cp = key.codepoint;
        // Handle Shift modifier for letter capitalization (needed for Kitty keyboard protocol)
        // The terminal sends lowercase codepoint + Shift modifier, we need to uppercase it
        if (shift && cp >= 'a' && cp <= 'z')
            cp = cp - 'a' + 'A';
        insertCodepoint(cp);
        _lastWasKill = false;
        return InputFieldAction::Changed;
    }

    return InputFieldAction::None;
}

void InputField::killToEnd()
{
    if (_cursor < _buffer.size())
    {
        saveUndoState();
        auto killed = _buffer.substr(_cursor);
        _buffer.erase(_cursor);
        pushKillRing(std::move(killed));
    }
    _lastWasKill = true;
}

void InputField::killToStart()
{
    if (_cursor > 0)
    {
        saveUndoState();
        auto killed = _buffer.substr(0, _cursor);
        _buffer.erase(0, _cursor);
        _cursor = 0;
        pushKillRing(std::move(killed));
    }
    _lastWasKill = true;
}

void InputField::killWord()
{
    auto const start = _cursor;
    // Find end position without modifying cursor
    auto endPos = _cursor;
    auto const size = _buffer.size();
    while (endPos < size && !isWordCharAt(_buffer[endPos]))
        endPos = nextGraphemeCluster(endPos);
    while (endPos < size && isWordCharAt(_buffer[endPos]))
        endPos = nextGraphemeCluster(endPos);

    if (endPos > start)
    {
        saveUndoState();
        auto killed = _buffer.substr(start, endPos - start);
        _buffer.erase(start, endPos - start);
        pushKillRing(std::move(killed));
    }
    _lastWasKill = true;
}

void InputField::killWordBackward()
{
    auto const end = _cursor;
    // Find start position without modifying cursor
    auto startPos = _cursor;
    while (startPos > 0 && !isWordCharAt(_buffer[prevGraphemeCluster(startPos)]))
        startPos = prevGraphemeCluster(startPos);
    while (startPos > 0 && isWordCharAt(_buffer[prevGraphemeCluster(startPos)]))
        startPos = prevGraphemeCluster(startPos);

    if (startPos < end)
    {
        saveUndoState();
        auto killed = _buffer.substr(startPos, end - startPos);
        _buffer.erase(startPos, end - startPos);
        _cursor = startPos;
        pushKillRing(std::move(killed));
    }
    _lastWasKill = true;
}

void InputField::yank()
{
    if (_killRing.empty())
        return;
    saveUndoState();
    _killRingIndex = _killRing.size() - 1;
    insertText(_killRing[_killRingIndex]);
}

void InputField::yankPop()
{
    if (_killRing.empty())
        return;
    saveUndoState();
    // Remove the previously yanked text and insert the next one in the ring
    if (_killRingIndex < _killRing.size())
    {
        auto const& prev = _killRing[_killRingIndex];
        if (_cursor >= prev.size())
        {
            _buffer.erase(_cursor - prev.size(), prev.size());
            _cursor -= prev.size();
        }
    }
    _killRingIndex = (_killRingIndex == 0) ? _killRing.size() - 1 : _killRingIndex - 1;
    insertText(_killRing[_killRingIndex]);
}

void InputField::deleteChar()
{
    if (_cursor >= _buffer.size())
        return;
    saveUndoState();
    auto const next = nextGraphemeCluster(_cursor);
    _buffer.erase(_cursor, next - _cursor);
}

void InputField::deleteCharBackward()
{
    if (_cursor == 0)
        return;
    saveUndoState();
    auto const prev = prevGraphemeCluster(_cursor);
    _buffer.erase(prev, _cursor - prev);
    _cursor = prev;
}

void InputField::moveToStart()
{
    _cursor = 0;
}

void InputField::moveToEnd()
{
    _cursor = _buffer.size();
}

void InputField::moveForwardChar()
{
    _cursor = nextGraphemeCluster(_cursor);
}

void InputField::moveBackwardChar()
{
    _cursor = prevGraphemeCluster(_cursor);
}

void InputField::moveForwardWord()
{
    auto const size = _buffer.size();
    // Emacs forward-word: skip non-word chars, then skip word chars
    while (_cursor < size && !isWordCharAt(_buffer[_cursor]))
        _cursor = nextUtf8(_buffer, _cursor);
    while (_cursor < size && isWordCharAt(_buffer[_cursor]))
        _cursor = nextUtf8(_buffer, _cursor);
}

void InputField::moveBackwardWord()
{
    // Skip whitespace/non-word characters
    while (_cursor > 0 && !isWordCharAt(_buffer[prevUtf8(_buffer, _cursor)]))
        _cursor = prevUtf8(_buffer, _cursor);
    // Skip word characters
    while (_cursor > 0 && isWordCharAt(_buffer[prevUtf8(_buffer, _cursor)]))
        _cursor = prevUtf8(_buffer, _cursor);
}

void InputField::historyPrev()
{
    if (_history.empty())
        return;
    if (_historyIndex == _history.size())
        _savedLine = _buffer;
    if (_historyIndex > 0)
    {
        --_historyIndex;
        _buffer = _history[_historyIndex];
        _cursor = _buffer.size();
        clearSelection();
    }
}

void InputField::historyNext()
{
    if (_historyIndex >= _history.size())
        return;
    ++_historyIndex;
    if (_historyIndex == _history.size())
    {
        _buffer = _savedLine;
        _savedLine.clear();
    }
    else
    {
        _buffer = _history[_historyIndex];
    }
    _cursor = _buffer.size();
    clearSelection();
}

void InputField::transpose()
{
    if (_cursor == 0 || _buffer.size() < 2)
        return;
    saveUndoState();
    // If at end, transpose the two characters before cursor
    auto pos = _cursor;
    if (pos == _buffer.size())
        pos = prevGraphemeCluster(pos);
    auto const prevPos = prevGraphemeCluster(pos);
    auto const nextPos = nextGraphemeCluster(pos);

    auto first = _buffer.substr(prevPos, pos - prevPos);
    auto second = _buffer.substr(pos, nextPos - pos);

    _buffer.replace(prevPos, nextPos - prevPos, second + first);
    _cursor = nextPos;
}

void InputField::pushKillRing(std::string text)
{
    if (text.empty())
        return;
    if (_lastWasKill && !_killRing.empty())
    {
        // Append to the last kill ring entry
        _killRing.back() += text;
    }
    else
    {
        _killRing.push_back(std::move(text));
        if (_killRing.size() > MaxKillRing)
            _killRing.erase(_killRing.begin());
    }
}

void InputField::insertCodepoint(char32_t cp)
{
    auto const utf8 = encodeUtf8(cp);
    _buffer.insert(_cursor, utf8);
    _cursor += utf8.size();
}

void InputField::insertText(std::string_view text)
{
    _buffer.insert(_cursor, text);
    _cursor += text.size();
}

auto InputField::nextGraphemeCluster(std::size_t pos) const -> std::size_t
{
    if (pos >= _buffer.size())
        return pos;

    // Use libunicode utf8_grapheme_segmenter for proper grapheme cluster boundaries.
    // The iterator's _clusterStart pointer tracks byte positions in the original string_view.
    auto const sv = std::string_view(_buffer).substr(pos);
    auto segmenter = unicode::utf8_grapheme_segmenter(sv);
    auto it = segmenter.begin();
    if (it == segmenter.end())
        return nextUtf8(_buffer, pos);

    ++it; // Advance past the first grapheme cluster
    if (it != segmenter.end())
    {
        // The iterator's _clusterStart points into the original sv data
        auto const byteOffset = static_cast<std::size_t>(it._clusterStart - sv.data());
        return pos + byteOffset;
    }

    // The first cluster spans to the end
    return _buffer.size();
}

auto InputField::prevGraphemeCluster(std::size_t pos) const -> std::size_t
{
    if (pos == 0)
        return 0;

    // Segment the text up to pos and find the last cluster boundary.
    auto const sv = std::string_view(_buffer).substr(0, pos);
    auto segmenter = unicode::utf8_grapheme_segmenter(sv);

    auto lastBoundaryOffset = std::size_t { 0 };
    for (auto it = segmenter.begin(); it != segmenter.end(); ++it)
    {
        auto const currentOffset = static_cast<std::size_t>(it._clusterStart - sv.data());
        lastBoundaryOffset = currentOffset;
    }

    return lastBoundaryOffset;
}

auto InputField::isWordCharAt(char c) -> bool
{
    return c != ' ' && c != '\t' && c != '\n' && c != '\r';
}

// ============================================================================
// Multiline support
// ============================================================================

void InputField::setMultiline(bool enable)
{
    _multiline = enable;
}

auto InputField::isMultiline() const noexcept -> bool
{
    return _multiline;
}

auto InputField::lineCount() const noexcept -> int
{
    if (_buffer.empty())
        return 1;

    int count = 1;
    for (char c: _buffer)
    {
        if (c == '\n')
            ++count;
    }
    return count;
}

auto InputField::cursorLine() const noexcept -> int
{
    int line = 0;
    for (std::size_t i = 0; i < _cursor && i < _buffer.size(); ++i)
    {
        if (_buffer[i] == '\n')
            ++line;
    }
    return line;
}

auto InputField::cursorColumn() const noexcept -> int
{
    auto const lineStart = findLineStart(_cursor);
    return countGraphemesInRange(lineStart, _cursor);
}

auto InputField::lineAt(int lineIndex) const -> std::string_view
{
    if (lineIndex < 0)
        return {};

    std::size_t start = 0;
    int currentLine = 0;

    // Find start of requested line
    while (currentLine < lineIndex && start < _buffer.size())
    {
        if (_buffer[start] == '\n')
            ++currentLine;
        ++start;
    }

    if (currentLine < lineIndex)
        return {}; // Line index out of range

    // Find end of line
    std::size_t end = start;
    while (end < _buffer.size() && _buffer[end] != '\n')
        ++end;

    return std::string_view(_buffer).substr(start, end - start);
}

void InputField::setMaxLines(int maxLines)
{
    _maxLines = maxLines;
}

auto InputField::maxLines() const noexcept -> int
{
    return _maxLines;
}

auto InputField::findLineStart(std::size_t pos) const -> std::size_t
{
    if (pos == 0)
        return 0;

    // Search backward for newline
    std::size_t i = pos;
    while (i > 0)
    {
        --i;
        if (_buffer[i] == '\n')
            return i + 1;
    }
    return 0;
}

auto InputField::findLineEnd(std::size_t pos) const -> std::size_t
{
    std::size_t i = pos;
    while (i < _buffer.size() && _buffer[i] != '\n')
        ++i;
    return i;
}

auto InputField::countGraphemesInRange(std::size_t start, std::size_t end) const -> int
{
    if (start >= end || start >= _buffer.size())
        return 0;

    auto const sv = std::string_view(_buffer).substr(start, end - start);
    auto segmenter = unicode::utf8_grapheme_segmenter(sv);
    int count = 0;
    for ([[maybe_unused]] auto const& cluster: segmenter)
        ++count;
    return count;
}

auto InputField::moveToGraphemeInLine(std::size_t lineStart, int graphemeIndex) const -> std::size_t
{
    auto const lineEnd = findLineEnd(lineStart);
    if (lineStart >= lineEnd)
        return lineStart;

    auto const sv = std::string_view(_buffer).substr(lineStart, lineEnd - lineStart);
    auto segmenter = unicode::utf8_grapheme_segmenter(sv);

    int currentIndex = 0;
    for (auto it = segmenter.begin(); it != segmenter.end(); ++it)
    {
        if (currentIndex >= graphemeIndex)
        {
            auto const offset = static_cast<std::size_t>(it._clusterStart - sv.data());
            return lineStart + offset;
        }
        ++currentIndex;
    }

    // Grapheme index exceeds line length, return end of line
    return lineEnd;
}

void InputField::moveToBufferStart()
{
    _cursor = 0;
}

void InputField::moveToBufferEnd()
{
    _cursor = _buffer.size();
}

void InputField::moveToLineStart()
{
    _cursor = findLineStart(_cursor);
}

void InputField::moveToLineEnd()
{
    _cursor = findLineEnd(_cursor);
}

void InputField::moveUp()
{
    auto const currentLineNum = cursorLine();
    if (currentLineNum == 0)
    {
        // Already on first line, delegate to history
        historyPrev();
        return;
    }

    // Remember current column (in graphemes)
    auto const column = cursorColumn();

    // Find start of current line, then go back to previous line
    auto const currentLineStart = findLineStart(_cursor);
    auto const prevLineEnd = currentLineStart > 0 ? currentLineStart - 1 : 0;
    auto const prevLineStart = findLineStart(prevLineEnd);

    // Move to same column in previous line (or end if line is shorter)
    _cursor = moveToGraphemeInLine(prevLineStart, column);
}

void InputField::moveDown()
{
    auto const currentLineNum = cursorLine();
    auto const totalLines = lineCount();
    if (currentLineNum >= totalLines - 1)
    {
        // Already on last line, delegate to history
        historyNext();
        return;
    }

    // Remember current column (in graphemes)
    auto const column = cursorColumn();

    // Find end of current line, then go to next line
    auto const currentLineEnd = findLineEnd(_cursor);
    auto const nextLineStart = currentLineEnd < _buffer.size() ? currentLineEnd + 1 : _buffer.size();

    // Move to same column in next line (or end if line is shorter)
    _cursor = moveToGraphemeInLine(nextLineStart, column);
}

void InputField::insertNewline()
{
    if (!_multiline)
        return;

    // Check max lines limit
    if (_maxLines > 0 && lineCount() >= _maxLines)
        return;

    _buffer.insert(_cursor, 1, '\n');
    ++_cursor;
}

void InputField::setCursorFromClick(int line, int column, bool extendSelection)
{
    // Clamp line to valid range
    auto const totalLines = lineCount();
    if (line < 0)
        line = 0;
    if (line >= totalLines)
        line = totalLines - 1;

    // Find the start of the target line
    std::size_t lineStart = 0;
    int currentLine = 0;
    while (currentLine < line && lineStart < _buffer.size())
    {
        if (_buffer[lineStart] == '\n')
            ++currentLine;
        ++lineStart;
    }

    // Clamp column to valid range within the line
    if (column < 0)
        column = 0;

    // Move to the target grapheme position within the line
    auto newCursor = moveToGraphemeInLine(lineStart, column);

    // Handle selection
    if (extendSelection)
        startOrExtendSelection();
    else
        clearSelection();

    _cursor = newCursor;
}

// ============================================================================
// Selection support (GUI-style)
// ============================================================================

auto InputField::hasSelection() const noexcept -> bool
{
    return _selectionAnchor.has_value() && *_selectionAnchor != _cursor;
}

auto InputField::selectionStart() const noexcept -> std::size_t
{
    if (!_selectionAnchor)
        return _cursor;
    return std::min(*_selectionAnchor, _cursor);
}

auto InputField::selectionEnd() const noexcept -> std::size_t
{
    if (!_selectionAnchor)
        return _cursor;
    return std::max(*_selectionAnchor, _cursor);
}

auto InputField::selectedText() const -> std::string_view
{
    if (!hasSelection())
        return {};
    return std::string_view(_buffer).substr(selectionStart(), selectionEnd() - selectionStart());
}

void InputField::clearSelection()
{
    _selectionAnchor.reset();
}

void InputField::selectAll()
{
    _selectionAnchor = 0;
    _cursor = _buffer.size();
}

void InputField::setClipboardCallback(ClipboardCallback callback)
{
    _clipboardCallback = std::move(callback);
}

auto InputField::copySelection() -> bool
{
    if (!hasSelection())
        return false;
    auto const text = selectedText();
    pushKillRing(std::string(text));
    // Also copy to system clipboard if callback is set
    if (_clipboardCallback)
        _clipboardCallback(text);
    return true;
}

auto InputField::cutSelection() -> bool
{
    if (!hasSelection())
        return false;
    auto const text = selectedText();
    pushKillRing(std::string(text));
    // Also copy to system clipboard if callback is set
    if (_clipboardCallback)
        _clipboardCallback(text);
    deleteSelection();
    return true;
}

void InputField::deleteSelection()
{
    if (!hasSelection())
        return;
    auto const start = selectionStart();
    auto const end = selectionEnd();
    _buffer.erase(start, end - start);
    _cursor = start;
    clearSelection();
}

void InputField::startOrExtendSelection()
{
    if (!_selectionAnchor)
        _selectionAnchor = _cursor;
}

void InputField::moveWithSelection(void (InputField::*move)())
{
    startOrExtendSelection();
    (this->*move)();
}

// ============================================================================
// Undo/Redo support
// ============================================================================

void InputField::saveUndoState()
{
    // Clear redo stack when new edit happens
    _redoStack.clear();

    // Save current state
    _undoStack.push_back(UndoState { .buffer = _buffer, .cursor = _cursor });

    // Limit undo history size
    if (_undoStack.size() > MaxUndoHistory)
        _undoStack.erase(_undoStack.begin());
}

auto InputField::undo() -> bool
{
    if (_undoStack.empty())
        return false;

    // Save current state to redo stack
    _redoStack.push_back(UndoState { .buffer = _buffer, .cursor = _cursor });

    // Restore previous state
    auto const& state = _undoStack.back();
    _buffer = state.buffer;
    _cursor = state.cursor;
    _undoStack.pop_back();

    clearSelection();
    return true;
}

auto InputField::redo() -> bool
{
    if (_redoStack.empty())
        return false;

    // Save current state to undo stack
    _undoStack.push_back(UndoState { .buffer = _buffer, .cursor = _cursor });

    // Restore redo state
    auto const& state = _redoStack.back();
    _buffer = state.buffer;
    _cursor = state.cursor;
    _redoStack.pop_back();

    clearSelection();
    return true;
}

auto InputField::canUndo() const noexcept -> bool
{
    return !_undoStack.empty();
}

auto InputField::canRedo() const noexcept -> bool
{
    return !_redoStack.empty();
}

void InputField::clearUndoHistory()
{
    _undoStack.clear();
    _redoStack.clear();
}

} // namespace tui
