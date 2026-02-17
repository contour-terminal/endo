// SPDX-License-Identifier: Apache-2.0
#include <tui/Terminal.hpp>

#include <chrono>
#include <csignal>

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

Terminal::Terminal() = default;

Terminal::~Terminal()
{
    shutdown();
}

auto Terminal::initialize() -> VoidResult
{
    if (_initialized)
        return {};

    // Initialize output first (queries dimensions)
    if (auto result = _output.initialize(); !result)
        return result;

    // Initialize input (raw mode, protocols)
    if (auto result = _input.initialize(); !result)
        return result;

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

    _initialized = true;
    return {};
}

void Terminal::shutdown()
{
    if (!_initialized)
        return;

    // Restore previous SIGWINCH handler
    sigaction(SIGWINCH, &gPrevSigwinch, nullptr);
    gActiveInput = nullptr;

    _input.shutdown();
    _initialized = false;
}

auto Terminal::input() noexcept -> TerminalInput&
{
    return _input;
}

auto Terminal::output() noexcept -> TerminalOutput&
{
    return _output;
}

auto Terminal::poll(int timeoutMs) -> std::vector<InputEvent>
{
    auto events = _input.poll(timeoutMs);

    // Consume protocol-level response events internally — do not pass to application
    std::erase_if(events, [this](InputEvent const& event) {
        if (auto const* csr = std::get_if<ColorSchemeReport>(&event))
        {
            auto const scheme = (csr->mode == 2) ? ColorScheme::Light : ColorScheme::Dark;
            handleColorSchemeReport(scheme);
            return true;
        }
        if (std::holds_alternative<CursorPositionReport>(event))
            return true;
        if (std::holds_alternative<CellSizeReport>(event))
            return true;
        return false;
    });

    return events;
}

auto Terminal::columns() const noexcept -> int
{
    return _output.columns();
}

auto Terminal::rows() const noexcept -> int
{
    return _output.rows();
}

void Terminal::suspend()
{
    if (_initialized)
        _input.suspend();
}

void Terminal::resume()
{
    if (_initialized)
        _input.resume();
}

auto Terminal::isSuspended() const noexcept -> bool
{
    return _input.isSuspended();
}

auto Terminal::queryCursorPosition() -> std::pair<int, int>
{
    // Send DSR (Device Status Report) to query cursor position.
    // Response will be: CSI row ; col R
    _output.writeRaw("\033[6n");
    _output.flush();

    // Poll with a deadline loop. The ColorSchemeReport from enableProtocols() may arrive
    // before the CursorPositionReport, so we keep polling until we find a CPR or time out.
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
    // Send CSI 16 t to query cell pixel dimensions.
    // Response will be: CSI 6 ; height ; width t
    _output.writeRaw("\033[16t");
    _output.flush();

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

} // namespace tui
