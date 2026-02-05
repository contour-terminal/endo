// SPDX-License-Identifier: Apache-2.0
#include "Prompt.hpp"

#include <unistd.h>

namespace endo
{

Prompt::Prompt() = default;

Prompt::~Prompt()
{
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

    // Set up clipboard callback to use OSC 52
    _inputField.setClipboardCallback([this](std::string_view text) {
        _terminal.output().copyToClipboard(text);
        _terminal.output().flush();
    });

    _inputField.setPrompt(_promptStr);
    _initialized = true;
}

std::string Prompt::read()
{
    initialize();
    if (_aborted)
        return {};

    // Clear any previous input
    _inputField.clear();
    _inputField.setPrompt(_promptStr);

    render();

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
                render();
                continue;
            }

            auto action = _inputField.processEvent(event);

            switch (action)
            {
                case tui::InputFieldAction::Submit: {
                    // Move to next line before returning
                    _terminal.output().writeRaw("\r\n");
                    _terminal.output().flush();
                    auto result = std::string(_inputField.text());
                    return result;
                }
                case tui::InputFieldAction::Abort:
                    // Ctrl+C - clear line and return empty
                    _terminal.output().writeRaw("^C\r\n");
                    _terminal.output().flush();
                    _inputField.clear();
                    return {};
                case tui::InputFieldAction::Eof:
                    // Ctrl+D on empty line - signal EOF
                    _terminal.output().writeRaw("\r\n");
                    _terminal.output().flush();
                    _aborted = true;
                    return {};
                case tui::InputFieldAction::Changed: render(); break;
                case tui::InputFieldAction::None: break;
            }
        }
    }

    return {};
}

void Prompt::setPrompt(std::string_view promptStr)
{
    _promptStr = std::string(promptStr);
    if (_initialized)
        _inputField.setPrompt(_promptStr);
}

void Prompt::addHistory(std::string entry)
{
    _inputField.addHistory(std::move(entry));
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
            render();
            continue;
        }

        auto action = _inputField.processEvent(event);

        switch (action)
        {
            case tui::InputFieldAction::Submit: {
                _terminal.output().writeRaw("\r\n");
                _terminal.output().flush();
                auto result = std::string(_inputField.text());
                _inputField.clear();
                return result;
            }
            case tui::InputFieldAction::Abort:
                _terminal.output().writeRaw("^C\r\n");
                _terminal.output().flush();
                _inputField.clear();
                return std::string {}; // Return empty string, not nullopt
            case tui::InputFieldAction::Eof:
                _terminal.output().writeRaw("\r\n");
                _terminal.output().flush();
                _aborted = true;
                return std::string {};
            case tui::InputFieldAction::Changed: render(); break;
            case tui::InputFieldAction::None: break;
        }
    }

    return std::nullopt;
}

void Prompt::onResize()
{
    _terminal.output().updateDimensions();
}

void Prompt::render()
{
    // Simple single-line rendering for now
    // TODO: Support multiline rendering with selection highlighting

    auto& out = _terminal.output();

    // Move to start of line, clear it
    out.writeRaw("\r");
    out.clearToEndOfLine();

    // Write prompt
    out.writeRaw(_promptStr);

    // Write input text
    out.writeRaw(_inputField.text());

    // Position cursor
    // Calculate cursor position in display columns
    auto const text = _inputField.text();
    auto const cursorPos = _inputField.cursor();
    auto const textBeforeCursor = text.substr(0, cursorPos);

    // Simple calculation: count bytes (good enough for ASCII, approximate for UTF-8)
    // TODO: Use proper grapheme width calculation
    auto displayPos = _promptStr.size() + textBeforeCursor.size();

    // Move cursor to correct position
    out.writeRaw("\r");
    if (displayPos > 0)
        out.moveRight(static_cast<int>(displayPos));

    out.flush();
}

} // namespace endo
