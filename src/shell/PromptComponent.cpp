// SPDX-License-Identifier: Apache-2.0
#include "PromptComponent.hpp"

#include <algorithm>

#include "CommandResolver.hpp"
#include "Completer.hpp"
#include "SourceOffsetUtils.hpp"
#include "SyntaxHighlighter.hpp"
#include <endo-language/HoverProvider.hpp>
#include <tui/Canvas.hpp>
#include <tui/Screen.hpp>
#include <tui/Theme.hpp>
#include <tui/Unicode.hpp>

#if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wold-style-cast"
#endif
#include <libunicode/utf8_grapheme_segmenter.h>
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

    tui::Style ghostStyle;
    ghostStyle.fg = PromptTextColor;
    ghostStyle.bg = BackgroundColor;
    ghostStyle.dim = true;

    // Get selection bounds
    auto const hasSelection = _inputField.hasSelection();
    auto const selStart = _inputField.selectionStart();
    auto const selEnd = _inputField.selectionEnd();

    // Compute syntax highlighting for the full input text
    auto const fullText = _inputField.text();
    auto const highlightMap = computeHighlightMap(fullText);

    // Compute diagnostics and build per-byte error map
    updateDiagnostics();
    auto errorMap = std::vector<bool>(fullText.size(), false);
    if (!_diagnostics.empty())
    {
        auto const lineStarts = buildLineStartOffsets(fullText);
        for (auto const& diag: _diagnostics)
        {
            if (diag.severity != DiagnosticSeverity::Error && diag.severity != DiagnosticSeverity::Warning)
                continue;
            auto const startByte = positionToByteOffset(fullText, lineStarts, diag.range.start);
            auto const endByte = positionToByteOffset(fullText, lineStarts, diag.range.end);
            for (auto i = startByte; i < endByte && i < errorMap.size(); ++i)
                errorMap[i] = true;
        }
    }

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

        // Render line content with syntax highlighting, selection, and error underlines
        {
            // Determine selection range local to this line
            auto const lineSelStart = (hasSelection && selStart < lineEndByte && selEnd > lineStartByte)
                                          ? std::max(selStart, lineStartByte) - lineStartByte
                                          : lineContent.size();
            auto const lineSelEnd = (hasSelection && selStart < lineEndByte && selEnd > lineStartByte)
                                        ? std::min(selEnd, lineEndByte) - lineStartByte
                                        : lineContent.size();

            // Iterate line content, grouping consecutive bytes with same category, selection, and error state
            std::size_t segStart = 0;
            while (segStart < lineContent.size())
            {
                auto const globalByte = lineStartByte + segStart;
                auto const cat =
                    (globalByte < highlightMap.size()) ? highlightMap[globalByte] : TokenCategory::Default;
                auto const selected = segStart >= lineSelStart && segStart < lineSelEnd;
                auto const hasError = globalByte < errorMap.size() && errorMap[globalByte];

                // Extend segment while category, selection state, and error state remain the same
                auto segEnd = segStart + 1;
                while (segEnd < lineContent.size())
                {
                    auto const gb = lineStartByte + segEnd;
                    auto const nextCat =
                        (gb < highlightMap.size()) ? highlightMap[gb] : TokenCategory::Default;
                    auto const nextSel = segEnd >= lineSelStart && segEnd < lineSelEnd;
                    auto const nextErr = gb < errorMap.size() && errorMap[gb];
                    if (nextCat != cat || nextSel != selected || nextErr != hasError)
                        break;
                    ++segEnd;
                }

                // Build style for this segment
                tui::Style segStyle;
                segStyle.fg = categoryColor(cat);
                segStyle.bg = BackgroundColor;
                segStyle.inverse = selected;

                // Apply curly red underline for error regions
                if (hasError)
                {
                    segStyle.underlineStyle = tui::UnderlineStyle::Curly;
                    segStyle.underlineColor = tui::RgbColor { .r = 255, .g = 85, .b = 85 };
                }

                col += canvas.putString(
                    lineIndex, col, lineContent.substr(segStart, segEnd - segStart), segStyle);
                segStart = segEnd;
            }
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
        displayCol += tui::graphemeClusterWidth(cluster);
        ++graphemeIndex;
    }

    canvas.setCursor(cursorLine, displayCol);

    // Render completion popup if visible
    if (_completionPopup.visible())
    {
        auto popupSize = _completionPopup.preferredSize();
        int availableBelow = canvas.height() - cursorLine - 1;
        int availableAbove = cursorLine;

        bool renderBelow = true;

        // In fullscreen/fixed mode, choose direction based on available space
        // In inline mode, always render below (Screen handles scrolling via preferredSize)
        if (auto* scr = screen(); scr && scr->viewport() != tui::Viewport::Inline)
        {
            // Prefer below, but use above if below has < 3 rows and above has more space
            if (availableBelow < 3 && availableAbove > availableBelow)
                renderBelow = false;
        }

        int popupRow = renderBelow ? (cursorLine + 1)
                                   : std::max(0, cursorLine - std::min(popupSize.height, availableAbove));
        int popupHeight = renderBelow ? std::min(popupSize.height, std::max(0, availableBelow))
                                      : std::min(popupSize.height, availableAbove);

        if (popupHeight >= 3) // Minimum: border (2) + 1 item
        {
            // Rect constructor: {x, y, width, height} where x=column, y=row
            auto popupRect =
                tui::Rect { totalPromptWidth, // x (column) - where prompt ends
                            popupRow,         // y (row) - below or above cursor
                            std::min(popupSize.width, canvasWidth - totalPromptWidth - HorizontalMargin),
                            popupHeight };

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

    // If completion popup is visible, add space for it below the input
    int totalHeight = lineCount;
    if (_completionPopup.visible())
    {
        auto popupSize = _completionPopup.preferredSize();
        totalHeight += popupSize.height;
        maxWidth = std::max(maxWidth, promptWidth + popupSize.width);
    }

    return { maxWidth, totalHeight };
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
        width += tui::graphemeClusterWidth(cluster);
    return width;
}

PromptComponent::Action PromptComponent::processInput(tui::InputEvent const& event)
{
    // Hide tooltip when user starts typing
    if (std::holds_alternative<tui::KeyEvent>(event))
    {
        if (auto* scr = screen())
            scr->hideTooltip();
    }

    // Track if popup was visible before processing (for dynamic filtering)
    bool const popupWasVisible = _completionPopup.visible();
    bool popupDismissedByTyping = false;

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
                updateGhostText(); // Clear/update ghost text after completion
                return Action::Changed;
            case tui::CompletionAction::Dismissed:
                // Don't hide yet - let event pass through and potentially re-filter
                popupDismissedByTyping = true;
                break;
        }
    }

    // Handle key events with special completion handling
    if (auto const* key = std::get_if<tui::KeyEvent>(&event))
    {
        // Tab triggers completion (double-Tab forces popup to show)
        if (key->key == tui::KeyCode::Tab && key->modifiers == tui::Modifier::None)
        {
            auto const now = std::chrono::steady_clock::now();
            bool const isDoubleTab = (now - _lastTabTime) < DoubleTabThreshold;
            _lastTabTime = now;
            triggerCompletion(isDoubleTab);
            return Action::Changed;
        }

        // Ctrl+Space triggers completion (always shows popup)
        if (key->codepoint == ' ' && tui::hasModifier(key->modifiers, tui::Modifier::Ctrl))
        {
            triggerCompletion(true);
            return Action::Changed;
        }

        // Ctrl+L clears the screen
        if (key->codepoint == 'l' && tui::hasModifier(key->modifiers, tui::Modifier::Ctrl))
        {
            return Action::ClearScreen;
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
        case tui::InputFieldAction::Submit:
            _inputField.clearGhostText();
            _completionPopup.hide();
            return Action::Submit;
        case tui::InputFieldAction::Abort:
            _inputField.clearGhostText();
            _completionPopup.hide();
            return Action::Abort;
        case tui::InputFieldAction::Eof:
            _inputField.clearGhostText();
            _completionPopup.hide();
            return Action::Eof;
        case tui::InputFieldAction::Changed:
            updateGhostText();
            // If popup was visible and dismissed by typing, re-filter instead of hiding
            if (popupWasVisible && popupDismissedByTyping)
                updateCompletionPopup();
            return Action::Changed;
        case tui::InputFieldAction::None:
            // If dismissed but text didn't change (e.g., Escape), hide popup
            if (popupDismissedByTyping)
                _completionPopup.hide();
            break;
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

void PromptComponent::triggerCompletion(bool forceShowPopup)
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

    if (completions.size() == 1 && !forceShowPopup)
    {
        // Single match: insert directly without showing popup
        // (unless force-show was requested via double-Tab or Ctrl+Space)
        insertCompletion(completions[0].text);
        _completionPopup.hide();
        updateGhostText(); // Clear/update ghost text after completion
        return;
    }

    // Multiple matches (or force-show): populate and show popup
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

void PromptComponent::updateCompletionPopup()
{
    if (!_completer)
    {
        _completionPopup.hide();
        return;
    }

    auto const text = _inputField.text();
    auto const cursor = _inputField.cursor();

    // Get filtered completions
    auto completions = _completer->complete(text, cursor);

    if (completions.empty())
    {
        _completionPopup.hide(); // Auto-close on 0 matches
        return;
    }

    // Convert to popup items
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

    // Update with selection preservation
    _completionPopup.updateItems(std::move(popupItems));
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

void PromptComponent::onHoverConfirmed(int x, int y)
{
    auto* scr = screen();
    if (!scr)
        return;

    auto const bounds = screenBounds();

    // Convert screen coordinates to source position
    auto const sourcePos = screenToSourcePosition(x, y);

    // Priority 1: Check diagnostics at this position
    if (sourcePos)
    {
        if (auto diag = diagnosticAt(sourcePos->line, sourcePos->character))
        {
            tui::Point tooltipPos { bounds.x + x, bounds.y + y + 1 };
            scr->showTooltip(diag->message, tooltipPos, tui::TooltipContentType::PlainText);
            return;
        }
    }

    // Priority 2: Check language hover info (keywords, constructors, operators, builtins, bindings)
    if (sourcePos)
    {
        auto const text = std::string(_inputField.text());
        if (auto hover = endo::computeHover(text, *sourcePos))
        {
            tui::Point tooltipPos { bounds.x + x, bounds.y + y + 1 };
            scr->showTooltip(hover->markdownText, tooltipPos, tui::TooltipContentType::Markdown);
            return;
        }
    }

    // Priority 3: Fall through to existing command hover logic (line 0 only)
    if (y == 0 && _commandResolver)
    {
        auto const cmd = getCommandAtColumn(x);
        if (cmd)
        {
            auto const info = _commandResolver->resolve(*cmd);
            auto const [cmdStart, cmdEnd] = getCommandBounds();
            tui::Point tooltipPos { bounds.x + cmdStart, bounds.y + 1 };
            scr->showTooltip(info.tooltip, tooltipPos, tui::TooltipContentType::PlainText);
        }
    }
}

void PromptComponent::onHoverLeave()
{
    if (auto* scr = screen())
    {
        scr->hideTooltip();
    }
}

std::optional<std::string> PromptComponent::getCommandAtColumn(int screenColumn) const
{
    // Calculate the prompt prefix width
    auto const totalPromptWidth =
        HorizontalMargin + LeftBarWidth + PaddingAfterBar + displayWidth(_promptStr);

    // Check if column is within command bounds
    auto const [cmdStart, cmdEnd] = getCommandBounds();
    if (screenColumn < cmdStart || screenColumn >= cmdEnd)
        return std::nullopt;

    // Extract the command from input text
    auto const text = _inputField.text();
    if (text.empty())
        return std::nullopt;

    // Skip leading whitespace to find command start
    auto pos = std::size_t { 0 };
    while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t'))
        ++pos;

    if (pos >= text.size())
        return std::nullopt;

    // Find command end (first whitespace, pipe, semicolon, etc.)
    auto cmdEndPos = pos;
    while (cmdEndPos < text.size() && text[cmdEndPos] != ' ' && text[cmdEndPos] != '\t'
           && text[cmdEndPos] != '|' && text[cmdEndPos] != ';' && text[cmdEndPos] != '&'
           && text[cmdEndPos] != '\n' && text[cmdEndPos] != '(' && text[cmdEndPos] != ')')
    {
        ++cmdEndPos;
    }

    if (cmdEndPos <= pos)
        return std::nullopt;

    return std::string(text.substr(pos, cmdEndPos - pos));
}

void PromptComponent::setKnownFSharpNames(std::set<std::string> names)
{
    if (_knownFSharpNames == names)
        return;

    _knownFSharpNames = std::move(names);
    _diagnosticsContent.clear(); // Invalidate cache so diagnostics re-run with new names
}

void PromptComponent::updateDiagnostics()
{
    auto const text = std::string(_inputField.text());
    if (text == _diagnosticsContent)
        return;

    _diagnosticsContent = text;
    _diagnostics = endo::collectDiagnostics(text, _knownFSharpNames);
}

std::optional<endo::DiagnosticMessage> PromptComponent::diagnosticAt(int line, int character) const
{
    for (auto const& diag: _diagnostics)
    {
        auto const& r = diag.range;
        // Check if (line, character) is within this diagnostic's range
        if (line < r.start.line || line > r.end.line)
            continue;
        if (line == r.start.line && character < r.start.character)
            continue;
        if (line == r.end.line && character >= r.end.character)
            continue;
        return diag;
    }
    return std::nullopt;
}

std::optional<endo::SourcePosition> PromptComponent::screenToSourcePosition(int x, int y) const
{
    auto const totalPromptWidth =
        HorizontalMargin + LeftBarWidth + PaddingAfterBar + displayWidth(_promptStr);

    // Screen x must be within the text area
    if (x < totalPromptWidth)
        return std::nullopt;

    auto const totalLines = _inputField.lineCount();
    if (y < 0 || y >= totalLines)
        return std::nullopt;

    auto const lineContent = _inputField.lineAt(y);
    if (lineContent.empty())
        return endo::SourcePosition { .line = y, .character = 0 };

    // Walk grapheme clusters to convert display column to codepoint index
    auto const targetCol = x - totalPromptWidth;
    auto segmenter = unicode::utf8_grapheme_segmenter(lineContent);
    int displayCol = 0;
    int codepointIndex = 0;

    for (auto const& cluster: segmenter)
    {
        auto const w = tui::graphemeClusterWidth(cluster);
        if (displayCol + w > targetCol)
            break;
        displayCol += w;
        ++codepointIndex;
    }

    return endo::SourcePosition { .line = y, .character = codepointIndex };
}

std::pair<int, int> PromptComponent::getCommandBounds() const
{
    auto const totalPromptWidth =
        HorizontalMargin + LeftBarWidth + PaddingAfterBar + displayWidth(_promptStr);

    auto const text = _inputField.text();
    if (text.empty())
        return { totalPromptWidth, totalPromptWidth };

    // Skip leading whitespace
    auto pos = std::size_t { 0 };
    int leadingSpaceWidth = 0;
    while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t'))
    {
        leadingSpaceWidth += (text[pos] == '\t') ? 8 : 1; // Approximate tab width
        ++pos;
    }

    if (pos >= text.size())
        return { totalPromptWidth + leadingSpaceWidth, totalPromptWidth + leadingSpaceWidth };

    // Find command end and calculate display width
    auto cmdEndPos = pos;
    while (cmdEndPos < text.size() && text[cmdEndPos] != ' ' && text[cmdEndPos] != '\t'
           && text[cmdEndPos] != '|' && text[cmdEndPos] != ';' && text[cmdEndPos] != '&'
           && text[cmdEndPos] != '\n' && text[cmdEndPos] != '(' && text[cmdEndPos] != ')')
    {
        ++cmdEndPos;
    }

    auto const cmdText = text.substr(pos, cmdEndPos - pos);
    auto const cmdWidth = displayWidth(cmdText);

    int const cmdStart = totalPromptWidth + leadingSpaceWidth;
    int const cmdEnd = cmdStart + cmdWidth;

    return { cmdStart, cmdEnd };
}

} // namespace endo
