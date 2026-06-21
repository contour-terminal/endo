// SPDX-License-Identifier: Apache-2.0
#include <tui/runtime/TerminalEventSource.hpp>

#if defined(_WIN32)

    #include <array>
    #include <iterator>

    #include <windows.h>

namespace tui::runtime
{

WaitOutcome TerminalEventSource::wait(int timeoutMs)
{
    auto& input = _terminal.input();

    // Build the wait set: input, resize, agent wakeup, interrupt wakeup. Unlike
    // the legacy TerminalInput::poll(), the agent and interrupt wakeups ARE
    // included, so an agent message or Ctrl+C wakes the wait immediately.
    auto handles = std::array<HANDLE, 4> {};
    auto count = DWORD { 0 };

    handles[count++] = input.inputNativeHandle();

    // The resize event is null until the terminal is initialized; WaitForMultipleObjects
    // rejects a null handle, so only include it once it is a real event. (Its absent
    // value is nullptr, not InvalidHandle/INVALID_HANDLE_VALUE.)
    auto const resizeHandle = input.resizeNativeHandle();
    if (resizeHandle != nullptr && resizeHandle != endo::platform::InvalidHandle)
        handles[count++] = resizeHandle;

    if (_agentWakeup != nullptr)
        handles[count++] = _agentWakeup->nativeHandle();

    if (_interruptWakeup != nullptr)
        handles[count++] = _interruptWakeup->nativeHandle();

    auto const timeout = (timeoutMs < 0) ? INFINITE : static_cast<DWORD>(timeoutMs);
    auto const waitResult = WaitForMultipleObjects(count, handles.data(), FALSE, timeout);

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

    return finalize(std::move(outcome));
}

} // namespace tui::runtime

#endif // _WIN32
