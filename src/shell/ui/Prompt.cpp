// SPDX-License-Identifier: Apache-2.0
#include "Prompt.hpp"

#include <tui/Screen.hpp>

#include "modules/GitModule.hpp"

#if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wold-style-cast"
#endif
#include <libunicode/utf8_grapheme_segmenter.h>
#if defined(__clang__)
    #pragma clang diagnostic pop
#endif

#include <shell/completion/Completer.hpp>
#include <shell/ui/SyntaxHighlighter.hpp>
#include <shell/util/CommandResolver.hpp>

#include "PromptComponent.hpp"
#if defined(_WIN32)
    #include <platform/windows/WindowsEnvironmentProvider.hpp>
#else
    #include <platform/posix/PosixEnvironmentProvider.hpp>
#endif

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

void Prompt::ensureInitialized()
{
    initialize();
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
#if defined(_WIN32)
    _commandResolver = std::make_unique<CommandResolver>(WindowsEnvironmentProvider::instance());
#else
    _commandResolver = std::make_unique<CommandResolver>(PosixEnvironmentProvider::instance());
#endif

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

    // Propagate terminal focus state to prompt component for visual dimming
    _terminal.onFocusChanged([this](bool focused) { _promptComponent->setTerminalFocused(focused); });

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
    _promptComponent->setArea(tui::Rect { .x = 0, .y = 0, .width = _terminal.columns(), .height = 1 });
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
    _promptComponent->setArea(
        tui::Rect { .x = 0, .y = 0, .width = _terminal.columns(), .height = prefSize.height });

    // Render initial state (skip if display() already drew current state)
    if (!_displayDrewCurrentState)
        _screen->draw();
    _displayDrewCurrentState = false;

    // Event loop
    while (ready())
    {
        // When unfocused, use a long poll timeout and skip deferred updates
        auto const wasFocused = _terminal.isFocused();

        // Combine hover, module auto-refresh, diagnostics debounce, and ghost text debounce timeouts
        auto timeout = -1;
        if (wasFocused)
        {
            auto const hoverTimeout = _screen->pollTimeoutMs();
            auto const moduleTimeout = _promptComponent->moduleRefreshTimeoutMs();
            auto const diagTimeout = _promptComponent->diagnosticsTimeoutMs();
            auto const ghostTimeout = _promptComponent->ghostTextTimeoutMs();
            auto const exitHintTimeout = _promptComponent->exitHintTimeoutMs();
            for (auto const t: { hoverTimeout, moduleTimeout, diagTimeout, ghostTimeout, exitHintTimeout })
            {
                if (t >= 0)
                    timeout = (timeout < 0) ? t : std::min(timeout, t);
            }
        }
        else
        {
            timeout = 5000; // Idle when unfocused — just wait for events
        }
        auto events = _terminal.poll(timeout);

        // Re-read focus state after poll — FocusEvent may have been consumed during poll()
        auto const nowFocused = _terminal.isFocused();
        auto const focusChanged = (wasFocused != nowFocused);

        // If no events received (timeout or only internal events like FocusEvent)
        if (events.empty())
        {
            if (focusChanged)
            {
                // Focus just changed — redraw to show/hide dim effect
                _promptComponent->flushDeferredUpdates();
                auto pSize = _promptComponent->preferredSize();
                _promptComponent->setArea(
                    tui::Rect { .x = 0, .y = 0, .width = _terminal.columns(), .height = pSize.height });
                _screen->draw();
            }
            else if (nowFocused)
            {
                _screen->tickHover();
                _promptComponent->flushDeferredUpdates();
                auto pSize = _promptComponent->preferredSize();
                _promptComponent->setArea(
                    tui::Rect { .x = 0, .y = 0, .width = _terminal.columns(), .height = pSize.height });
                _screen->draw();
            }
            continue;
        }

        auto needsRedraw = false;
        for (auto const& event: events)
        {
            // Handle resize events
            if (std::holds_alternative<tui::ResizeEvent>(event))
            {
                onResize();
                needsRedraw = true;
                continue;
            }

            // Skip modifier-only key events (Ctrl, Alt, Shift, CapsLock, etc. pressed alone).
            // These produce no text and no editing action; processing them would cause
            // unnecessary redraws that force the terminal viewport to scroll to the bottom.
            if (auto const* key = std::get_if<tui::KeyEvent>(&event); key && tui::isModifierOnlyKey(key->key))
                continue;

            // Dispatch all events through Screen (updates hover state, auto-hides tooltips on key press,
            // and routes mouse events to PromptComponent::onEvent for click-to-cursor positioning)
            auto const screenResult = _screen->dispatchEvent(event);
            if (screenResult == tui::EventResult::Handled && std::holds_alternative<tui::MouseEvent>(event))
                needsRedraw = true;

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
                        // Move cursor past the editor region (including bottom padding)
                        auto const totalLines = _promptComponent->inputField().lineCount();
                        auto const cursorLine = _promptComponent->inputField().cursorLine();
                        auto const linesToMoveDown =
                            totalLines - cursorLine - 1 + _promptComponent->bottomPadding();
                        if (linesToMoveDown > 0)
                            out.moveDown(linesToMoveDown);
                    }
                    out.carriageReturn();
                    out.linefeed();
                    out.enableReflow();
                    out.flush();
                    _lastAction = PromptComponent::Action::Submit;
                    return inputText;
                }
                case PromptComponent::Action::Abort:
                    // Ctrl+C - clear line and return empty
                    _terminal.output().writeText("^C", {});
                    _terminal.output().carriageReturn();
                    _terminal.output().linefeed();
                    _terminal.output().enableReflow();
                    _terminal.output().flush();
                    _promptComponent->clear();
                    _lastAction = PromptComponent::Action::Abort;
                    return {};
                case PromptComponent::Action::Eof:
                    // Ctrl+D on empty line - signal EOF
                    _terminal.output().carriageReturn();
                    _terminal.output().linefeed();
                    _terminal.output().enableReflow();
                    _terminal.output().flush();
                    _aborted = true;
                    _lastAction = PromptComponent::Action::Eof;
                    return {};
                case PromptComponent::Action::AgentMode: {
                    _promptComponent->clear();
                    _screen->clearAndRelease();
                    _lastAction = PromptComponent::Action::AgentMode;
                    return {};
                }
                case PromptComponent::Action::CommandPalette: needsRedraw = true; break;
                case PromptComponent::Action::Changed: needsRedraw = true; break;
                case PromptComponent::Action::ClearScreen: {
                    // Clear screen and move prompt to top
                    auto& out = _terminal.output();
                    out.clearScreen();
                    out.flush();
                    // Reset cursor tracking since screen was cleared and cursor moved to top
                    _screen->releaseCursor();
                    needsRedraw = true;
                    break;
                }
                case PromptComponent::Action::None: break;
            }
        }

        // Display completion errors above the prompt if any
        if (_promptComponent->hasPendingCompletionErrors())
        {
            auto errors = _promptComponent->takePendingCompletionErrors();
            auto& out = _terminal.output();
            // Hide prompt: clear inline viewport
            _screen->clearAndRelease();
            // Write each error line
            for (auto const& err: errors)
            {
                out.writeRaw(err);
                out.carriageReturn();
                out.linefeed();
            }
            out.flush();
            // Update saved cursor to AFTER errors so flushInline()'s restoreCursor() lands here
            out.saveCursor();
            // Re-render prompt below the errors
            _promptComponent->flushDeferredUpdates();
            auto pSize = _promptComponent->preferredSize();
            _promptComponent->setArea(
                tui::Rect { .x = 0, .y = 0, .width = _terminal.columns(), .height = pSize.height });
            _screen->draw();
            continue; // skip the normal needsRedraw path
        }

        if (needsRedraw)
        {
            _promptComponent->flushDeferredUpdates();
            auto pSize = _promptComponent->preferredSize();
            _promptComponent->setArea(
                tui::Rect { .x = 0, .y = 0, .width = _terminal.columns(), .height = pSize.height });
            _screen->draw();
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
            _promptComponent->setArea(
                tui::Rect { .x = 0, .y = 0, .width = _terminal.columns(), .height = pSize.height });
            _screen->draw();
            continue;
        }

        // Skip modifier-only key events (Ctrl, Alt, Shift, CapsLock, etc. pressed alone).
        if (auto const* key = std::get_if<tui::KeyEvent>(&event); key && tui::isModifierOnlyKey(key->key))
            continue;

        // Dispatch through Screen so mouse events reach PromptComponent::onEvent()
        auto const screenResult = _screen->dispatchEvent(event);
        if (screenResult == tui::EventResult::Handled && std::holds_alternative<tui::MouseEvent>(event))
        {
            _promptComponent->flushDeferredUpdates();
            auto pSize = _promptComponent->preferredSize();
            _promptComponent->setArea(
                tui::Rect { .x = 0, .y = 0, .width = _terminal.columns(), .height = pSize.height });
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
                    auto const linesToMoveDown =
                        totalLines - cursorLine - 1 + _promptComponent->bottomPadding();
                    if (linesToMoveDown > 0)
                        out.moveDown(linesToMoveDown);
                }
                out.carriageReturn();
                out.linefeed();
                out.enableReflow();
                out.flush();
                _promptComponent->clear();
                return result;
            }
            case PromptComponent::Action::Abort:
                _terminal.output().writeText("^C", {});
                _terminal.output().carriageReturn();
                _terminal.output().linefeed();
                _terminal.output().enableReflow();
                _terminal.output().flush();
                _promptComponent->clear();
                return std::string {};
            case PromptComponent::Action::Eof:
                _terminal.output().carriageReturn();
                _terminal.output().linefeed();
                _terminal.output().enableReflow();
                _terminal.output().flush();
                _aborted = true;
                return std::string {};
            case PromptComponent::Action::Changed: {
                _promptComponent->flushDeferredUpdates();
                auto pSize = _promptComponent->preferredSize();
                _promptComponent->setArea(
                    tui::Rect { .x = 0, .y = 0, .width = _terminal.columns(), .height = pSize.height });
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
                _promptComponent->flushDeferredUpdates();
                auto pSize = _promptComponent->preferredSize();
                _promptComponent->setArea(
                    tui::Rect { .x = 0, .y = 0, .width = _terminal.columns(), .height = pSize.height });
                _screen->draw();
                break;
            }
            case PromptComponent::Action::AgentMode: {
                _promptComponent->clear();
                _screen->clearAndRelease();
                _lastAction = PromptComponent::Action::AgentMode;
                return std::string {};
            }
            case PromptComponent::Action::CommandPalette:
                // Palette is shown internally by PromptComponent; just redraw.
                _promptComponent->flushDeferredUpdates();
                {
                    auto pSize = _promptComponent->preferredSize();
                    _promptComponent->setArea(
                        tui::Rect { .x = 0, .y = 0, .width = _terminal.columns(), .height = pSize.height });
                }
                _screen->draw();
                break;
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
    _promptComponent->setArea(
        tui::Rect { .x = 0, .y = 0, .width = _terminal.columns(), .height = prefSize.height });

    // Render
    _screen->draw();
    _displayDrewCurrentState = true;
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
        _terminal.output().updateDimensions(); // Refresh cached dimensions after child exit

        // Check if the command left the cursor at a non-column-1 position,
        // indicating output that didn't end with a newline. Show a dim indicator
        // (like fish shell) and move to a fresh line.
        if (auto const [row, col] = _terminal.queryCursorPosition(); col > 1)
            emitPartialLineIndicator(standardOutput(), col);

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

GitModule const* Prompt::gitModule() const noexcept
{
    return _promptComponent ? _promptComponent->gitModule() : nullptr;
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

void Prompt::setCommandRegistry(tui::CommandRegistry* registry)
{
    if (_promptComponent)
        _promptComponent->setCommandRegistry(registry);
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

/// @brief Writes text to the terminal output with per-grapheme syntax highlighting.
/// @param out The terminal output to write to.
/// @param text The source text to highlight and write.
static void writeSyntaxHighlighted(tui::TerminalOutput& out, std::string_view text)
{
    auto const highlights = computeHighlightMap(text);
    if (highlights.empty())
        return;

    // Group consecutive grapheme clusters with the same category into segments.
    // The highlight map is now per-grapheme-cluster, so we iterate grapheme
    // clusters and look up by grapheme index.
    auto segmenter = unicode::utf8_grapheme_segmenter(text);

    std::size_t graphemeIdx = 0;
    std::size_t segStartByte = 0;
    auto currentCat = highlights[0];

    for (auto it = segmenter.begin(); it != segmenter.end(); ++it, ++graphemeIdx)
    {
        auto const clusterByte = static_cast<std::size_t>(it._clusterStart - text.data());
        auto const cat = graphemeIdx < highlights.size() ? highlights[graphemeIdx] : TokenCategory::Default;

        if (graphemeIdx > 0 && cat != currentCat)
        {
            // Flush the previous segment
            out.writeText(text.substr(segStartByte, clusterByte - segStartByte),
                          tui::Style { .fg = categoryColor(currentCat) });
            segStartByte = clusterByte;
            currentCat = cat;
        }
    }

    // Flush remaining segment
    if (segStartByte < text.size())
        out.writeText(text.substr(segStartByte), tui::Style { .fg = categoryColor(currentCat) });
}

void Prompt::emitTransientPrompt(std::string_view inputText)
{
    if (_promptConfig.transient == TransientMode::Off)
        return;

    auto& out = _terminal.output();
    auto const topPad = _promptComponent->topPadding();
    auto const auroraH = _promptComponent->auroraFadeHeight();
    auto const chrome = _promptComponent->chromeHeight();
    auto const totalInputLines = _promptComponent->inputField().lineCount();
    auto const cursorLine = _promptComponent->inputField().cursorLine();
    auto const botPad = _promptComponent->bottomPadding();
    auto const totalHeight = topPad + auroraH + chrome + totalInputLines + botPad;
    auto const currentRow = topPad + auroraH + chrome + cursorLine;

    // Move cursor up to the top of the prompt region
    if (currentRow > 0)
        out.moveUp(currentRow);

    // Clear top padding rows to preserve vertical spacing
    for (auto i = 0; i < topPad; ++i)
    {
        out.carriageReturn();
        out.clearLine();
        out.moveDown(1);
    }

    // Write transient content at row topPad (after the padding)
    out.carriageReturn();
    out.clearLine();

    if (_promptConfig.transient == TransientMode::Arrow)
        out.writeText("\u276F ", tui::Style { .dim = true });

    // Show full command input, each line on its own terminal row
    auto linesWritten = 0;
    auto remaining = inputText;
    while (!remaining.empty())
    {
        auto const newlinePos = remaining.find('\n');
        auto const line = remaining.substr(0, newlinePos);
        if (linesWritten > 0)
        {
            out.moveDown(1);
            out.carriageReturn();
            out.clearLine();
        }
        writeSyntaxHighlighted(out, line);
        ++linesWritten;
        if (newlinePos == std::string_view::npos)
            break;
        remaining = remaining.substr(newlinePos + 1);
    }

    // Clear remaining rows (including padding rows with NBSP markers)
    for (auto i = topPad + linesWritten; i < totalHeight; ++i)
    {
        out.moveDown(1);
        out.carriageReturn();
        out.clearLine();
    }

    // Cursor is now at row (totalHeight - 1). Reposition so that the subsequent
    // \r\n in the submit handler preserves exactly the configured spacing.
    if (auto const excessRows = totalHeight - topPad - linesWritten - botPad; excessRows > 0)
        out.moveUp(excessRows);
}

void emitPartialLineIndicator(NativeHandle handle, int cursorColumn)
{
    if (cursorColumn <= 1)
        return;

    // SGR 2 (dim) + U+23CE (return symbol) + SGR 0 (reset) + CSI K (clear to EOL) + CR LF
    static constexpr std::string_view indicator = "\033[2m\u23CE\033[0m\033[K\r\n";
    platformWrite(handle, indicator.data(), indicator.size());
}

} // namespace endo
