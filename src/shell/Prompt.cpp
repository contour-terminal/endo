// SPDX-License-Identifier: Apache-2.0
#include "Prompt.hpp"

#include <tui/Screen.hpp>

#include <unistd.h>

#include "CommandResolver.hpp"
#include "Completer.hpp"
#include "PromptComponent.hpp"
#include "platform/PosixEnvironmentProvider.hpp"

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
    _commandResolver = std::make_unique<CommandResolver>(PosixEnvironmentProvider::instance());

    // Create PromptComponent
    _promptComponent = std::make_unique<PromptComponent>();
    _promptComponent->setPromptConfig(_promptConfig);
    _promptComponent->setPrompt(_promptStr);
    _promptComponent->setMultiline(_multilineEnabled);
    _promptComponent->setCompleter(_completer);
    _promptComponent->setHistory(_history);
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
        .inhibitReflow = true,
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
            // hover.x and hover.y are viewport-relative 1-based coordinates
            // Convert to component-relative 0-based coordinates
            auto const bounds = _promptComponent->screenBounds();
            int const relX = hover.x - 1 - bounds.x;
            int const relY = hover.y - 1 - bounds.y;
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
        // Combine hover timeout with module auto-refresh timeout
        auto const hoverTimeout = _screen->pollTimeoutMs();
        auto const moduleTimeout = _promptComponent->moduleRefreshTimeoutMs();
        int timeout;
        if (hoverTimeout < 0 && moduleTimeout < 0)
            timeout = -1;
        else if (hoverTimeout < 0)
            timeout = moduleTimeout;
        else if (moduleTimeout < 0)
            timeout = hoverTimeout;
        else
            timeout = std::min(hoverTimeout, moduleTimeout);
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

            // Dispatch mouse events through Screen (updates hover state)
            // Key events go directly to processInput to avoid double-processing
            if (std::holds_alternative<tui::MouseEvent>(event))
            {
                (void) _screen->dispatchEvent(event);
            }

            // Process event through PromptComponent
            auto action = _promptComponent->processInput(event);

            switch (action)
            {
                case PromptComponent::Action::Submit: {
                    // Redraw to clear ghost text before moving cursor
                    _screen->draw();
                    auto& out = _terminal.output();
                    auto const inputText = std::string(_promptComponent->text());
                    if (_promptConfig.transient != TransientMode::Off)
                    {
                        emitTransientPrompt(inputText);
                    }
                    else
                    {
                        // Move cursor past the editor region before returning
                        auto const totalLines = _promptComponent->inputField().lineCount();
                        auto const cursorLine = _promptComponent->inputField().cursorLine();
                        auto const linesToMoveDown = totalLines - cursorLine;
                        if (linesToMoveDown > 0)
                            out.moveDown(linesToMoveDown);
                    }
                    out.writeRaw("\r\n");
                    out.enableReflow();
                    out.flush();
                    return inputText;
                }
                case PromptComponent::Action::Abort:
                    // Ctrl+C - clear line and return empty
                    _terminal.output().writeRaw("^C\r\n");
                    _terminal.output().enableReflow();
                    _terminal.output().flush();
                    _promptComponent->clear();
                    return {};
                case PromptComponent::Action::Eof:
                    // Ctrl+D on empty line - signal EOF
                    _terminal.output().writeRaw("\r\n");
                    _terminal.output().enableReflow();
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
                    // Reset cursor tracking since screen was cleared and cursor moved to top
                    _screen->releaseCursor();
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
                auto result = std::string(_promptComponent->text());
                if (_promptConfig.transient != TransientMode::Off)
                {
                    emitTransientPrompt(result);
                }
                else
                {
                    auto const totalLines = _promptComponent->inputField().lineCount();
                    auto const cursorLine = _promptComponent->inputField().cursorLine();
                    auto const linesToMoveDown = totalLines - cursorLine;
                    if (linesToMoveDown > 0)
                        out.moveDown(linesToMoveDown);
                }
                out.writeRaw("\r\n");
                out.enableReflow();
                out.flush();
                _promptComponent->clear();
                return result;
            }
            case PromptComponent::Action::Abort:
                _terminal.output().writeRaw("^C\r\n");
                _terminal.output().enableReflow();
                _terminal.output().flush();
                _promptComponent->clear();
                return std::string {};
            case PromptComponent::Action::Eof:
                _terminal.output().writeRaw("\r\n");
                _terminal.output().enableReflow();
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
                // Reset cursor tracking since screen was cleared and cursor moved to top
                _screen->releaseCursor();
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

        // Check if the command left the cursor at a non-column-1 position,
        // indicating output that didn't end with a newline. Show a dim indicator
        // (like fish shell) and move to a fresh line.
        if (auto const [row, col] = _terminal.queryCursorPosition(); col > 1)
            emitPartialLineIndicator(STDOUT_FILENO, col);

        if (_screen)
        {
            // Release cursor tracking since external output (shell commands) may have
            // moved the cursor to an unknown position.
            _screen->releaseCursor();
        }
    }
}

void Prompt::setPromptConfig(PromptConfig config)
{
    _promptConfig = std::move(config);
    // Update the prompt indicator string to match config
    _promptStr = _promptConfig.indicator;
    if (_promptComponent)
    {
        _promptComponent->setPromptConfig(_promptConfig);
        _promptComponent->setPrompt(_promptStr);
    }
}

PromptConfig const& Prompt::promptConfig() const noexcept
{
    return _promptConfig;
}

void Prompt::setPromptContext(PromptContext context)
{
    if (_promptComponent)
        _promptComponent->setPromptContext(std::move(context));
}

void Prompt::setKnownFSharpNames(std::set<std::string> names)
{
    if (_promptComponent)
        _promptComponent->setKnownFSharpNames(std::move(names));
}

void Prompt::setCompleter(Completer* completer)
{
    _completer = completer;
    if (_promptComponent)
        _promptComponent->setCompleter(completer);
}

void Prompt::setHistory(History const* history)
{
    _history = history;
    if (_promptComponent)
        _promptComponent->setHistory(history);
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

void Prompt::emitTransientPrompt(std::string_view inputText)
{
    if (_promptConfig.transient == TransientMode::Off)
        return;

    auto& out = _terminal.output();
    auto const chrome = _promptComponent->chromeHeight();
    auto const totalInputLines = _promptComponent->inputField().lineCount();
    auto const cursorLine = _promptComponent->inputField().cursorLine();
    auto const totalHeight = chrome + totalInputLines;
    auto const currentRow = chrome + cursorLine;

    // Move cursor up to the top of the prompt region
    if (currentRow > 0)
        out.moveUp(currentRow);

    // Write transient content on the first line
    out.writeRaw("\r");
    out.clearLine();

    if (_promptConfig.transient == TransientMode::Arrow)
        out.write("\u276F ", tui::Style { .dim = true });

    // Show first line of input (with ellipsis for multi-line)
    auto const firstNewline = inputText.find('\n');
    if (firstNewline != std::string_view::npos)
    {
        out.writeRaw(inputText.substr(0, firstNewline));
        out.write("\u2026", tui::Style { .dim = true });
    }
    else
    {
        out.writeRaw(inputText);
    }

    // Clear remaining rows
    for (auto i = 1; i < totalHeight; ++i)
    {
        out.moveDown(1);
        out.writeRaw("\r");
        out.clearLine();
    }
}

void emitPartialLineIndicator(int fd, int cursorColumn)
{
    if (cursorColumn <= 1)
        return;

    // SGR 2 (dim) + U+23CE (return symbol) + SGR 0 (reset) + CSI K (clear to EOL) + CR LF
    static constexpr std::string_view indicator = "\033[2m\u23CE\033[0m\033[K\r\n";
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    ::write(fd, indicator.data(), indicator.size());
}

} // namespace endo
