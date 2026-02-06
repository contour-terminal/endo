// SPDX-License-Identifier: Apache-2.0
#include "Prompt.hpp"

#include <unistd.h>

#include "Completer.hpp"
#include "PromptComponent.hpp"
#include <tui/Screen.hpp>

namespace endo
{

Prompt::Prompt() = default;

Prompt::~Prompt()
{
    // Screen must be destroyed before terminal shutdown
    _screen.reset();
    _promptComponent.reset();

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
    _terminal.output().flush();

    // Create PromptComponent
    _promptComponent = std::make_unique<PromptComponent>();
    _promptComponent->setPrompt(_promptStr);
    _promptComponent->setMultiline(_multilineEnabled);
    _promptComponent->setCompleter(_completer);

    // Set up clipboard callback to use OSC 52
    _promptComponent->setClipboardCallback([this](std::string_view text) {
        _terminal.output().copyToClipboard(text);
        _terminal.output().flush();
    });

    // Create Screen with Inline viewport mode
    auto screenConfig = tui::ScreenConfig {
        .viewport = tui::Viewport::Inline,
        .fixedArea = {},
        .inlineMaxHeight = _terminal.rows() / 2, // Max 50% of terminal height
    };
    _screen = std::make_unique<tui::Screen>(_terminal, screenConfig);

    // Add PromptComponent to the screen's root
    // Set initial area to full width, 1 row (will grow as needed)
    _promptComponent->setArea(tui::Rect { 0, 0, _terminal.columns(), 1 });
    _screen->root().addChild(*_promptComponent);

    // Set initial focus
    _screen->setFocus(_promptComponent.get());

    _initialized = true;
}

std::string Prompt::read()
{
    initialize();
    if (_aborted)
        return {};

    // Clear and prepare for new input
    _promptComponent->clear();
    _promptComponent->setPrompt(_promptStr);
    _promptComponent->setMultiline(_multilineEnabled);

    // Update component area based on content
    auto prefSize = _promptComponent->preferredSize();
    _promptComponent->setArea(tui::Rect { 0, 0, _terminal.columns(), prefSize.height });

    // Render initial state
    _screen->draw();

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
                // Update component area
                auto pSize = _promptComponent->preferredSize();
                _promptComponent->setArea(tui::Rect { 0, 0, _terminal.columns(), pSize.height });
                _screen->draw();
                continue;
            }

            // Process event through PromptComponent
            auto action = _promptComponent->processInput(event);

            switch (action)
            {
                case PromptComponent::Action::Submit: {
                    // Redraw to clear ghost text before moving cursor
                    _screen->draw();
                    // Move cursor past the editor region before returning
                    auto& out = _terminal.output();
                    auto const totalLines = _promptComponent->inputField().lineCount();
                    auto const cursorLine = _promptComponent->inputField().cursorLine();
                    auto const linesToMoveDown = totalLines - cursorLine;
                    if (linesToMoveDown > 0)
                        out.moveDown(linesToMoveDown);
                    out.writeRaw("\r\n");
                    out.flush();
                    return std::string(_promptComponent->text());
                }
                case PromptComponent::Action::Abort:
                    // Ctrl+C - clear line and return empty
                    _terminal.output().writeRaw("^C\r\n");
                    _terminal.output().flush();
                    _promptComponent->clear();
                    return {};
                case PromptComponent::Action::Eof:
                    // Ctrl+D on empty line - signal EOF
                    _terminal.output().writeRaw("\r\n");
                    _terminal.output().flush();
                    _aborted = true;
                    return {};
                case PromptComponent::Action::Changed: {
                    // Update component area for potential size change
                    auto pSize = _promptComponent->preferredSize();
                    _promptComponent->setArea(tui::Rect { 0, 0, _terminal.columns(), pSize.height });
                    _screen->draw();
                    break;
                }
                case PromptComponent::Action::ClearScreen: {
                    // Clear screen and move prompt to top
                    auto& out = _terminal.output();
                    out.clearScreen();
                    out.flush();
                    // Force full redraw since terminal was cleared
                    _screen->invalidate();
                    // Update component area and redraw
                    auto pSize = _promptComponent->preferredSize();
                    _promptComponent->setArea(tui::Rect { 0, 0, _terminal.columns(), pSize.height });
                    _screen->draw();
                    break;
                }
                case PromptComponent::Action::None: break;
            }
        }
    }

    return {};
}

void Prompt::setPrompt(std::string_view promptStr)
{
    _promptStr = std::string(promptStr);
    if (_promptComponent)
        _promptComponent->setPrompt(_promptStr);
}

void Prompt::addHistory(std::string entry)
{
    if (_promptComponent)
        _promptComponent->addHistory(std::move(entry));
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
            auto pSize = _promptComponent->preferredSize();
            _promptComponent->setArea(tui::Rect { 0, 0, _terminal.columns(), pSize.height });
            _screen->draw();
            continue;
        }

        auto action = _promptComponent->processInput(event);

        switch (action)
        {
            case PromptComponent::Action::Submit: {
                auto& out = _terminal.output();
                auto const totalLines = _promptComponent->inputField().lineCount();
                auto const cursorLine = _promptComponent->inputField().cursorLine();
                auto const linesToMoveDown = totalLines - cursorLine;
                if (linesToMoveDown > 0)
                    out.moveDown(linesToMoveDown);
                out.writeRaw("\r\n");
                out.flush();
                auto result = std::string(_promptComponent->text());
                _promptComponent->clear();
                return result;
            }
            case PromptComponent::Action::Abort:
                _terminal.output().writeRaw("^C\r\n");
                _terminal.output().flush();
                _promptComponent->clear();
                return std::string {};
            case PromptComponent::Action::Eof:
                _terminal.output().writeRaw("\r\n");
                _terminal.output().flush();
                _aborted = true;
                return std::string {};
            case PromptComponent::Action::Changed: {
                auto pSize = _promptComponent->preferredSize();
                _promptComponent->setArea(tui::Rect { 0, 0, _terminal.columns(), pSize.height });
                _screen->draw();
                break;
            }
            case PromptComponent::Action::ClearScreen: {
                // Clear screen and move prompt to top
                auto& out = _terminal.output();
                out.clearScreen();
                out.flush();
                // Force full redraw since terminal was cleared
                _screen->invalidate();
                // Update component area and redraw
                auto pSize = _promptComponent->preferredSize();
                _promptComponent->setArea(tui::Rect { 0, 0, _terminal.columns(), pSize.height });
                _screen->draw();
                break;
            }
            case PromptComponent::Action::None: break;
        }
    }

    return std::nullopt;
}

void Prompt::onResize()
{
    _terminal.output().updateDimensions();
    if (_screen)
        _screen->invalidate();
}

void Prompt::display()
{
    initialize();
    if (_aborted)
        return;

    // Prepare the input
    _promptComponent->clear();
    _promptComponent->setPrompt(_promptStr);
    _promptComponent->setMultiline(_multilineEnabled);

    // Update component area
    auto prefSize = _promptComponent->preferredSize();
    _promptComponent->setArea(tui::Rect { 0, 0, _terminal.columns(), prefSize.height });

    // Render
    _screen->draw();
}

void Prompt::suspend()
{
    if (_initialized)
        _terminal.suspend();
}

void Prompt::resume()
{
    if (_initialized)
    {
        _terminal.resume();
        if (_screen)
            _screen->invalidate();
    }
}

void Prompt::setCompleter(Completer* completer)
{
    _completer = completer;
    if (_promptComponent)
        _promptComponent->setCompleter(completer);
}

void Prompt::setMultilineEnabled(bool enable)
{
    _multilineEnabled = enable;
    if (_promptComponent)
        _promptComponent->setMultiline(enable);
}

bool Prompt::isMultilineEnabled() const noexcept
{
    return _multilineEnabled;
}

} // namespace endo
