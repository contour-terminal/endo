// SPDX-License-Identifier: Apache-2.0
#include <tui/Canvas.hpp>
#include <tui/Theme.hpp>
#include <tui/completer/CompletionProvider.hpp>

#include <agent/AgentInputComponent.hpp>

namespace endo::agent
{

AgentInputComponent::AgentInputComponent()
{
    // Default prompt indicator: ❯ (U+276F)
    _inputField.setPrompt("\xe2\x9d\xaf ");
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

    // Row 0: Header line  ╭─ agent │ provider/model
    canvas.putString(0, 0, "\xe2\x95\xad", barStyle);           // ╭
    canvas.putString(0, 1, "\xe2\x94\x80", barStyle);           // ─
    canvas.put(0, 2, " ", {});                                  // padding
    auto col = 3 + canvas.putString(0, 3, "agent", labelStyle); // "agent" label

    // Show provider and model info if available
    if (!_providerName.empty() || !_modelName.empty())
    {
        auto dimPipeStyle = tui::Style { .fg = theme.agentColors.leftBar };
        dimPipeStyle.dim = true;
        col += canvas.putString(0, col, " ", {});
        col += canvas.putString(0, col, "\xe2\x94\x82", dimPipeStyle); // │ separator
        col += canvas.putString(0, col, " ", {});

        if (!_providerName.empty() && !_modelName.empty())
            col += canvas.putString(0, col, _providerName + "/" + _modelName, infoStyle);
        else if (!_providerName.empty())
            col += canvas.putString(0, col, _providerName, infoStyle);
        else
            col += canvas.putString(0, col, _modelName, infoStyle);
    }

    // Show git branch if available (appears after background context loading completes)
    if (!_gitBranch.empty())
    {
        auto dimPipeStyle = tui::Style { .fg = theme.agentColors.leftBar };
        dimPipeStyle.dim = true;
        col += canvas.putString(0, col, " ", {});
        col += canvas.putString(0, col, "\xe2\x94\x82", dimPipeStyle); // │ separator
        col += canvas.putString(0, col, " ", {});

        auto branchStyle = tui::Style { .fg = theme.agentColors.statusText };
        branchStyle.dim = true;
        col += canvas.putString(0, col, _gitBranch, branchStyle);
    }

    // Draw left chrome for each input line
    for (auto row = 0; row < lineCount && (row + HeaderHeight) < area.height; ++row)
    {
        auto const canvasRow = row + HeaderHeight;
        if (row == 0)
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

    // Render InputField offset by header height and left chrome
    auto const fieldArea = tui::Rect {
        LeftBarWidth + BarPadding,
        HeaderHeight,
        area.width - LeftBarWidth - BarPadding,
        area.height - HeaderHeight,
    };
    auto fieldCanvas = canvas.subcanvas(fieldArea);
    _inputField.render(fieldCanvas);

    // Render completion popup if visible
    if (_completionPopup.visible())
    {
        auto const popupSize = _completionPopup.preferredSize();
        auto const cursorRow = HeaderHeight + _inputField.cursorLine();
        auto const popupRow = cursorRow + 1; // Below the cursor line
        auto const popupCol = LeftBarWidth + BarPadding;
        auto const popupHeight = std::min(popupSize.height, area.height - popupRow);
        auto const popupWidth = std::min(popupSize.width, area.width - popupCol);

        if (popupHeight > 0 && popupWidth > 0)
        {
            auto const popupRect = tui::Rect { popupCol, popupRow, popupWidth, popupHeight };
            _completionPopup.setArea(popupRect);
            auto popupCanvas = canvas.subcanvas(popupRect);
            _completionPopup.render(popupCanvas);
        }
    }
}

tui::EventResult AgentInputComponent::onEvent(tui::InputEvent const& event)
{
    auto const action = processInput(event);
    return action != Action::None ? tui::EventResult::Handled : tui::EventResult::Ignored;
}

tui::Size AgentInputComponent::preferredSize() const
{
    auto const fieldSize = _inputField.preferredSize();
    auto totalHeight = fieldSize.height + HeaderHeight;

    // Add space for completion popup if visible
    if (_completionPopup.visible())
    {
        auto const popupSize = _completionPopup.preferredSize();
        totalHeight += popupSize.height;
    }

    return { fieldSize.width + LeftBarWidth + BarPadding, totalHeight };
}

AgentInputComponent::Action AgentInputComponent::processInput(tui::InputEvent const& event)
{
    // Track if popup was visible before processing (for dynamic filtering)
    auto const popupWasVisible = _completionPopup.visible();
    auto popupDismissedByTyping = false;

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
            auto const currentPrefix = inputText.substr(0, cursor);
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
            return Action::Abort;
        }

        // Tab triggers completion
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
            dismissPopup();
            if (!_inputField.text().empty())
                return Action::Submit;
            return Action::None;
        case tui::InputFieldAction::Abort: dismissPopup(); return Action::Abort;
        case tui::InputFieldAction::Eof: dismissPopup(); return Action::Abort;
        case tui::InputFieldAction::Changed:
            // If popup was visible and dismissed by typing, re-filter instead of hiding
            if (popupWasVisible && popupDismissedByTyping)
                _completionPopupDirty = true;
            return Action::Changed;
        case tui::InputFieldAction::AgentMode: dismissPopup(); return Action::Abort; // Toggle back to shell
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
    // For slash commands, replace the entire input up to cursor with the completion text
    // then append a trailing space for argument entry
    auto const inputText = std::string(_inputField.text());
    auto const cursor = _inputField.cursor();

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

void AgentInputComponent::flushDeferredUpdates()
{
    if (_completionPopupDirty)
    {
        _completionPopupDirty = false;
        updateCompletionPopup();
    }
}

} // namespace endo::agent
