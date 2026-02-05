// SPDX-License-Identifier: Apache-2.0
#include "Prompt.hpp"

#include <algorithm>

#include <unistd.h>

#if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wold-style-cast"
#endif
#include <libunicode/utf8_grapheme_segmenter.h>
#include <libunicode/width.h>
#if defined(__clang__)
    #pragma clang diagnostic pop
#endif

namespace endo
{

Prompt::Prompt() = default;

Prompt::~Prompt()
{
    if (_initialized)
        _terminal.shutdown();
}

bool Prompt::ready() const
{
    return !_aborted;
}

void Prompt::initialize()
{
    if (_initialized)
        return;

    if (auto result = _terminal.initialize(); !result)
    {
        // Fall back to non-TUI mode if terminal initialization fails
        _aborted = true;
        return;
    }

    // Ensure any protocol enable sequences are flushed
    // (they're written directly via write() in TerminalInput::initialize())
    // This is needed because some terminals may buffer output
    _terminal.output().flush();

    // Set up clipboard callback to use OSC 52
    _inputField.setClipboardCallback([this](std::string_view text) {
        _terminal.output().copyToClipboard(text);
        _terminal.output().flush();
    });

    _inputField.setPrompt(_promptStr);
    _inputField.setMultiline(_multilineEnabled);
    _initialized = true;
}

std::string Prompt::read()
{
    initialize();
    if (_aborted)
        return {};

    // Clear any previous input
    _inputField.clear();
    _inputField.setPrompt(_promptStr);
    _inputField.setMultiline(_multilineEnabled);
    _lastRenderedLines = 1;

    // Render the initial prompt immediately
    render();

    // Event loop
    while (ready())
    {
        auto events = _terminal.poll(-1); // Block until input

        for (auto const& event: events)
        {
            // Handle resize events
            if (std::holds_alternative<tui::ResizeEvent>(event))
            {
                onResize();
                render();
                continue;
            }

            auto action = _inputField.processEvent(event);

            switch (action)
            {
                case tui::InputFieldAction::Submit: {
                    // Move cursor past the editor region before returning
                    auto& out = _terminal.output();
                    auto const currentLine = _inputField.cursorLine();
                    auto const totalLines = _inputField.lineCount();
                    auto const linesToMoveDown = totalLines - currentLine;
                    if (linesToMoveDown > 0)
                        out.moveDown(linesToMoveDown);
                    out.writeRaw("\r\n");
                    out.flush();
                    auto result = std::string(_inputField.text());
                    return result;
                }
                case tui::InputFieldAction::Abort:
                    // Ctrl+C - clear line and return empty
                    _terminal.output().writeRaw("^C\r\n");
                    _terminal.output().flush();
                    _inputField.clear();
                    return {};
                case tui::InputFieldAction::Eof:
                    // Ctrl+D on empty line - signal EOF
                    _terminal.output().writeRaw("\r\n");
                    _terminal.output().flush();
                    _aborted = true;
                    return {};
                case tui::InputFieldAction::Changed: render(); break;
                case tui::InputFieldAction::None: break;
            }
        }
    }

    return {};
}

void Prompt::setPrompt(std::string_view promptStr)
{
    _promptStr = std::string(promptStr);
    if (_initialized)
        _inputField.setPrompt(_promptStr);
}

void Prompt::addHistory(std::string entry)
{
    _inputField.addHistory(std::move(entry));
}

int Prompt::inputFd() const noexcept
{
    return STDIN_FILENO;
}

std::optional<std::string> Prompt::processInput()
{
    initialize();
    if (_aborted)
        return std::nullopt;

    auto events = _terminal.poll(0); // Non-blocking

    for (auto const& event: events)
    {
        if (std::holds_alternative<tui::ResizeEvent>(event))
        {
            onResize();
            render();
            continue;
        }

        auto action = _inputField.processEvent(event);

        switch (action)
        {
            case tui::InputFieldAction::Submit: {
                auto& out = _terminal.output();
                auto const currentLine = _inputField.cursorLine();
                auto const totalLines = _inputField.lineCount();
                auto const linesToMoveDown = totalLines - currentLine;
                if (linesToMoveDown > 0)
                    out.moveDown(linesToMoveDown);
                out.writeRaw("\r\n");
                out.flush();
                auto result = std::string(_inputField.text());
                _inputField.clear();
                return result;
            }
            case tui::InputFieldAction::Abort:
                _terminal.output().writeRaw("^C\r\n");
                _terminal.output().flush();
                _inputField.clear();
                return std::string {}; // Return empty string, not nullopt
            case tui::InputFieldAction::Eof:
                _terminal.output().writeRaw("\r\n");
                _terminal.output().flush();
                _aborted = true;
                return std::string {};
            case tui::InputFieldAction::Changed: render(); break;
            case tui::InputFieldAction::None: break;
        }
    }

    return std::nullopt;
}

void Prompt::onResize()
{
    _terminal.output().updateDimensions();
}

void Prompt::display()
{
    initialize();
    if (_aborted)
        return;

    // Prepare the input field
    _inputField.clear();
    _inputField.setPrompt(_promptStr);
    _inputField.setMultiline(_multilineEnabled);
    _lastRenderedLines = 1;

    // Render the prompt
    render();
}

void Prompt::setMultilineEnabled(bool enable)
{
    _multilineEnabled = enable;
    if (_initialized)
        _inputField.setMultiline(enable);
}

bool Prompt::isMultilineEnabled() const noexcept
{
    return _multilineEnabled;
}

int Prompt::displayWidth(std::string_view text)
{
    int width = 0;
    auto segmenter = unicode::utf8_grapheme_segmenter(text);
    for (auto const& cluster: segmenter)
    {
        // Get the width of the first codepoint in the cluster
        // (grapheme clusters are typically one cell wide, but some are 2)
        if (!cluster.empty())
        {
            // Decode the first UTF-8 codepoint from the cluster
            auto const* p = reinterpret_cast<unsigned char const*>(cluster.data());
            char32_t cp = 0;
            if ((*p & 0x80) == 0)
            {
                cp = *p;
            }
            else if ((*p & 0xE0) == 0xC0)
            {
                cp = (*p & 0x1F) << 6;
                cp |= (*(p + 1) & 0x3F);
            }
            else if ((*p & 0xF0) == 0xE0)
            {
                cp = (*p & 0x0F) << 12;
                cp |= (*(p + 1) & 0x3F) << 6;
                cp |= (*(p + 2) & 0x3F);
            }
            else if ((*p & 0xF8) == 0xF0)
            {
                cp = (*p & 0x07) << 18;
                cp |= (*(p + 1) & 0x3F) << 12;
                cp |= (*(p + 2) & 0x3F) << 6;
                cp |= (*(p + 3) & 0x3F);
            }
            width += unicode::width(cp);
        }
    }
    return width;
}

int Prompt::maxEditorHeight() const
{
    return std::max(1, _terminal.rows() / 2);
}

void Prompt::render()
{
    auto& out = _terminal.output();

    auto const totalLines = _inputField.lineCount();
    auto const maxHeight = maxEditorHeight();
    auto const visibleLines = std::min(totalLines, maxHeight);
    auto const cursorLine = _inputField.cursorLine();
    auto const cursorColumn = _inputField.cursorColumn();
    auto const termWidth = std::max(80, _terminal.columns()); // Ensure minimum width

    // Style constants - OpenCode-inspired look
    // Soft blue for the left bar: RGB(97, 175, 239)
    // Soft gray background: RGB(45, 50, 55)
    static constexpr auto LeftBarFg = "\033[38;2;97;175;239m"; // Soft blue foreground
    static constexpr auto BgColor = "\033[48;2;45;50;55m";     // Soft gray background
    static constexpr auto PromptFg = "\033[38;2;180;180;180m"; // Light gray for prompt text
    static constexpr auto TextFg = "\033[38;2;220;220;220m";   // Slightly brighter for input text
    static constexpr auto Reset = "\033[0m";
    static constexpr auto InverseOn = "\033[7m";
    static constexpr auto InverseOff = "\033[27m";

    // Left bar character (thin vertical bar)
    static constexpr auto LeftBar = "\xe2\x96\x8e"; // ▎ (U+258E LEFT ONE QUARTER BLOCK)
    static constexpr int LeftBarWidth = 1;
    static constexpr int PaddingAfterBar = 1; // Space after bar

    // Calculate effective prompt width (bar + padding + prompt text)
    auto const promptTextWidth = displayWidth(_promptStr);
    auto const totalPromptWidth = LeftBarWidth + PaddingAfterBar + promptTextWidth;

    // Calculate scroll offset to keep cursor visible
    static int scrollOffset = 0;
    if (cursorLine < scrollOffset)
        scrollOffset = cursorLine;
    else if (cursorLine >= scrollOffset + visibleLines)
        scrollOffset = cursorLine - visibleLines + 1;

    // Get selection bounds
    auto const hasSelection = _inputField.hasSelection();
    auto const selStart = _inputField.selectionStart();
    auto const selEnd = _inputField.selectionEnd();

    // Clear previously rendered lines (move up and clear)
    if (_lastRenderedLines > 1)
    {
        out.moveUp(_lastRenderedLines - 1);
    }

    // Render each visible line
    for (int displayRow = 0; displayRow < visibleLines; ++displayRow)
    {
        auto const lineIndex = scrollOffset + displayRow;
        auto const lineContent = _inputField.lineAt(lineIndex);

        // Move to start of line and clear it
        out.writeRaw("\r");
        out.clearToEndOfLine();

        // Start the styled line
        out.writeRaw(BgColor);

        // Draw the left bar (soft blue)
        out.writeRaw(LeftBarFg);
        out.writeRaw(LeftBar);

        // Padding after bar
        out.writeRaw(" ");

        // Write prompt on first line, or continuation indicator on subsequent lines
        if (lineIndex == 0)
        {
            out.writeRaw(PromptFg);
            out.writeRaw(_promptStr);
        }
        else
        {
            // Continuation indicator (dots or spaces to align)
            out.writeRaw(PromptFg);
            for (int i = 0; i < promptTextWidth - 2; ++i)
                out.writeRaw(" ");
            out.writeRaw("\xc2\xb7\xc2\xb7"); // ·· (middle dots as continuation indicator)
        }

        // Switch to text color
        out.writeRaw(TextFg);

        // Calculate byte offset of this line's start in the buffer
        std::size_t lineStartByte = 0;
        {
            auto const text = _inputField.text();
            std::size_t pos = 0;
            int currentLine = 0;
            while (pos < text.size() && currentLine < lineIndex)
            {
                if (text[pos] == '\n')
                    ++currentLine;
                ++pos;
            }
            lineStartByte = pos;
        }
        auto const lineEndByte = lineStartByte + lineContent.size();

        // Render line content with selection highlighting
        if (hasSelection && selStart < lineEndByte && selEnd > lineStartByte)
        {
            // This line has some selection
            auto const lineSelStart = std::max(selStart, lineStartByte) - lineStartByte;
            auto const lineSelEnd = std::min(selEnd, lineEndByte) - lineStartByte;

            // Text before selection
            if (lineSelStart > 0)
            {
                out.writeRaw(lineContent.substr(0, lineSelStart));
            }

            // Selected text (inverse video)
            if (lineSelEnd > lineSelStart)
            {
                out.writeRaw(InverseOn);
                out.writeRaw(lineContent.substr(lineSelStart, lineSelEnd - lineSelStart));
                out.writeRaw(InverseOff);
            }

            // Text after selection
            if (lineSelEnd < lineContent.size())
            {
                out.writeRaw(lineContent.substr(lineSelEnd));
            }
        }
        else
        {
            // No selection on this line
            out.writeRaw(lineContent);
        }

        // Fill rest of line with background color for clean look
        auto const contentWidth = totalPromptWidth + displayWidth(lineContent);
        auto const remainingWidth = termWidth - contentWidth;
        if (remainingWidth > 0)
        {
            for (int i = 0; i < remainingWidth; ++i)
                out.writeRaw(" ");
        }

        // Reset styling at end of line
        out.writeRaw(Reset);

        // Move to next line (except for the last one)
        if (displayRow < visibleLines - 1)
        {
            out.writeRaw("\r\n");
        }
    }

    // Position cursor
    auto const cursorDisplayRow = cursorLine - scrollOffset;

    // Move cursor to correct row
    auto const rowsToMoveUp = (visibleLines - 1) - cursorDisplayRow;
    if (rowsToMoveUp > 0)
        out.moveUp(rowsToMoveUp);

    // Move cursor to correct column within the line
    out.writeRaw("\r");

    // Calculate display position: bar + padding + prompt + text before cursor
    auto const lineContent = _inputField.lineAt(cursorLine);
    int displayCol = totalPromptWidth;

    // Count display width up to cursor column (in graphemes)
    auto segmenter = unicode::utf8_grapheme_segmenter(lineContent);
    int graphemeIndex = 0;
    for (auto const& cluster: segmenter)
    {
        if (graphemeIndex >= cursorColumn)
            break;
        int clusterWidth = 0;
        for (char32_t cp: cluster)
        {
            clusterWidth += unicode::width(cp);
        }
        displayCol += std::max(1, clusterWidth);
        ++graphemeIndex;
    }

    if (displayCol > 0)
        out.moveRight(displayCol);

    _lastRenderedLines = visibleLines;

    // Flush output to terminal
    out.flush();
}

} // namespace endo
