// SPDX-License-Identifier: Apache-2.0
#include <tui/Canvas.hpp>
#include <tui/Theme.hpp>

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
}

tui::EventResult AgentInputComponent::onEvent(tui::InputEvent const& event)
{
    auto const action = processInput(event);
    return action != Action::None ? tui::EventResult::Handled : tui::EventResult::Ignored;
}

tui::Size AgentInputComponent::preferredSize() const
{
    auto const fieldSize = _inputField.preferredSize();
    return { fieldSize.width + LeftBarWidth + BarPadding, fieldSize.height + HeaderHeight };
}

AgentInputComponent::Action AgentInputComponent::processInput(tui::InputEvent const& event)
{
    // Check for Escape to abort
    if (auto const* key = std::get_if<tui::KeyEvent>(&event))
    {
        if (key->key == tui::KeyCode::Escape)
            return Action::Abort;
    }

    // Delegate to InputField
    auto const action = _inputField.processEvent(event);
    switch (action)
    {
        case tui::InputFieldAction::Submit:
            if (!_inputField.text().empty())
                return Action::Submit;
            return Action::None;
        case tui::InputFieldAction::Abort: return Action::Abort;
        case tui::InputFieldAction::Eof: return Action::Abort;
        case tui::InputFieldAction::Changed: return Action::Changed;
        case tui::InputFieldAction::AgentMode: return Action::Abort; // Toggle back to shell
        case tui::InputFieldAction::None: break;
    }

    return Action::None;
}

} // namespace endo::agent
