// SPDX-License-Identifier: Apache-2.0
#include <shell/AgentModeSession.hpp>

#include <tui/Buffer.hpp>
#include <tui/Canvas.hpp>
#include <tui/Screen.hpp>
#include <tui/Theme.hpp>

namespace endo
{

void InlinePrompt::clear(tui::TerminalOutput& output)
{
    if (!visible)
        return;
    output.hideCursor();
    output.restoreCursor();
    output.clearToEndOfDisplay();
    output.flush();
    visible = false;
}

void InlinePrompt::render(tui::TerminalOutput& output, tui::Terminal const& terminal)
{
    if (!active || !component)
        return;
    auto const& theme = tui::currentTheme();
    auto const prefSize = component->preferredSize();
    auto const width = terminal.columns();
    auto const height = prefSize.height;

    for (auto i = 0; i < height; ++i)
        output.linefeed();
    output.moveUp(height);
    output.saveCursor();
    output.linefeed();

    auto buffer = tui::Buffer(height, width);
    auto canvas = tui::Canvas(buffer, tui::Rect { .x = 0, .y = 0, .width = width, .height = height }, theme);
    component->setArea(tui::Rect { .x = 0, .y = 0, .width = width, .height = height });
    component->setScreenBounds(tui::Rect { .x = 0, .y = 0, .width = width, .height = height });
    component->render(canvas);
    buffer.writeTo(output);

    if (component->cursorShape() == tui::CursorShape::SteadyBar)
        output.showCursor();
    else
        output.hideCursor();
    output.flush();
    visible = true;
}

void InlinePrompt::reset()
{
    active = false;
    component.reset();
    visible = false;
    requestId = 0;
}

auto InlinePrompt::isActive() const -> bool
{
    return active && component.has_value();
}

bool AgentModeSession::anyPromptActive() const noexcept
{
    return askUserPrompt.active || permissionPrompt.active || sessionPickerPrompt.active
           || planApprovalPrompt.active;
}

void AgentModeSession::teardownStreaming()
{
    streaming = false;
    streamCancelled = false;
    streamingPromptVisible = false;
    currentRenderer.reset();
    activeRenderer = nullptr;
    _inputComponent.setThinkingActive(false);
}

void AgentModeSession::renderComponentDirect()
{
    auto const& theme = tui::currentTheme();
    auto const prefSize = _inputComponent.preferredSize();
    auto const width = _terminal.columns();
    auto const height = prefSize.height;

    auto buffer = tui::Buffer(height, width);
    auto canvas = tui::Canvas(buffer, tui::Rect { .x = 0, .y = 0, .width = width, .height = height }, theme);
    _inputComponent.setArea(tui::Rect { .x = 0, .y = 0, .width = width, .height = height });
    _inputComponent.setScreenBounds(tui::Rect { .x = 0, .y = 0, .width = width, .height = height });
    _inputComponent.render(canvas);
    buffer.writeTo(_out);
}

void AgentModeSession::renderToolStatusDirect()
{
    if (!_toolStatusComponent.hasEntries())
        return;
    auto const& theme = tui::currentTheme();
    auto const prefSize = _toolStatusComponent.preferredSize();
    auto const width = _terminal.columns();
    auto const height = prefSize.height;
    if (height <= 0)
        return;

    auto buffer = tui::Buffer(height, width);
    auto canvas = tui::Canvas(buffer, tui::Rect { .x = 0, .y = 0, .width = width, .height = height }, theme);
    _toolStatusComponent.setArea(tui::Rect { .x = 0, .y = 0, .width = width, .height = height });
    _toolStatusComponent.setScreenBounds(tui::Rect { .x = 0, .y = 0, .width = width, .height = height });
    _toolStatusComponent.render(canvas);
    buffer.writeTo(_out);
}

void AgentModeSession::clearStreamingPrompt()
{
    if (!streamingPromptVisible)
        return;
    _out.hideCursor();
    _out.restoreCursor();
    _out.clearToEndOfDisplay();
    _out.flush();
    streamingPromptVisible = false;
}

void AgentModeSession::renderStreamingPrompt()
{
    if (!streaming || anyPromptActive())
        return;
    // Pre-scroll: emit linefeeds matching the prompt height.
    // This forces any terminal scrolling BEFORE saveCursor, keeping the saved position valid.
    auto const promptHeight = _inputComponent.preferredSize().height;
    for (auto i = 0; i < promptHeight; ++i)
        _out.linefeed();
    _out.moveUp(promptHeight);
    _out.saveCursor();
    _out.linefeed();
    renderComponentDirect();
    _out.flush();
    streamingPromptVisible = true;
}

} // namespace endo
