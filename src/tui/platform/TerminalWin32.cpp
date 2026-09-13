// SPDX-License-Identifier: Apache-2.0
#include <tui/Terminal.hpp>

#if defined(_WIN32)

    #include <tuple>

    #include <windows.h>

namespace tui
{

namespace
{
    // Global pointer for resize notification from external sources.
    // Only one Terminal instance should be active at a time.
    TerminalInput* gActiveInput = nullptr; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
} // namespace

Terminal::~Terminal()
{
    shutdown();
}

auto Terminal::initialize() -> VoidResult
{
    if (_initialized)
        return {};

    // Initialize output first (queries dimensions)
    if (auto result = _output->initialize(); !result)
        return result;

    // In mock mode, skip input initialization and capability queries.
    if (_mockMode)
    {
        _initialized = true;
        return {};
    }

    // Initialize input (raw mode, protocols)
    if (auto result = _input.initialize(); !result)
        return result;

    // No SIGWINCH handler needed on Windows — resize events arrive
    // via WINDOW_BUFFER_SIZE_EVENT in TerminalInput::poll() directly.
    gActiveInput = &_input;

    // Query cell pixel dimensions (best-effort, non-fatal; unanswered leaves them 0, "unknown")
    if (auto const cellSize = queryCellSize())
    {
        _cellPixelWidth = cellSize->first;
        _cellPixelHeight = cellSize->second;
    }

    // Wait for color scheme response so the first prompt renders with correct colors.
    // The original query from enableProtocols() may have been consumed by the raw
    // XTVERSION read in detectCapabilities(), so re-send it here.
    awaitColorScheme();

    _initialized = true;
    return {};
}

void Terminal::shutdown()
{
    if (!_initialized)
        return;

    if (!_mockMode)
    {
        gActiveInput = nullptr;
        _input.shutdown();
    }
    _initialized = false;
}

auto Terminal::input() noexcept -> TerminalInput&
{
    return _input;
}

auto Terminal::output() noexcept -> TerminalOutput&
{
    return *_output;
}

auto Terminal::poll(int timeoutMs) -> std::vector<InputEvent>
{
    if (_mockMode)
        return {};

    auto events = _input.poll(timeoutMs);
    std::ignore = consumeProtocolReports(events);
    return events;
}

auto Terminal::columns() const noexcept -> int
{
    return _output->columns();
}

auto Terminal::rows() const noexcept -> int
{
    return _output->rows();
}

void Terminal::suspend()
{
    if (_initialized && !_mockMode)
        _input.suspend();
}

void Terminal::resume()
{
    if (_initialized && !_mockMode)
        _input.resume();
}

auto Terminal::isSuspended() const noexcept -> bool
{
    return _input.isSuspended();
}

auto Terminal::cellPixelWidth() const noexcept -> int
{
    return _cellPixelWidth;
}

auto Terminal::cellPixelHeight() const noexcept -> int
{
    return _cellPixelHeight;
}

auto Terminal::colorScheme() const noexcept -> ColorScheme
{
    return _colorScheme;
}

void Terminal::onColorSchemeChanged(std::function<void(ColorScheme)> callback)
{
    _colorSchemeCallbacks.push_back(std::move(callback));
}

void Terminal::handleColorSchemeReport(ColorScheme scheme)
{
    if (scheme == _colorScheme)
        return;

    _colorScheme = scheme;
    for (auto const& cb: _colorSchemeCallbacks)
        cb(scheme);
}

auto Terminal::isFocused() const noexcept -> bool
{
    return _focused;
}

void Terminal::onFocusChanged(std::function<void(bool)> callback)
{
    _focusCallbacks.push_back(std::move(callback));
}

auto Terminal::hudSupported() const noexcept -> bool
{
    return _hudSupported;
}

} // namespace tui

#endif // _WIN32
