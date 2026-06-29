// SPDX-License-Identifier: Apache-2.0
#include <tui/runtime/TerminalEventSource.hpp>

#if defined(_WIN32)

    #include <iterator>
    #include <vector>

    #include <windows.h>

namespace tui::runtime
{

WaitOutcome TerminalEventSource::wait(int timeoutMs)
{
    auto& input = _terminal.input();

    // Build the wait set: the terminal's own sources (input, resize, agent wakeup,
    // interrupt wakeup) followed by user-registered fds. Unlike the legacy
    // TerminalInput::poll(), the agent and interrupt wakeups ARE included, so an
    // agent message or Ctrl+C wakes the wait immediately. WaitForMultipleObjects
    // rejects null handles, so a handle is only added once it is real.
    auto handles = std::vector<HANDLE> {};
    handles.reserve(4 + _registrations.size());

    handles.push_back(input.inputNativeHandle());

    // The resize event is null until the terminal is initialized. (Its absent
    // value is nullptr, not InvalidHandle/INVALID_HANDLE_VALUE.)
    auto const resizeHandle = input.resizeNativeHandle();
    if (resizeHandle != nullptr && resizeHandle != endo::platform::InvalidHandle)
        handles.push_back(resizeHandle);

    if (_agentWakeup != nullptr)
        handles.push_back(_agentWakeup->nativeHandle());

    if (_interruptWakeup != nullptr)
        handles.push_back(_interruptWakeup->nativeHandle());

    // User fds: only those with a real handle and non-empty interest take part.
    for (auto const& reg: _registrations)
        if (reg.fd != nullptr && reg.fd != endo::platform::InvalidHandle && reg.interest != FdInterest::None)
            handles.push_back(reg.fd);

    auto const timeout = (timeoutMs < 0) ? INFINITE : static_cast<DWORD>(timeoutMs);
    auto const waitResult =
        WaitForMultipleObjects(static_cast<DWORD>(handles.size()), handles.data(), FALSE, timeout);

    auto outcome = WaitOutcome {};
    if (waitResult == WAIT_TIMEOUT)
    {
        outcome.events = input.parserTimeout();
        return finalize(std::move(outcome));
    }
    if (waitResult == WAIT_FAILED)
    {
        // A failed wait must not busy-loop; report it as an interrupt so the
        // runtime unwinds the root flow rather than re-entering wait() tightly.
        outcome.interrupted = true;
        return finalize(std::move(outcome));
    }

    auto const signalled = [](HANDLE handle) {
        return handle != nullptr && WaitForSingleObject(handle, 0) == WAIT_OBJECT_0;
    };

    if (auto const resize = input.drainResize())
        outcome.events.emplace_back(*resize);

    if (_agentWakeup != nullptr && signalled(_agentWakeup->nativeHandle()))
    {
        _agentWakeup->reset();
        outcome.agentReady = true;
    }

    if (_interruptWakeup != nullptr && signalled(_interruptWakeup->nativeHandle()))
        _interruptWakeup->reset();

    // The console input handle stays signalled for non-key records too; reading
    // is non-blocking and returns nothing if there is no decodable input.
    auto parsed = input.readReadyInput();
    if (outcome.events.empty())
        outcome.events = std::move(parsed);
    else
        outcome.events.insert(outcome.events.end(),
                              std::make_move_iterator(parsed.begin()),
                              std::make_move_iterator(parsed.end()));

    // Route user-registered fds: a signalled handle satisfies its interest. The
    // socket layer maps precise socket events onto a waitable event via
    // WSAEventSelect, so "signalled" here means "the requested readiness occurred".
    for (auto const& reg: _registrations)
    {
        if (!signalled(reg.fd))
            continue;
        if (hasInterest(reg.interest, FdInterest::Read))
            outcome.readyRead.push_back(reg.token);
        if (hasInterest(reg.interest, FdInterest::Write))
            outcome.readyWrite.push_back(reg.token);
    }

    return finalize(std::move(outcome));
}

} // namespace tui::runtime

#endif // _WIN32
