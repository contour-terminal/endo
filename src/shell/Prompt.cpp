// SPDX-License-Identifier: Apache-2.0
#include "Prompt.hpp"

#include <unistd.h>

#include "CommandResolver.hpp"
#include "Completer.hpp"
#include "Environment.hpp"
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

    // Create CommandResolver for tooltip support
    _commandResolver = std::make_unique<CommandResolver>(SystemEnvironment::instance());

    // Create PromptComponent
    _promptComponent = std::make_unique<PromptComponent>();
    _promptComponent->setPrompt(_promptStr);
    _promptComponent->setMultiline(_multilineEnabled);
    _promptComponent->setCompleter(_completer);
    _promptComponent->setCommandResolver(_commandResolver.get());

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

    // Set up hover callbacks for tooltip support
    setupHoverCallbacks();

    _initialized = true;
}

void Prompt::setupHoverCallbacks()
{
    // Set up hover confirmed callback
    _screen->hoverState().setOnHoverConfirmed([this](tui::HoverInfo const& hover) {
        // Check if hovering over our prompt component
        if (hover.target == _promptComponent.get() || hover.target == nullptr)
        {
            // Convert to component-relative coordinates
            auto const bounds = _promptComponent->screenBounds();
            int const relX = hover.x - bounds.x;
            int const relY = hover.y - bounds.y;
            _promptComponent->onHoverConfirmed(relX, relY);
        }
    });

    // Set up hover leave callback
    _screen->hoverState().setOnHoverLeave([this]() { _promptComponent->onHoverLeave(); });
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
        // Use hover timeout if hovering, otherwise block indefinitely
        int const timeout = _screen->pollTimeoutMs();
        auto events = _terminal.poll(timeout);

        // If no events received (timeout), process hover timer
        if (events.empty())
        {
            _screen->tickHover();
            _screen->draw(); // Redraw in case tooltip was shown
            continue;
        }

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

            // Dispatch through Screen first (updates hover state for mouse events)
            (void) _screen->dispatchEvent(event);

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
        {
            // Release cursor tracking since external output (shell commands) may have
            // moved the cursor to an unknown position.
            _screen->releaseCursor();
        }
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

void Prompt::bindKey(tui::KeyChord chord, tui::EditAction action)
{
    initialize();
    if (_promptComponent)
        _promptComponent->inputField().keyBindings().bind(chord, action);
}

void Prompt::unbindKey(tui::KeyChord chord)
{
    initialize();
    if (_promptComponent)
        _promptComponent->inputField().keyBindings().unbind(chord);
}

void Prompt::resetKeyBindings()
{
    initialize();
    if (_promptComponent)
        _promptComponent->inputField().setKeyBindings(tui::KeyBindings::defaults());
}

tui::KeyBindings const& Prompt::keyBindings() const
{
    // This const version needs to handle the case where promptComponent isn't initialized yet
    // Return a static default bindings as fallback
    static auto const defaultBindings = tui::KeyBindings::defaults();
    if (_promptComponent)
        return _promptComponent->inputField().keyBindings();
    return defaultBindings;
}

tui::KeyBindings& Prompt::keyBindings()
{
    // Initialize to ensure _promptComponent exists
    const_cast<Prompt*>(this)->initialize();
    // This will crash if initialization failed (_aborted), but that's acceptable
    // since the shell can't work without a functional terminal anyway
    return _promptComponent->inputField().keyBindings();
}

} // namespace endo
