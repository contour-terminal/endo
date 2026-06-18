// SPDX-License-Identifier: Apache-2.0
#include <tui/Canvas.hpp>
#include <tui/GhostTextHelper.hpp>
#include <tui/ImageLoader.hpp>
#include <tui/Sixel.hpp>
#include <tui/Theme.hpp>
#include <tui/TimerUtils.hpp>
#include <tui/completer/CompletionProvider.hpp>

#include <format>
#include <ranges>

#include <agent/ui/AgentInputComponent.hpp>

namespace endo::agent
{

AgentInputComponent::AgentInputComponent()
{
    // Default prompt indicator: ❯ (U+276F)
    _inputField.setPrompt("\xe2\x9d\xaf ");
    _inputField.setMultiline(true);
}

void AgentInputComponent::setPromptIndicator(std::string indicator)
{
    _inputField.setPrompt(std::move(indicator) + " ");
}

void AgentInputComponent::addCompletionProvider(std::unique_ptr<tui::CompletionProvider> provider)
{
    _completer.addProvider(std::move(provider));
}

void AgentInputComponent::render(tui::Canvas& canvas)
{
    auto const& theme = tui::currentTheme();
    auto const barStyle = tui::Style { .fg = theme.agentColors.leftBar };
    auto const labelStyle = tui::Style { .fg = theme.agentColors.leftBar };
    auto const infoStyle = tui::Style { .fg = theme.agentColors.statusText };

    auto const area = screenBounds();
    auto const lineCount = _inputField.lineCount();

    // Top padding rows are left blank (promptSpacing)
    auto const rowOff = _topPadding;

    // Row rowOff: Header line  ╭─ agent │ provider/model
    canvas.putString(rowOff, 0, "\xe2\x95\xad", barStyle);           // ╭
    canvas.putString(rowOff, 1, "\xe2\x94\x80", barStyle);           // ─
    canvas.put(rowOff, 2, " ", {});                                  // padding
    auto col = 3 + canvas.putString(rowOff, 3, "agent", labelStyle); // "agent" label

    // Show mode indicator (plan vs execute)
    {
        auto dimPipeStyle = tui::Style { .fg = theme.agentColors.leftBar };
        dimPipeStyle.dim = true;
        col += canvas.putString(rowOff, col, " ", {});
        col += canvas.putString(rowOff, col, "\xe2\x94\x82", dimPipeStyle); // │ separator
        col += canvas.putString(rowOff, col, " ", {});
        auto const modeStyle = _planMode ? tui::Style { .fg = theme.agentColors.planModeText }
                                         : tui::Style { .fg = theme.agentColors.executeModeText };
        col += canvas.putString(rowOff, col, _planMode ? "plan" : "execute", modeStyle);
    }

    // Show provider and model info if available
    if (!_providerName.empty() || !_modelName.empty())
    {
        auto dimPipeStyle = tui::Style { .fg = theme.agentColors.leftBar };
        dimPipeStyle.dim = true;
        col += canvas.putString(rowOff, col, " ", {});
        col += canvas.putString(rowOff, col, "\xe2\x94\x82", dimPipeStyle); // │ separator
        col += canvas.putString(rowOff, col, " ", {});

        if (!_providerName.empty() && !_modelName.empty())
            col += canvas.putString(rowOff, col, _providerName + "/" + _modelName, infoStyle);
        else if (!_providerName.empty())
            col += canvas.putString(rowOff, col, _providerName, infoStyle);
        else
            col += canvas.putString(rowOff, col, _modelName, infoStyle);
    }

    // Show thinking mode if not off
    if (_thinkingMode != ThinkingMode::Off)
    {
        auto dimPipeStyle = tui::Style { .fg = theme.agentColors.leftBar };
        dimPipeStyle.dim = true;
        col += canvas.putString(rowOff, col, " ", {});
        col += canvas.putString(rowOff, col, "\xe2\x94\x82", dimPipeStyle); // | separator
        col += canvas.putString(rowOff, col, " ", {});

        auto const modeStr = std::string("thinking:") + std::string(thinkingModeToString(_thinkingMode));
        auto thinkingStyle = tui::Style { .fg = theme.agentColors.statusText };
        col += canvas.putString(rowOff, col, modeStr, thinkingStyle);
    }

    // Show git branch and/or project path (appears after background context loading completes)
    if (!_gitBranch.empty() || !_projectPath.empty())
    {
        auto dimPipeStyle = tui::Style { .fg = theme.agentColors.leftBar };
        dimPipeStyle.dim = true;
        col += canvas.putString(rowOff, col, " ", {});
        col += canvas.putString(rowOff, col, "\xe2\x94\x82", dimPipeStyle); // │ separator
        col += canvas.putString(rowOff, col, " ", {});

        auto dimTextStyle = tui::Style { .fg = theme.agentColors.statusText };
        dimTextStyle.dim = true;

        // Render project path with blue→teal gradient coloring.
        auto const renderPathGradient = [&]() {
            for (auto const i: std::views::iota(0uz, _projectPath.size()))
            {
                auto const t = _projectPath.size() == 1
                                   ? 0.0f
                                   : static_cast<float>(i) / static_cast<float>(_projectPath.size() - 1);
                auto const color =
                    tui::lerpColor(theme.agentColors.pathGradientStart, theme.agentColors.pathGradientEnd, t);
                col += canvas.putString(rowOff, col, _projectPath.substr(i, 1), tui::Style { .fg = color });
            }
        };

        if (!_gitBranch.empty())
        {
            col += canvas.putString(rowOff, col, _gitBranch, dimTextStyle);
            if (!_projectPath.empty())
            {
                col += canvas.putString(rowOff, col, " @ ", dimPipeStyle);
                renderPathGradient();
            }
        }
        else
        {
            renderPathGradient();
        }
    }

    // Draw left chrome for each visible input line (accounting for scroll offset)
    auto const scrollOff = _inputField.scrollOffset();
    auto const fieldHeight =
        std::max(1, area.height - rowOff - HeaderHeight - imagePreviewHeight() - FooterHeight);
    auto const visibleLines = std::min(lineCount - scrollOff, fieldHeight);
    for (auto row = 0; row < visibleLines && (rowOff + row + HeaderHeight) < area.height; ++row)
    {
        auto const canvasRow = rowOff + row + HeaderHeight;
        auto const logicalLine = row + scrollOff;
        if (logicalLine == 0)
        {
            // First input line: ╰─
            canvas.putString(canvasRow, 0, "\xe2\x95\xb0", barStyle); // ╰
            canvas.putString(canvasRow, 1, "\xe2\x94\x80", barStyle); // ─
        }
        else
        {
            // Continuation lines: │
            canvas.putString(canvasRow, 0, "\xe2\x94\x82", barStyle); // │
        }
    }

    // Render InputField offset by top padding, header height, and left chrome
    auto const fieldArea = tui::Rect {
        .x = LeftBarWidth + BarPadding,
        .y = rowOff + HeaderHeight,
        .width = area.width - LeftBarWidth - BarPadding,
        .height = fieldHeight,
    };
    auto fieldCanvas = canvas.subcanvas(fieldArea);
    _inputField.render(fieldCanvas);

    // Render completion popup if visible
    if (_completionPopup.visible())
    {
        auto const popupSize = _completionPopup.preferredSize();
        auto const cursorRow = rowOff + HeaderHeight + _inputField.cursorLine() - scrollOff;
        auto const popupRow = cursorRow + 1; // Below the cursor line
        auto const popupCol = LeftBarWidth + BarPadding;
        auto const popupHeight = std::min(popupSize.height, area.height - popupRow);
        auto const popupWidth = std::min(popupSize.width, area.width - popupCol);

        if (popupHeight > 0 && popupWidth > 0)
        {
            auto const popupRect =
                tui::Rect { .x = popupCol, .y = popupRow, .width = popupWidth, .height = popupHeight };
            _completionPopup.setArea(popupRect);
            auto popupCanvas = canvas.subcanvas(popupRect);
            _completionPopup.render(popupCanvas);
        }
    }

    // Render command palette (centered, below input area)
    if (_commandPalette.visible())
    {
        auto const palettePrefSize = _commandPalette.preferredSize();
        auto const paletteWidth = std::min(palettePrefSize.width, area.width);
        auto const paletteRow = rowOff + HeaderHeight + fieldHeight;
        auto const paletteHeight = std::min(palettePrefSize.height, area.height - paletteRow);
        if (paletteHeight >= 4) // Minimum: border(2) + filter(1) + separator(1)
        {
            auto const paletteX = std::max(0, (area.width - paletteWidth) / 2);
            auto const paletteRect =
                tui::Rect { .x = paletteX, .y = paletteRow, .width = paletteWidth, .height = paletteHeight };
            _commandPalette.setArea(paletteRect);
            auto paletteCanvas = canvas.subcanvas(paletteRect);
            _commandPalette.render(paletteCanvas);
        }
    }

    // Render image preview area between input field and info line
    auto const previewHeight = imagePreviewHeight();
    auto previewRow = rowOff + HeaderHeight + fieldHeight;
    if (previewHeight > 0 && previewRow < area.height)
    {
        auto const dimStyle = tui::Style { .fg = theme.agentColors.statusText, .dim = true };
        for (size_t i = 0; i < _attachedImages.size() && previewRow < area.height; ++i)
        {
            auto const& img = _attachedImages[i];
            auto const& layout = i < _previewLayouts.size() ? _previewLayouts[i]
                                                            : PreviewLayout { .colSpan = 0, .lineSpan = 1 };
            if (i < _imagePreviews.size() && !_imagePreviews[i].empty() && layout.colSpan > 0)
            {
                canvas.drawImage(previewRow,
                                 LeftBarWidth + BarPadding,
                                 layout.colSpan,
                                 layout.lineSpan,
                                 _imagePreviews[i]);
                // Label after image
                auto const sizeKb = img.data.size() / 1024;
                auto const label = std::format("[{}: {}, {} KB]", i + 1, img.mediaType, sizeKb);
                canvas.putString(previewRow, LeftBarWidth + BarPadding + layout.colSpan + 1, label, dimStyle);
                previewRow += layout.lineSpan;
            }
            else
            {
                // Text placeholder for images without sixel preview
                auto const sizeKb = img.data.size() / 1024;
                auto const label = std::format("[image {}: {}, {} KB]", i + 1, img.mediaType, sizeKb);
                canvas.putString(previewRow, LeftBarWidth + BarPadding, label, dimStyle);
                ++previewRow;
            }
        }
    }

    // Render info line below the input field (and image preview area)
    auto const infoLineRow = rowOff + HeaderHeight + fieldHeight + previewHeight;
    if (infoLineRow < area.height)
        renderInfoLine(canvas, infoLineRow);
    // Bottom padding: write a non-breaking space so Screen counts this row as content.
    auto const paddingRow = rowOff + HeaderHeight + fieldHeight + previewHeight + InfoLineHeight;
    if (paddingRow < area.height)
        canvas.put(paddingRow, 0, "\xc2\xa0", {}); // U+00A0 non-breaking space
}

tui::EventResult AgentInputComponent::onEvent(tui::InputEvent const& event)
{
    auto const action = processInput(event);
    return action != Action::None ? tui::EventResult::Handled : tui::EventResult::Ignored;
}

tui::Size AgentInputComponent::preferredSize() const
{
    auto const fieldSize = _inputField.preferredSize();
    auto totalHeight = _topPadding + fieldSize.height + HeaderHeight + imagePreviewHeight() + FooterHeight;

    // Add space for completion popup if visible
    if (_completionPopup.visible())
    {
        auto const popupSize = _completionPopup.preferredSize();
        totalHeight += popupSize.height;
    }

    // Add space for command palette if visible
    if (_commandPalette.visible())
    {
        auto const paletteSize = _commandPalette.preferredSize();
        totalHeight += paletteSize.height;
    }

    return { .width = fieldSize.width + LeftBarWidth + BarPadding, .height = totalHeight };
}

AgentInputComponent::Action AgentInputComponent::processInput(tui::InputEvent const& event)
{
    // Handle command palette events first (takes priority over everything)
    if (_commandPalette.visible())
    {
        auto const paletteAction = _commandPalette.processEvent(event);
        switch (paletteAction)
        {
            case tui::CommandPaletteAction::Changed: return Action::Changed;
            case tui::CommandPaletteAction::Executed:
            case tui::CommandPaletteAction::Dismissed: return Action::Changed;
        }
        return Action::Changed;
    }

    // Track if popup was visible before processing (for dynamic filtering)
    auto const popupWasVisible = _completionPopup.visible();
    auto popupDismissedByTyping = false;

    // Intercept paste events: detect image data before forwarding to InputField.
    if (auto const* paste = std::get_if<tui::PasteEvent>(&event))
    {
        auto const raw = std::span<std::uint8_t const>(
            reinterpret_cast<std::uint8_t const*>(paste->text.data()), paste->text.size());
        auto const mediaType = tui::detectImageMediaType(raw);
        if (!mediaType.empty())
        {
            attachImage(std::vector<std::uint8_t>(raw.begin(), raw.end()), mediaType);
            return Action::Changed;
        }

        // If paste text is empty, the terminal likely received an image paste request
        // that it couldn't convert to text. Try reading image from the system clipboard.
        if (paste->text.empty())
        {
            if (auto clipboardImage = tui::readClipboardImage())
            {
                attachImage(std::move(clipboardImage->data), std::move(clipboardImage->mediaType));
                return Action::Changed;
            }
        }
        // Non-image paste: fall through to InputField.
    }

    // Handle completion popup events first
    if (_completionPopup.visible())
    {
        // Intercept Tab for partial completion (longest common prefix)
        if (auto const* key = std::get_if<tui::KeyEvent>(&event);
            key && key->key == tui::KeyCode::Tab
            && tui::withoutLockKeys(key->modifiers) == tui::Modifier::None
            && _completionPopup.itemCount() > 1)
        {
            auto const commonPrefix = tui::Completer::findCommonPrefix(_completionPopup.items());
            auto const inputText = std::string(_inputField.text());
            auto const cursor = _inputField.cursor();

            // For @-mentions, compute current prefix from the '@' position
            auto currentPrefix = std::string {};
            if (commonPrefix.starts_with("@"))
            {
                auto const upToCursor = std::string_view(inputText).substr(0, cursor);
                auto const atPos = upToCursor.rfind('@');
                if (atPos != std::string_view::npos)
                    currentPrefix = inputText.substr(atPos, cursor - atPos);
            }
            else
            {
                currentPrefix = inputText.substr(0, cursor);
            }

            if (!commonPrefix.empty() && commonPrefix.size() > currentPrefix.size())
            {
                insertCompletion(commonPrefix);
                _completionPopupDirty = true;
                return Action::Changed;
            }
        }

        auto const completionResult = _completionPopup.processEvent(event);
        switch (completionResult)
        {
            case tui::CompletionAction::Changed: return Action::Changed;
            case tui::CompletionAction::Accepted:
                if (auto const* selected = _completionPopup.selectedItem())
                    insertCompletion(selected->text);
                dismissPopup();
                return Action::Changed;
            case tui::CompletionAction::Dismissed:
                // Don't hide yet — let event pass through and potentially re-filter
                popupDismissedByTyping = true;
                break;
        }
    }

    // Check for Escape to abort (only if popup is not visible)
    if (auto const* key = std::get_if<tui::KeyEvent>(&event))
    {
        if (key->key == tui::KeyCode::Escape)
        {
            if (_completionPopup.visible())
            {
                dismissPopup();
                return Action::Changed;
            }

            if (_escapeHintVisible)
            {
                // Second press within timeout — abort.
                auto const now = std::chrono::steady_clock::now();
                auto const elapsed =
                    std::chrono::duration_cast<std::chrono::milliseconds>(now - _lastEscapeTime);
                if (elapsed < EscapeHintTimeout)
                {
                    _escapeHintVisible = false;
                    _inputField.clearGhostText();
                    _savedTextBeforeEscape.clear();
                    return Action::Abort;
                }
                // Timeout already expired but flush hasn't run yet — restore, then start new cycle.
                restoreFromEscapeHint();
            }

            // First press: save text, clear field, show hint.
            _lastEscapeTime = std::chrono::steady_clock::now();
            _escapeHintVisible = true;
            _savedTextBeforeEscape = std::string(_inputField.text());
            _savedCursorBeforeEscape = _inputField.cursor();
            _inputField.clear();
            _inputField.setGhostText("Press Escape again to cancel");
            return Action::Changed;
        }

        // Any other key while escape hint is visible — restore and continue.
        if (_escapeHintVisible)
            restoreFromEscapeHint();

        // Tab with ghost text: accept ghost text before trying completion
        if (key->key == tui::KeyCode::Tab && tui::withoutLockKeys(key->modifiers) == tui::Modifier::None
            && _inputField.hasGhostText())
        {
            // Confirm the suggestion before committing it (a delete may have left an
            // un-recomputed re-prepended guess); recompute is cache-backed.
            updateGhostText();
            if (_inputField.hasGhostText())
            {
                _inputField.acceptGhostText();
                // Preserve the consumed-prefix seed acceptGhostText just set (so a following
                // backward delete restores the ghost synchronously): suppress the pending
                // recompute that would otherwise clear it. See PromptComponent for rationale.
                _ghostTextDirty = false;
                _ghostTextPendingSince.reset();
            }
            return Action::Changed;
        }

        // Right arrow or End at end of line accepts ghost text
        if (_inputField.hasGhostText() && _inputField.cursor() == _inputField.text().size())
        {
            if (key->key == tui::KeyCode::Right || key->key == tui::KeyCode::End
                || (key->codepoint == 'e' && tui::hasModifier(key->modifiers, tui::Modifier::Ctrl)))
            {
                updateGhostText();
                if (_inputField.hasGhostText())
                {
                    _inputField.acceptGhostText();
                    _ghostTextDirty = false;
                    _ghostTextPendingSince.reset();
                }
                return Action::Changed;
            }
        }

        // Ctrl+L clears the screen
        if (key->codepoint == 'l' && tui::hasModifier(key->modifiers, tui::Modifier::Ctrl))
            return Action::ClearScreen;

        // Tab triggers completion (no ghost text case)
        if (key->key == tui::KeyCode::Tab && tui::withoutLockKeys(key->modifiers) == tui::Modifier::None)
        {
            triggerCompletion(false);
            return Action::Changed;
        }

        // Ctrl+Space triggers completion (always shows popup)
        if (key->codepoint == ' ' && tui::hasModifier(key->modifiers, tui::Modifier::Ctrl))
        {
            triggerCompletion(true);
            return Action::Changed;
        }
    }

    // Delegate to InputField
    auto const action = _inputField.processEvent(event);
    switch (action)
    {
        case tui::InputFieldAction::Submit:
            _inputField.clearGhostText();
            dismissPopup();
            if (std::ranges::all_of(_inputField.text(), [](unsigned char c) { return std::isspace(c); }))
                return Action::None;
            return Action::Submit;
        case tui::InputFieldAction::Abort:
            _inputField.clearGhostText();
            dismissPopup();
            return Action::Abort;
        case tui::InputFieldAction::Eof:
            _inputField.clearGhostText();
            dismissPopup();
            return Action::Abort;
        case tui::InputFieldAction::Changed:
            _ghostTextDirty = true;
            _ghostTextPendingSince = std::chrono::steady_clock::now();
            // If popup was visible and dismissed by typing, re-filter instead of hiding
            if (popupWasVisible && popupDismissedByTyping)
                _completionPopupDirty = true;
            // Auto-trigger popup when typing a slash command
            else if (_inputField.text().starts_with("/")
                     && std::string_view(_inputField.text()).substr(0, _inputField.cursor()).find(' ')
                            == std::string_view::npos)
                _completionPopupDirty = true;
            // Auto-trigger popup when typing an @-mention
            else if (isInAtMentionContext(_inputField.text(), _inputField.cursor()))
                _completionPopupDirty = true;
            // Dismiss popup if text no longer looks like a completable context
            else if (_completionPopup.visible())
                dismissPopup();
            return Action::Changed;
        case tui::InputFieldAction::CommandPalette:
            _inputField.clearGhostText();
            dismissPopup();
            if (_commandRegistry)
                _commandPalette.show(*_commandRegistry, tui::CommandContext::Agent);
            return Action::Changed;
        case tui::InputFieldAction::AgentMode:
            _inputField.clearGhostText();
            dismissPopup();
            return Action::Abort; // Toggle back to shell
        case tui::InputFieldAction::CycleAgentMode:
            _inputField.clearGhostText();
            dismissPopup();
            return Action::CycleMode;
        case tui::InputFieldAction::CycleThinkingMode:
            _inputField.clearGhostText();
            dismissPopup();
            return Action::CycleThinkingMode;
        case tui::InputFieldAction::CycleModel:
            _inputField.clearGhostText();
            dismissPopup();
            return Action::CycleModel;
        case tui::InputFieldAction::FuzzyFileFinder:
            // Not applicable in agent mode; ignore.
            break;
        case tui::InputFieldAction::NewPrompt:
            _inputField.clearGhostText();
            dismissPopup();
            return Action::NewPrompt;
        case tui::InputFieldAction::None:
            // If dismissed but text didn't change (e.g., Escape), hide popup
            if (popupDismissedByTyping)
            {
                dismissPopup();
                return Action::Changed;
            }
            break;
    }

    return Action::None;
}

void AgentInputComponent::triggerCompletion(bool forceShowPopup)
{
    if (_completer.providerCount() == 0)
        return;

    auto const text = _inputField.text();
    auto const cursor = _inputField.cursor();

    auto completions = _completer.complete(text, cursor);

    if (completions.empty())
    {
        dismissPopup();
        return;
    }

    if (completions.size() == 1 && !forceShowPopup)
    {
        // Single match: insert directly without showing popup
        insertCompletion(completions[0].text);
        dismissPopup();
        return;
    }

    // Multiple matches (or force-show): show popup
    _completionPopup.show(std::move(completions));
}

void AgentInputComponent::updateCompletionPopup()
{
    if (_completer.providerCount() == 0)
    {
        dismissPopup();
        return;
    }

    auto const text = _inputField.text();
    auto const cursor = _inputField.cursor();

    auto completions = _completer.complete(text, cursor);

    if (completions.empty())
    {
        dismissPopup();
        return;
    }

    _completionPopup.updateItems(std::move(completions));
}

void AgentInputComponent::insertCompletion(std::string_view text)
{
    auto const inputText = std::string(_inputField.text());
    auto const cursor = _inputField.cursor();

    // @-mention: replace only the @query portion, preserving surrounding text
    if (text.starts_with("@"))
    {
        auto const upToCursor = std::string_view(inputText).substr(0, cursor);
        auto const atPos = upToCursor.rfind('@');
        if (atPos != std::string_view::npos)
        {
            auto newBuffer = std::string {};
            newBuffer.append(inputText, 0, atPos);
            newBuffer.append(text);
            newBuffer += ' ';
            newBuffer.append(inputText.substr(cursor));
            _inputField.setText(std::move(newBuffer));
            _inputField.setCursor(atPos + text.size() + 1);
            return;
        }
    }

    // Slash commands: replace the entire input up to cursor with the completion text
    // then append a trailing space for argument entry
    auto newBuffer = std::string {};
    newBuffer.reserve(text.size() + 1 + inputText.size() - cursor);
    newBuffer.append(text);
    newBuffer += ' ';
    newBuffer.append(inputText.substr(cursor));

    _inputField.setText(std::move(newBuffer));
}

void AgentInputComponent::dismissPopup()
{
    _completionPopup.hide();
    _completionPopupDirty = false;
}

void AgentInputComponent::updateGhostText()
{
    if (_completer.providerCount() == 0)
    {
        _inputField.clearGhostText();
        return;
    }

    tui::updateGhostText(_inputField,
                         _suggestCacheText,
                         _suggestCacheResult,
                         [this](auto const& text, auto cursor) { return _completer.suggest(text, cursor); });
}

void AgentInputComponent::flushDeferredUpdates()
{
    if (_escapeHintVisible)
    {
        if ((std::chrono::steady_clock::now() - _lastEscapeTime) >= EscapeHintTimeout)
            restoreFromEscapeHint();
    }
    if (_ghostTextDirty)
    {
        // Only flush once debounce period has elapsed.
        if (!_ghostTextPendingSince
            || (std::chrono::steady_clock::now() - *_ghostTextPendingSince) >= GhostTextDebounceMs)
        {
            _ghostTextDirty = false;
            _ghostTextPendingSince.reset();
            updateGhostText();
        }
        // else: debounce not expired — keep dirty flag for next flush.
    }
    if (_completionPopupDirty)
    {
        _completionPopupDirty = false;
        updateCompletionPopup();
    }
}

int AgentInputComponent::ghostTextTimeoutMs() const
{
    return tui::remainingMs(_ghostTextPendingSince, GhostTextDebounceMs);
}

int AgentInputComponent::escapeHintTimeoutMs() const
{
    if (!_escapeHintVisible)
        return -1;
    return tui::remainingMs(_lastEscapeTime, EscapeHintTimeout);
}

void AgentInputComponent::restoreFromEscapeHint()
{
    _escapeHintVisible = false;
    _inputField.clearGhostText();
    _inputField.setText(_savedTextBeforeEscape);
    _inputField.setCursor(_savedCursorBeforeEscape);
    _savedTextBeforeEscape.clear();
}

bool AgentInputComponent::isInAtMentionContext(std::string_view input, size_t cursorPosition)
{
    auto const upToCursor = input.substr(0, cursorPosition);
    auto const atPos = upToCursor.rfind('@');
    if (atPos == std::string_view::npos)
        return false;

    // '@' must be at position 0 or preceded by whitespace
    if (atPos > 0 && !std::isspace(static_cast<unsigned char>(input[atPos - 1])))
        return false;

    // No whitespace between '@' and cursor
    auto const afterAt = upToCursor.substr(atPos + 1);
    return afterAt.find_first_of(" \t\n") == std::string_view::npos;
}

void AgentInputComponent::setThinkingActive(bool active)
{
    _thinkingActive = active;
    if (active)
        _spinner.reset();
}

void AgentInputComponent::setActivityLabel(std::string label)
{
    _activityLabel = std::move(label);
}

bool AgentInputComponent::tickSpinner()
{
    if (!_thinkingActive)
        return false;
    return _spinner.tick();
}

int AgentInputComponent::spinnerTimeoutMs() const
{
    if (!_thinkingActive)
        return -1;
    return static_cast<int>(_spinner.interval().count());
}

void AgentInputComponent::renderInfoLine(tui::Canvas& canvas, int row)
{
    auto const& theme = tui::currentTheme();
    auto const area = screenBounds();
    auto col = 1; // Indent to align with content (past the left bar chrome).

    if (_thinkingActive)
    {
        // Spinner + activity label
        auto const spinnerStyle = tui::Style { .fg = theme.agentColors.spinnerColor };
        auto const labelStyle = tui::Style { .fg = theme.agentColors.statusText };
        col += canvas.putString(row, col, _spinner.currentFrame(), spinnerStyle);
        col += canvas.putString(row, col, " ", {});
        canvas.putString(row, col, _activityLabel, labelStyle);
    }
    else
    {
        // Shortcut hints
        auto const keyStyle = tui::Style { .fg = theme.agentColors.leftBar, .dim = true };
        auto const descStyle = tui::Style { .fg = theme.agentColors.statusText, .dim = true };

        struct Hint
        {
            std::string_view key;
            std::string_view desc;
        };

        static constexpr std::array hints = {
            Hint { .key = "Esc", .desc = "exit" },
            Hint { .key = "S-Tab", .desc = "mode" },
            Hint { .key = "C-/", .desc = "thinking" },
            Hint { .key = "C-.", .desc = "model" },
        };

        for (auto const& [key, desc]: hints)
        {
            if (col > 1)
                col += canvas.putString(row, col, "  ", {}); // gap between hints
            col += canvas.putString(row, col, key, keyStyle);
            col += canvas.putString(row, col, " ", {});
            col += canvas.putString(row, col, desc, descStyle);
            if (col >= area.width)
                break;
        }

        // Show image attachment count
        if (!_attachedImages.empty() && col < area.width)
        {
            auto const imageCountStyle = tui::Style { .fg = theme.agentColors.leftBar };
            auto const label = _attachedImages.size() == 1
                                   ? std::string("1 image attached")
                                   : std::format("{} images attached", _attachedImages.size());
            col += canvas.putString(row, col, "  ", {});
            canvas.putString(row, col, label, imageCountStyle);
        }
    }
}

bool AgentInputComponent::attachImage(std::vector<std::uint8_t> data, std::string mediaType)
{
    if (_attachedImages.size() >= MaxAttachedImages)
        return false;

    if (data.size() > MaxImageBytes)
        return false;

    _attachedImages.push_back(ImageBlock {
        .data = std::move(data),
        .mediaType = std::move(mediaType),
    });

    // Generate sixel preview (best-effort — failure doesn't block attachment).
    auto const& img = _attachedImages.back();
    auto loaded = tui::loadImageFromMemory(std::span<std::uint8_t const>(img.data.data(), img.data.size()));
    if (!loaded.has_value())
    {
        _imagePreviews.emplace_back();
        _previewLayouts.push_back(PreviewLayout { .colSpan = 0, .lineSpan = 1 });
        return true;
    }

    auto const cellW = std::max(1, _cellPixelWidth > 0 ? _cellPixelWidth : 8);
    auto const cellH = std::max(1, _cellPixelHeight > 0 ? _cellPixelHeight : 16);
    auto const maxPixelWidth = PreviewMaxColumns * cellW;
    auto const maxPixelHeight = PreviewMaxLines * cellH;

    // Fit within bounds preserving aspect ratio.
    auto targetW = loaded->width;
    auto targetH = loaded->height;
    if (targetW > maxPixelWidth)
    {
        targetH = targetH * maxPixelWidth / targetW;
        targetW = maxPixelWidth;
    }
    if (targetH > maxPixelHeight)
    {
        targetW = targetW * maxPixelHeight / targetH;
        targetH = maxPixelHeight;
    }

    // Cache the computed layout for this preview.
    auto const colSpan = std::max(1, targetW / cellW);
    auto const lineSpan = std::max(1, (std::max(1, targetH) + cellH - 1) / cellH);
    _previewLayouts.push_back(PreviewLayout { .colSpan = colSpan, .lineSpan = lineSpan });

    auto resized = tui::resizeImage(*loaded, std::max(1, targetW), std::max(1, targetH));
    if (!resized.has_value())
    {
        _imagePreviews.emplace_back();
        return true;
    }

    auto const imageData = tui::ImageData {
        .pixels = std::span(resized->pixels),
        .width = resized->width,
        .height = resized->height,
    };
    auto sixel = tui::encodeSixel(imageData, 64);
    _imagePreviews.push_back(sixel.has_value() ? std::move(*sixel) : std::string {});
    return true;
}

void AgentInputComponent::removeImage(size_t index)
{
    if (index < _attachedImages.size())
    {
        _attachedImages.erase(_attachedImages.begin() + static_cast<ptrdiff_t>(index));
        _imagePreviews.erase(_imagePreviews.begin() + static_cast<ptrdiff_t>(index));
        _previewLayouts.erase(_previewLayouts.begin() + static_cast<ptrdiff_t>(index));
    }
}

void AgentInputComponent::clearImages()
{
    _attachedImages.clear();
    _imagePreviews.clear();
    _previewLayouts.clear();
}

auto AgentInputComponent::attachedImages() const noexcept -> std::span<ImageBlock const>
{
    return _attachedImages;
}

auto AgentInputComponent::imageCount() const noexcept -> size_t
{
    return _attachedImages.size();
}

void AgentInputComponent::setCellPixelDimensions(int width, int height) noexcept
{
    _cellPixelWidth = width;
    _cellPixelHeight = height;
}

int AgentInputComponent::imagePreviewHeight() const noexcept
{
    auto totalLines = 0;
    for (auto const& layout: _previewLayouts)
        totalLines += layout.lineSpan;
    return totalLines;
}

} // namespace endo::agent
