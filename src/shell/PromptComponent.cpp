// SPDX-License-Identifier: Apache-2.0
#include "PromptComponent.hpp"

#include <algorithm>

#include "Completer.hpp"
#include <tui/Canvas.hpp>
#include <tui/Theme.hpp>

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

PromptComponent::PromptComponent()
{
    _inputField.setPrompt(""); // We handle prompt rendering ourselves
}

void PromptComponent::render(tui::Canvas& canvas)
{
    auto const canvasWidth = canvas.width();
    auto const totalLines = _inputField.lineCount();
    auto const promptTextWidth = displayWidth(_promptStr);
    auto const totalPromptWidth = HorizontalMargin + LeftBarWidth + PaddingAfterBar + promptTextWidth;

    // Effective content width (excluding margins)
    auto const contentWidth = canvasWidth - 2 * HorizontalMargin;

    // Create styles
    tui::Style bgStyle;
    bgStyle.bg = BackgroundColor;

    tui::Style leftBarStyle;
    leftBarStyle.fg = LeftBarColor;
    leftBarStyle.bg = BackgroundColor;

    tui::Style promptStyle;
    promptStyle.fg = PromptTextColor;
    promptStyle.bg = BackgroundColor;

    tui::Style textStyle;
    textStyle.fg = InputTextColor;
    textStyle.bg = BackgroundColor;

    tui::Style ghostStyle;
    ghostStyle.fg = PromptTextColor;
    ghostStyle.bg = BackgroundColor;
    ghostStyle.dim = true;

    tui::Style selectionStyle = textStyle;
    selectionStyle.inverse = true;

    // Get selection bounds
    auto const hasSelection = _inputField.hasSelection();
    auto const selStart = _inputField.selectionStart();
    auto const selEnd = _inputField.selectionEnd();

    // Render each line
    for (int lineIndex = 0; lineIndex < totalLines && lineIndex < canvas.height(); ++lineIndex)
    {
        auto const lineContent = _inputField.lineAt(lineIndex);

        // Fill content area with background (with margins)
        canvas.fill(tui::Rect { HorizontalMargin, lineIndex, contentWidth, 1 }, ' ', bgStyle);

        // Draw left bar (thin vertical bar) after left margin
        canvas.put(
            lineIndex, HorizontalMargin, "\xe2\x96\x8e", leftBarStyle); // U+258E LEFT ONE QUARTER BLOCK

        // Padding after bar
        canvas.put(lineIndex, HorizontalMargin + 1, " ", bgStyle);

        // Prompt or continuation indicator
        auto col = HorizontalMargin + LeftBarWidth + PaddingAfterBar;
        if (lineIndex == 0)
        {
            col += canvas.putString(lineIndex, col, _promptStr, promptStyle);
        }
        else
        {
            // Continuation indicator: spaces + middle dots
            for (int i = 0; i < promptTextWidth - 2; ++i)
                canvas.put(lineIndex, col++, " ", bgStyle);
            col += canvas.putString(lineIndex, col, "\xc2\xb7\xc2\xb7", promptStyle); // ..
        }

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
                col += canvas.putString(lineIndex, col, lineContent.substr(0, lineSelStart), textStyle);
            }

            // Selected text
            if (lineSelEnd > lineSelStart)
            {
                col += canvas.putString(lineIndex,
                                        col,
                                        lineContent.substr(lineSelStart, lineSelEnd - lineSelStart),
                                        selectionStyle);
            }

            // Text after selection
            if (lineSelEnd < lineContent.size())
            {
                col += canvas.putString(lineIndex, col, lineContent.substr(lineSelEnd), textStyle);
            }
        }
        else
        {
            // No selection on this line
            col += canvas.putString(lineIndex, col, lineContent, textStyle);
        }

        // Ghost text on last line
        if (!_inputField.ghostText().empty() && lineIndex == totalLines - 1)
        {
            canvas.putString(lineIndex, col, _inputField.ghostText(), ghostStyle);
        }
    }

    // Position cursor
    auto const cursorLine = _inputField.cursorLine();
    auto const cursorColumn = _inputField.cursorColumn();

    // Calculate cursor display position (including left margin)
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
            clusterWidth += unicode::width(cp);
        displayCol += std::max(1, clusterWidth);
        ++graphemeIndex;
    }

    canvas.setCursor(cursorLine, displayCol);

    // Render completion popup if visible (as overlay in bottom-right area)
    if (_completionPopup.visible())
    {
        auto popupSize = _completionPopup.preferredSize();
        auto popupRect =
            tui::Rect { cursorLine + 1, // Below cursor
                        totalPromptWidth,
                        std::min(popupSize.width, canvasWidth - totalPromptWidth - HorizontalMargin),
                        std::min(popupSize.height, canvas.height() - cursorLine - 1) };

        if (popupRect.height > 0)
        {
            _completionPopup.setArea(popupRect);
            auto popupCanvas = canvas.subcanvas(popupRect);
            _completionPopup.render(popupCanvas);
        }
    }
}

tui::EventResult PromptComponent::onEvent(tui::InputEvent const& event)
{
    auto action = processInput(event);
    if (action != Action::None)
    {
        invalidate();
        return tui::EventResult::Handled;
    }
    return tui::EventResult::Ignored;
}

tui::Size PromptComponent::preferredSize() const
{
    auto const lineCount = _inputField.lineCount();
    auto const promptWidth = this->promptWidth();

    // Calculate max line width
    int maxWidth = 0;
    for (int i = 0; i < lineCount; ++i)
    {
        auto const lineContent = _inputField.lineAt(i);
        maxWidth = std::max(maxWidth, promptWidth + displayWidth(lineContent));
    }

    return { maxWidth, lineCount };
}

void PromptComponent::setPrompt(std::string_view prompt)
{
    _promptStr = std::string(prompt);
}

int PromptComponent::promptWidth() const
{
    return LeftBarWidth + PaddingAfterBar + displayWidth(_promptStr);
}

int PromptComponent::displayWidth(std::string_view text)
{
    int width = 0;
    auto segmenter = unicode::utf8_grapheme_segmenter(text);
    for (auto const& cluster: segmenter)
    {
        int clusterWidth = 0;
        for (char32_t cp: cluster)
            clusterWidth += unicode::width(cp);
        width += std::max(1, clusterWidth);
    }
    return width;
}

PromptComponent::Action PromptComponent::processInput(tui::InputEvent const& event)
{
    // Handle completion popup events first
    if (_completionPopup.visible())
    {
        auto completionResult = _completionPopup.processEvent(event);
        switch (completionResult)
        {
            case tui::CompletionAction::Changed: return Action::Changed;
            case tui::CompletionAction::Accepted:
                if (auto const* selected = _completionPopup.selectedItem())
                    insertCompletion(selected->text);
                _completionPopup.hide();
                return Action::Changed;
            case tui::CompletionAction::Dismissed: _completionPopup.hide(); return Action::Changed;
            case tui::CompletionAction::None:
                // Close popup and continue processing
                _completionPopup.hide();
                break;
        }
    }

    // Handle key events with special completion handling
    if (auto const* key = std::get_if<tui::KeyEvent>(&event))
    {
        // Tab triggers completion
        if (key->key == tui::KeyCode::Tab && key->modifiers == tui::Modifier::None)
        {
            triggerCompletion();
            return Action::Changed;
        }

        // Ctrl+Space triggers completion
        if (key->codepoint == ' ' && tui::hasModifier(key->modifiers, tui::Modifier::Ctrl))
        {
            triggerCompletion();
            return Action::Changed;
        }

        // Right arrow or End at end of line accepts ghost text
        if (_inputField.hasGhostText() && _inputField.cursor() == _inputField.text().size())
        {
            if (key->key == tui::KeyCode::Right || key->key == tui::KeyCode::End)
            {
                _inputField.acceptGhostText();
                updateGhostText();
                return Action::Changed;
            }
        }
    }

    // Process through InputField
    auto action = _inputField.processEvent(event);

    switch (action)
    {
        case tui::InputFieldAction::Submit: _completionPopup.hide(); return Action::Submit;
        case tui::InputFieldAction::Abort: _completionPopup.hide(); return Action::Abort;
        case tui::InputFieldAction::Eof: _completionPopup.hide(); return Action::Eof;
        case tui::InputFieldAction::Changed: updateGhostText(); return Action::Changed;
        case tui::InputFieldAction::None: break;
    }

    return Action::None;
}

void PromptComponent::updateGhostText()
{
    if (!_completer)
    {
        _inputField.clearGhostText();
        return;
    }

    auto const text = _inputField.text();
    auto const cursor = _inputField.cursor();

    // Only show ghost text when cursor is at end of input
    if (cursor != text.size())
    {
        _inputField.clearGhostText();
        return;
    }

    // Get suggestion from completer
    auto suggestion = _completer->suggest(text, cursor);
    if (suggestion)
        _inputField.setGhostText(*suggestion);
    else
        _inputField.clearGhostText();
}

void PromptComponent::triggerCompletion()
{
    if (!_completer)
        return;

    auto const text = _inputField.text();
    auto const cursor = _inputField.cursor();

    // Get completions
    auto completions = _completer->complete(text, cursor);

    if (completions.empty())
    {
        _completionPopup.hide();
        return;
    }

    if (completions.size() == 1)
    {
        // Single match: insert directly without showing popup
        insertCompletion(completions[0].text);
        _completionPopup.hide();
        return;
    }

    // Multiple matches: populate and show popup
    std::vector<tui::CompletionItem> popupItems;
    popupItems.reserve(completions.size());
    for (auto const& item: completions)
    {
        popupItems.push_back(tui::CompletionItem {
            .text = item.text,
            .displayText = item.displayText.empty() ? item.text : item.displayText,
            .description = item.description,
            .score = item.score,
        });
    }
    _completionPopup.show(std::move(popupItems));
}

void PromptComponent::insertCompletion(std::string_view text)
{
    if (!_completer)
        return;

    auto const inputText = _inputField.text();
    auto const cursor = _inputField.cursor();

    // Get the context to find what prefix to replace
    auto ctx = _completer->analyzeContext(inputText, cursor);

    // Calculate how much text to replace (the prefix being completed)
    auto const prefixLen = ctx.prefix.size();

    // Build new buffer: text before prefix + completion + text after cursor
    std::string newBuffer;
    newBuffer.reserve(inputText.size() - prefixLen + text.size());
    newBuffer.append(inputText.substr(0, cursor - prefixLen));
    newBuffer.append(text);
    newBuffer.append(inputText.substr(cursor));

    // Update the input field
    _inputField.setText(newBuffer);
}

} // namespace endo
