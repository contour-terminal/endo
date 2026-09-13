// SPDX-License-Identifier: Apache-2.0
#include <tui/Terminal.hpp>

#include <csignal>
#include <tuple>
#include <variant>

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

    /// Whether a DECRQM answer makes the mode usable: recognized and changeable (set or reset).
    /// A permanent state or anything that is not an answer reads as unusable, as it did when this
    /// was a bool.
    [[nodiscard]] constexpr auto isChangeable(DecModeStatus status) noexcept -> bool
    {
        return status == DecModeStatus::Set || status == DecModeStatus::Reset;
    }
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

    // Query cell pixel dimensions (best-effort, non-fatal; unanswered leaves them 0, "unknown")
    if (auto const cellSize = queryCellSize())
    {
        _cellPixelWidth = cellSize->first;
        _cellPixelHeight = cellSize->second;
    }

    // Detect HUD overlay support (DEC mode 2035, Contour terminal)
    _hudSupported = isChangeable(queryDecMode(2035));

    // Detect passive mouse tracking support (DEC mode 2029).
    // When supported, also enable any-motion tracking (mode 1003) for hover tooltips.
    // Non-supporting terminals silently ignored mode 2029 in enableProtocols(),
    // so we only add 1003 when the terminal actually recognized it.
    if (isChangeable(queryDecMode(2029)))
        _input.setAnyMotionTracking(true);

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
