// SPDX-License-Identifier: Apache-2.0
#include <tui/MockTerminalOutput.hpp>
#include <tui/Terminal.hpp>

#if defined(_WIN32)

    #include <chrono>
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

Terminal::Terminal(): _output(std::make_unique<TerminalOutput>())
{
}

Terminal::Terminal(std::unique_ptr<TerminalOutput> output):
    _output(std::move(output)), _mockMode(dynamic_cast<MockTerminalOutput*>(_output.get()) != nullptr)
{
}

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

    // Query cell pixel dimensions (best-effort, non-fatal)
    auto const [cellWidth, cellHeight] = queryCellSize();
    _cellPixelWidth = cellWidth;
    _cellPixelHeight = cellHeight;

    // Wait for color scheme response so the first prompt renders with correct colors.
    // The original query from enableProtocols() may have been consumed by the raw
    // XTVERSION read in detectCapabilities(), so re-send it here.
    if (_colorScheme == ColorScheme::Unknown)
    {
        _output->writeRaw("\033[?996n"); // CSI ? 996 n — query color scheme
        _output->flush();
        auto constexpr totalTimeout = std::chrono::milliseconds(100);
        auto const deadline = std::chrono::steady_clock::now() + totalTimeout;
        while (_colorScheme == ColorScheme::Unknown)
        {
            auto const now = std::chrono::steady_clock::now();
            if (now >= deadline)
                break;
            auto const remaining =
                std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
            auto events = _input.poll(static_cast<int>(remaining));
            if (events.empty())
                break; // Real timeout — terminal doesn't support color scheme queries
            for (auto const& event: events)
            {
                if (auto const* csr = std::get_if<ColorSchemeReport>(&event))
                {
                    auto const scheme = (csr->mode == 2) ? ColorScheme::Light : ColorScheme::Dark;
                    handleColorSchemeReport(scheme);
                }
            }
        }
    }

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

auto Terminal::queryCursorPosition() -> std::pair<int, int>
{
    if (_mockMode)
    {
        if (auto* mock = dynamic_cast<MockTerminalOutput*>(_output.get()))
            return { mock->cursorRow() + 1, mock->cursorCol() + 1 }; // Convert 0-based to 1-based
        return { 0, 0 };
    }

    _output->requestCursorPosition();
    _output->flush();

    auto constexpr totalTimeout = std::chrono::milliseconds(100);
    auto const deadline = std::chrono::steady_clock::now() + totalTimeout;

    while (true)
    {
        auto const now = std::chrono::steady_clock::now();
        if (now >= deadline)
            break;

        auto const remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
        auto events = _input.poll(static_cast<int>(remaining));

        if (events.empty())
            break;

        for (auto const& event: events)
        {
            if (auto const* cpr = std::get_if<CursorPositionReport>(&event))
                return { cpr->row, cpr->column };

            if (auto const* csr = std::get_if<ColorSchemeReport>(&event))
            {
                auto const scheme = (csr->mode == 2) ? ColorScheme::Light : ColorScheme::Dark;
                handleColorSchemeReport(scheme);
            }
        }
    }

    return { 0, 0 };
}

auto Terminal::queryCellSize() -> std::pair<int, int>
{
    if (_mockMode)
        return { 0, 0 };

    _output->requestCellSize();
    _output->flush();

    auto constexpr totalTimeout = std::chrono::milliseconds(100);
    auto const deadline = std::chrono::steady_clock::now() + totalTimeout;

    while (true)
    {
        auto const now = std::chrono::steady_clock::now();
        if (now >= deadline)
            break;

        auto const remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
        auto events = _input.poll(static_cast<int>(remaining));

        if (events.empty())
            break;

        for (auto const& event: events)
        {
            if (auto const* csr = std::get_if<CellSizeReport>(&event))
                return { csr->width, csr->height };

            if (auto const* cs = std::get_if<ColorSchemeReport>(&event))
            {
                auto const scheme = (cs->mode == 2) ? ColorScheme::Light : ColorScheme::Dark;
                handleColorSchemeReport(scheme);
            }
        }
    }

    return { 0, 0 };
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

auto Terminal::queryDecMode(int /*mode*/) -> bool
{
    // Not yet implemented on Windows
    return false;
}

} // namespace tui

#endif // _WIN32
