// SPDX-License-Identifier: Apache-2.0
#include <tui/MockTerminalOutput.hpp>
#include <tui/Terminal.hpp>

#include <chrono>
#include <csignal>
#include <tuple>

#include <unistd.h>

namespace tui
{

namespace
{
    // Global pointer for SIGWINCH handler to notify the TerminalInput instance.
    // Only one Terminal instance should be active at a time.
    TerminalInput* gActiveInput = nullptr; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
    struct sigaction gPrevSigwinch {};     // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

    void sigwinchHandler(int /*sig*/)
    {
        if (gActiveInput != nullptr)
            gActiveInput->notifyResize(0, 0); // Actual dimensions are queried in poll()
    }
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

    // In mock mode, skip input initialization, SIGWINCH handler, and cell size query.
    if (_mockMode)
    {
        _initialized = true;
        return {};
    }

    // Initialize input (raw mode, protocols — ECHO off from here)
    if (auto result = _input.initialize(); !result)
        return result;

    // Detect capabilities that require query/response I/O (e.g., XTVERSION).
    // Must run after raw mode is enabled so response bytes aren't echoed.
    _output->detectCapabilities();

    // Install SIGWINCH handler
    gActiveInput = &_input;
    struct sigaction sa {};
    sa.sa_handler = sigwinchHandler;
    sa.sa_flags = SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGWINCH, &sa, &gPrevSigwinch);

    // Query cell pixel dimensions (best-effort, non-fatal)
    auto const [cellWidth, cellHeight] = queryCellSize();
    _cellPixelWidth = cellWidth;
    _cellPixelHeight = cellHeight;

    // Detect HUD overlay support (DEC mode 2035, Contour terminal)
    _hudSupported = queryDecMode(2035);

    // Detect passive mouse tracking support (DEC mode 2029).
    // When supported, also enable any-motion tracking (mode 1003) for hover tooltips.
    // Non-supporting terminals silently ignored mode 2029 in enableProtocols(),
    // so we only add 1003 when the terminal actually recognized it.
    if (queryDecMode(2029))
        _input.setAnyMotionTracking(true);

    // Wait for color scheme response so the first prompt renders with correct colors.
    // The original query from enableProtocols() may have been consumed by the raw
    // XTVERSION read in detectCapabilities(), so re-send it here.
    if (_colorScheme == ColorScheme::Unknown)
    {
        _output->writeRaw("\033[?996n"); // CSI ? 996 n — query color scheme
        _output->flush();
        auto constexpr TotalTimeout = std::chrono::milliseconds(100);
        auto const deadline = std::chrono::steady_clock::now() + TotalTimeout;
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
        // Restore previous SIGWINCH handler
        sigaction(SIGWINCH, &gPrevSigwinch, nullptr);
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

    // Send DSR (Device Status Report) to query cursor position.
    // Response will be: CSI row ; col R
    _output->requestCursorPosition();
    _output->flush();

    // Poll with a deadline loop. The ColorSchemeReport from enableProtocols() may arrive
    // before the CursorPositionReport, so we keep polling until we find a CPR or time out.
    auto constexpr TotalTimeout = std::chrono::milliseconds(100);
    auto const deadline = std::chrono::steady_clock::now() + TotalTimeout;

    while (true)
    {
        auto const now = std::chrono::steady_clock::now();
        if (now >= deadline)
            break;

        auto const remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
        auto events = _input.poll(static_cast<int>(remaining));

        if (events.empty())
            break; // Real timeout — no more data coming

        for (auto const& event: events)
        {
            if (auto const* cpr = std::get_if<CursorPositionReport>(&event))
                return { cpr->row, cpr->column };

            // Handle ColorSchemeReport inline so it isn't dropped
            if (auto const* csr = std::get_if<ColorSchemeReport>(&event))
            {
                auto const scheme = (csr->mode == 2) ? ColorScheme::Light : ColorScheme::Dark;
                handleColorSchemeReport(scheme);
            }
        }
    }

    // Failed to get response within timeout
    return { 0, 0 };
}

auto Terminal::queryCellSize() -> std::pair<int, int>
{
    if (_mockMode)
        return { 0, 0 };

    // Send CSI 16 t to query cell pixel dimensions.
    // Response will be: CSI 6 ; height ; width t
    _output->requestCellSize();
    _output->flush();

    auto constexpr TotalTimeout = std::chrono::milliseconds(100);
    auto const deadline = std::chrono::steady_clock::now() + TotalTimeout;

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

            // Handle ColorSchemeReport inline so it isn't dropped
            if (auto const* cs = std::get_if<ColorSchemeReport>(&event))
            {
                auto const scheme = (cs->mode == 2) ? ColorScheme::Light : ColorScheme::Dark;
                handleColorSchemeReport(scheme);
            }
        }
    }

    return { 0, 0 };
}

auto Terminal::queryDecMode(int mode) -> bool
{
    if (_mockMode)
        return false;

    // Send DECRQM: CSI ? mode $ p
    // Response: CSI ? mode ; status $ y (DecModeReport)
    // Status 1 = set, 2 = reset (both mean the mode is recognized/supported)
    _output->requestDecMode(mode);
    _output->flush();

    auto constexpr TotalTimeout = std::chrono::milliseconds(100);
    auto const deadline = std::chrono::steady_clock::now() + TotalTimeout;

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
            if (auto const* dmr = std::get_if<DecModeReport>(&event))
            {
                if (dmr->mode == mode)
                    return dmr->status == 1 || dmr->status == 2;
            }

            // Handle ColorSchemeReport inline so it isn't dropped
            if (auto const* cs = std::get_if<ColorSchemeReport>(&event))
            {
                auto const scheme = (cs->mode == 2) ? ColorScheme::Light : ColorScheme::Dark;
                handleColorSchemeReport(scheme);
            }
        }
    }

    return false;
}

auto Terminal::hudSupported() const noexcept -> bool
{
    return _hudSupported;
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

} // namespace tui
