// SPDX-License-Identifier: Apache-2.0
#include "PromptComponent.hpp"

#include <endo-language/HoverProvider.hpp>

#include <tui/Canvas.hpp>
#include <tui/Screen.hpp>
#include <tui/Theme.hpp>
#include <tui/Unicode.hpp>

#include <algorithm>

#include "CommandResolver.hpp"
#include "Completer.hpp"
#include "Gradient.hpp"
#include "History.hpp"
#include "SourceOffsetUtils.hpp"
#include "SyntaxHighlighter.hpp"
#include "modules/BatteryModule.hpp"
#include "modules/ClockModule.hpp"
#include "modules/DurationModule.hpp"
#include "modules/ExitStatusModule.hpp"
#include "modules/FSharpModeModule.hpp"
#include "modules/GitModule.hpp"
#include "modules/HostnameModule.hpp"
#include "modules/IndicatorModule.hpp"
#include "modules/PathModule.hpp"
#include "modules/StructuredOutputModule.hpp"
#include "modules/ToolchainModule.hpp"

#if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wold-style-cast"
#endif
#include <libunicode/utf8_grapheme_segmenter.h>
#if defined(__clang__)
    #pragma clang diagnostic pop
#endif

using tui::operator""_rgb;

namespace endo
{

PromptComponent::PromptComponent()
{
    _inputField.setPrompt(""); // We handle prompt rendering ourselves
    initializeModules();
}

void PromptComponent::initializeModules()
{
    auto add = [this](std::unique_ptr<PromptModule> mod) {
        auto const id = std::string(mod->id());
        _modules[id] = std::move(mod);
    };

    add(std::make_unique<PathModule>());
    add(std::make_unique<GitModule>());
    add(std::make_unique<ExitStatusModule>());
    add(std::make_unique<DurationModule>(_config.durationThresholdMs));
    add(std::make_unique<HostnameModule>());
    add(std::make_unique<ClockModule>());
    add(std::make_unique<BatteryModule>());
    add(std::make_unique<FSharpModeModule>());
    add(std::make_unique<StructuredOutputModule>());
    add(std::make_unique<ToolchainModule>());
    add(std::make_unique<IndicatorModule>(_config.indicator));
}

std::vector<PromptSegments> PromptComponent::evaluateModules(
    std::vector<std::string> const& moduleNames) const
{
    auto results = std::vector<PromptSegments> {};
    for (auto const& name: moduleNames)
    {
        auto it = _modules.find(name);
        if (it == _modules.end())
            continue;

        if (!it->second->shouldShow(_context))
            continue;

        auto segments = it->second->evaluate(_context);
        if (!segments.empty())
        {
            // Apply gradient to path module when configured
            if (_config.useGradientPath && name == "path")
            {
                std::string pathText;
                for (auto const& seg: segments)
                    pathText += seg.text;
                segments = gradient(_config.gradientStart, _config.gradientEnd, pathText);
                for (auto& seg: segments)
                    seg.style.bold = true;
            }
            results.push_back(std::move(segments));
        }
    }
    return results;
}

void PromptComponent::setPromptConfig(PromptConfig config)
{
    _config = std::move(config);

    // Update indicator module
    if (auto it = _modules.find("indicator"); it != _modules.end())
    {
        if (auto* ind = dynamic_cast<IndicatorModule*>(it->second.get()))
            ind->setIndicator(_config.indicator);
    }

    // Update duration module threshold
    if (auto it = _modules.find("duration"); it != _modules.end())
    {
        // Recreate with new threshold
        _modules["duration"] = std::make_unique<DurationModule>(_config.durationThresholdMs);
    }
}

void PromptComponent::setPromptContext(PromptContext context)
{
    _context = std::move(context);
}

void PromptComponent::render(tui::Canvas& canvas)
{
    auto const& theme = tui::currentTheme();
    auto const canvasWidth = canvas.width();
    auto const totalLines = _inputField.lineCount();
    auto const promptTextWidth = displayWidth(_promptStr);
    auto const totalPromptWidth = HorizontalMargin + leftBarWidth() + PaddingAfterBar + promptTextWidth;

    // Effective content width (excluding margins)
    auto const contentWidth = canvasWidth - 2 * HorizontalMargin;

    // Use theme-based colors
    auto const& pc = theme.promptColors;

    // Create styles
    tui::Style bgStyle;
    bgStyle.bg = pc.background;

    tui::Style leftBarStyle;
    leftBarStyle.fg = pc.separator;
    leftBarStyle.bg = pc.background;

    tui::Style promptStyle;
    promptStyle.fg = pc.badgeText;
    promptStyle.bg = pc.background;

    tui::Style ghostStyle;
    ghostStyle.fg = pc.badgeText;
    ghostStyle.bg = pc.background;
    ghostStyle.dim = true;

    // Calculate chrome height (info lines above input)
    auto const chrome = chromeHeight();

    // Render info line chrome above input
    if (chrome > 0)
    {
        auto infoModules = evaluateModules(_config.infoLineModules);
        auto rightModules = evaluateModules(_config.rightPromptModules);
        _nextModuleRefresh = computeModuleRefreshDeadline();

        // Info line background
        canvas.fill(tui::Rect { HorizontalMargin, 0, contentWidth, 1 }, ' ', bgStyle);

        auto col = HorizontalMargin;

        // Info line separator
        if (_config.separator == SeparatorStyle::Bar)
        {
            col += canvas.putString(0, col, "\xe2\x96\x8e", leftBarStyle); // U+258E
            canvas.put(0, col, " ", bgStyle);
            ++col;
        }
        else if (_config.separator == SeparatorStyle::Rounded)
        {
            tui::Style sepStyle;
            sepStyle.fg = pc.separator;
            sepStyle.bg = pc.background;
            col += canvas.putString(0, col, "\xe2\x95\xad", sepStyle); // U+256D ╭
            col += canvas.putString(0, col, "\xe2\x94\x80", sepStyle); // U+2500 ─
            canvas.put(0, col, " ", bgStyle);
            ++col;
        }

        // Render info modules
        for (std::size_t i = 0; i < infoModules.size(); ++i)
        {
            if (i > 0)
            {
                if (_config.separator == SeparatorStyle::Rounded)
                {
                    // Dim │ pipe separator between module groups
                    tui::Style dimPipeStyle;
                    dimPipeStyle.fg = pc.separator;
                    dimPipeStyle.bg = pc.background;
                    dimPipeStyle.dim = true;
                    canvas.put(0, col, " ", bgStyle);
                    ++col;
                    col += canvas.putString(0, col, "\xe2\x94\x82", dimPipeStyle); // U+2502 │
                    canvas.put(0, col, " ", bgStyle);
                    ++col;
                }
                else
                {
                    canvas.put(0, col, " ", bgStyle);
                    ++col;
                }
            }
            for (auto const& seg: infoModules[i])
            {
                auto segStyle = seg.style;
                segStyle.bg = pc.background;
                col += canvas.putString(0, col, seg.text, segStyle);
            }
        }

        // Right-aligned modules on info line
        if (!rightModules.empty())
        {
            auto rightWidth = 0;
            for (auto const& mod: rightModules)
            {
                for (auto const& seg: mod)
                    rightWidth += displayWidth(seg.text);
                rightWidth += 1; // space between modules
            }
            if (rightWidth > 0)
                --rightWidth; // Remove trailing space

            auto rightCol = canvasWidth - HorizontalMargin - rightWidth;
            if (rightCol > col + 2) // Ensure at least 2 chars gap
            {
                for (std::size_t i = 0; i < rightModules.size(); ++i)
                {
                    if (i > 0)
                    {
                        canvas.put(0, rightCol, " ", bgStyle);
                        ++rightCol;
                    }
                    for (auto const& seg: rightModules[i])
                    {
                        auto segStyle = seg.style;
                        segStyle.bg = pc.background;
                        rightCol += canvas.putString(0, rightCol, seg.text, segStyle);
                    }
                }
            }
        }
    }

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

    // Render each input line (offset by chrome height)
    for (int lineIndex = 0; lineIndex < totalLines && (lineIndex + chrome) < canvas.height(); ++lineIndex)
    {
        auto const row = lineIndex + chrome;
        auto const lineContent = _inputField.lineAt(lineIndex);

        // Fill content area with background (with margins)
        canvas.fill(tui::Rect { HorizontalMargin, row, contentWidth, 1 }, ' ', bgStyle);

        // Draw separator on input lines
        if (_config.separator == SeparatorStyle::Bar)
        {
            canvas.put(row, HorizontalMargin, "\xe2\x96\x8e", leftBarStyle); // U+258E
        }
        else if (_config.separator == SeparatorStyle::Rounded)
        {
            tui::Style sepStyle;
            sepStyle.fg = pc.separator;
            sepStyle.bg = pc.background;
            if (lineIndex == 0)
            {
                canvas.putString(row, HorizontalMargin, "\xe2\x95\xb0", sepStyle);     // U+2570 ╰
                canvas.putString(row, HorizontalMargin + 1, "\xe2\x94\x80", sepStyle); // U+2500 ─
            }
            else
                canvas.putString(row, HorizontalMargin, "\xe2\x94\x82", sepStyle); // U+2502 │
        }
        else if (_config.separator == SeparatorStyle::None)
        {
            canvas.put(row, HorizontalMargin, " ", bgStyle);
        }

        // Padding after separator
        canvas.put(row, HorizontalMargin + leftBarWidth(), " ", bgStyle);

        // Prompt or continuation indicator
        auto col = HorizontalMargin + leftBarWidth() + PaddingAfterBar;
        if (lineIndex == 0)
        {
            col += canvas.putString(row, col, _promptStr, promptStyle);
        }
        else
        {
            // Continuation indicator: spaces + middle dots
            for (int i = 0; i < promptTextWidth - 2; ++i)
                canvas.put(row, col++, " ", bgStyle);
            col += canvas.putString(row, col, "\xc2\xb7\xc2\xb7", promptStyle); // ··
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
                segStyle.bg = pc.background;
                segStyle.inverse = selected;

                // Apply curly red underline for error regions
                if (hasError)
                {
                    segStyle.underlineStyle = tui::UnderlineStyle::Curly;
                    segStyle.underlineColor = 0xC0C000_rgb; // Yellow color for errors (stands out on both
                                                            // light and dark backgrounds)
                }

                col += canvas.putString(row, col, lineContent.substr(segStart, segEnd - segStart), segStyle);
                segStart = segEnd;
            }
        }

        // Ghost text on last line
        if (!_inputField.ghostText().empty() && lineIndex == totalLines - 1)
        {
            canvas.putString(row, col, _inputField.ghostText(), ghostStyle);
        }
    }

    // Position cursor (add chrome height offset)
    auto const cursorLine = _inputField.cursorLine();
    auto const cursorColumn = _inputField.cursorColumn();
    auto const cursorRow = cursorLine + chrome;

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

    canvas.setCursor(cursorRow, displayCol);

    // Render completion popup if visible
    if (_completionPopup.visible())
    {
        auto popupSize = _completionPopup.preferredSize();
        int availableBelow = canvas.height() - cursorRow - 1;
        int availableAbove = cursorRow;

        bool renderBelow = true;

        // In fullscreen/fixed mode, choose direction based on available space
        // In inline mode, always render below (Screen handles scrolling via preferredSize)
        if (auto* scr = screen(); scr && scr->viewport() != tui::Viewport::Inline)
        {
            // Prefer below, but use above if below has < 3 rows and above has more space
            if (availableBelow < 3 && availableAbove > availableBelow)
                renderBelow = false;
        }

        int popupRow = renderBelow ? (cursorRow + 1)
                                   : std::max(0, cursorRow - std::min(popupSize.height, availableAbove));
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
    auto const inputLineCount = _inputField.lineCount();
    auto const pw = this->promptWidth();

    // Calculate max line width
    int maxWidth = 0;
    for (int i = 0; i < inputLineCount; ++i)
    {
        auto const lineContent = _inputField.lineAt(i);
        maxWidth = std::max(maxWidth, pw + displayWidth(lineContent));
    }

    // Total height = chrome lines (info/box above) + input lines
    int totalHeight = inputLineCount + chromeHeight();

    // If completion popup is visible, add space for it below the input
    if (_completionPopup.visible())
    {
        auto popupSize = _completionPopup.preferredSize();
        totalHeight += popupSize.height;
        maxWidth = std::max(maxWidth, pw + popupSize.width);
    }

    return { maxWidth, totalHeight };
}

void PromptComponent::setPrompt(std::string_view prompt)
{
    _promptStr = std::string(prompt);
}

int PromptComponent::promptWidth() const
{
    return leftBarWidth() + PaddingAfterBar + displayWidth(_promptStr);
}

int PromptComponent::chromeHeight() const noexcept
{
    if (_config.layout == PromptLayoutKind::TwoLine || _config.layout == PromptLayoutKind::Powerline)
        return 1;
    if (_config.layout == PromptLayoutKind::Boxed)
        return 3;
    return 0;
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

    // Inline history cycling: Up/Down cycles through history (prefix-matched when input is non-empty)
    if (!_completionPopup.visible())
    {
        if (auto const* key = std::get_if<tui::KeyEvent>(&event))
        {
            auto const inputText = std::string(_inputField.text());
            if (key->key == tui::KeyCode::Up || key->key == tui::KeyCode::Down)
            {
                if (key->key == tui::KeyCode::Up)
                {
                    if (!_historyCycleIndex.has_value())
                    {
                        // First Up press: compute candidates from completer, save original input
                        _historyCycleSavedInput = inputText;
                        _historyCandidates.clear();
                        if (_history)
                        {
                            auto matches = _history->search(inputText, 50);
                            for (auto const& entry: matches)
                                if (entry != inputText)
                                    _historyCandidates.push_back(std::string(entry));
                        }
                        if (!_historyCandidates.empty())
                            _historyCycleIndex = 0;
                    }
                    else if (*_historyCycleIndex + 1 < _historyCandidates.size())
                    {
                        ++(*_historyCycleIndex);
                    }
                    // else: at end, do nothing (don't wrap)
                }
                else // Down
                {
                    if (_historyCycleIndex.has_value())
                    {
                        if (*_historyCycleIndex > 0)
                            --(*_historyCycleIndex);
                        else
                        {
                            // Back to original input
                            _historyCycleIndex.reset();
                        }
                    }
                }

                // Apply the selected candidate or restore original
                if (_historyCycleIndex.has_value())
                    _inputField.setText(_historyCandidates[*_historyCycleIndex]);
                else
                    _inputField.setText(_historyCycleSavedInput);

                updateGhostText();
                return Action::Changed;
            }
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
            if (key->key == tui::KeyCode::Right || key->key == tui::KeyCode::End
                || (key->codepoint == 'e' && tui::hasModifier(key->modifiers, tui::Modifier::Ctrl)))
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
            resetHistoryCycling();
            return Action::Submit;
        case tui::InputFieldAction::Abort:
            _inputField.clearGhostText();
            _completionPopup.hide();
            resetHistoryCycling();
            return Action::Abort;
        case tui::InputFieldAction::Eof:
            _inputField.clearGhostText();
            _completionPopup.hide();
            resetHistoryCycling();
            return Action::Eof;
        case tui::InputFieldAction::Changed:
            resetHistoryCycling();
            updateGhostText();
            // If popup was visible and dismissed by typing, re-filter instead of hiding
            if (popupWasVisible && popupDismissedByTyping)
                updateCompletionPopup();
            return Action::Changed;
        case tui::InputFieldAction::None:
            // If dismissed but text didn't change (e.g., Escape), hide popup
            if (popupDismissedByTyping)
            {
                _completionPopup.hide();
                return Action::Changed; // Trigger re-render so popup disappears
            }
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
            auto tooltipText = diag->message;
            for (auto const& hint: diag->suggestions)
                tooltipText += "\nhint: " + hint;
            tui::Point tooltipPos { bounds.x + x, bounds.y + y + 1 };
            scr->showTooltip(tooltipText, tooltipPos, tui::TooltipContentType::PlainText);
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

    // Priority 3: Fall through to existing command hover logic (first input line only)
    if (y == chromeHeight() && _commandResolver)
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
        HorizontalMargin + leftBarWidth() + PaddingAfterBar + displayWidth(_promptStr);

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
        HorizontalMargin + leftBarWidth() + PaddingAfterBar + displayWidth(_promptStr);

    // Screen x must be within the text area
    if (x < totalPromptWidth)
        return std::nullopt;

    // Convert screen y to input line index (subtract chrome offset)
    auto const inputLine = y - chromeHeight();
    auto const totalLines = _inputField.lineCount();
    if (inputLine < 0 || inputLine >= totalLines)
        return std::nullopt;

    auto const lineContent = _inputField.lineAt(inputLine);
    if (lineContent.empty())
        return endo::SourcePosition { .line = inputLine, .character = 0 };

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

    return endo::SourcePosition { .line = inputLine, .character = codepointIndex };
}

std::pair<int, int> PromptComponent::getCommandBounds() const
{
    auto const totalPromptWidth =
        HorizontalMargin + leftBarWidth() + PaddingAfterBar + displayWidth(_promptStr);

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

void PromptComponent::resetHistoryCycling()
{
    _historyCycleIndex.reset();
    _historyCandidates.clear();
}

std::optional<std::chrono::steady_clock::time_point> PromptComponent::computeModuleRefreshDeadline() const
{
    auto minInterval = std::optional<std::chrono::milliseconds> {};

    auto const checkModules = [&](std::vector<std::string> const& moduleNames) {
        for (auto const& name: moduleNames)
        {
            auto const it = _modules.find(name);
            if (it == _modules.end())
                continue;
            if (!it->second->shouldShow(_context))
                continue;
            if (auto const interval = it->second->refreshInterval())
            {
                if (!minInterval || *interval < *minInterval)
                    minInterval = *interval;
            }
        }
    };

    checkModules(_config.infoLineModules);
    checkModules(_config.rightPromptModules);

    if (minInterval)
        return std::chrono::steady_clock::now() + *minInterval;

    return std::nullopt;
}

int PromptComponent::moduleRefreshTimeoutMs() const
{
    if (!_nextModuleRefresh)
        return -1;

    auto const now = std::chrono::steady_clock::now();
    if (now >= *_nextModuleRefresh)
        return 0;

    auto const remaining = std::chrono::duration_cast<std::chrono::milliseconds>(*_nextModuleRefresh - now);
    return std::max(100, static_cast<int>(remaining.count()));
}

} // namespace endo
