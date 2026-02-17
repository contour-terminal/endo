// SPDX-License-Identifier: Apache-2.0
#include <tui/Canvas.hpp>
#include <tui/Theme.hpp>

#include <agent/AgentInputComponent.hpp>

namespace endo::agent
{

AgentInputComponent::AgentInputComponent()
{
    _inputField.setPrompt("# ");
}

void AgentInputComponent::render(tui::Canvas& canvas)
{
    auto const& theme = tui::currentTheme();
    auto const barStyle = tui::Style { .fg = theme.agentColors.leftBar };
    auto const textStyle = tui::Style { .fg = theme.colors.text };

    auto const area = screenBounds();
    auto const lineCount = _inputField.lineCount();

    // Draw purple left bar for each line
    for (auto row = 0; row < lineCount && row < area.height; ++row)
        canvas.put(row, 0, "\u2502", barStyle); // │ character

    // Render InputField in the remaining area
    auto const fieldArea = tui::Rect {
        LeftBarWidth + BarPadding,
        0,
        area.width - LeftBarWidth - BarPadding,
        area.height,
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
    return { fieldSize.width + LeftBarWidth + BarPadding, fieldSize.height };
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
        case tui::InputFieldAction::None: break;
    }

    return Action::None;
}

} // namespace endo::agent
