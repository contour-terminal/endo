// SPDX-License-Identifier: Apache-2.0
#include <csignal>

#include <unistd.h>

#include <tui/Terminal.hpp>

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
    return _input.poll(timeoutMs);
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

} // namespace tui
