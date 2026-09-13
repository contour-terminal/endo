// SPDX-License-Identifier: Apache-2.0
#include <tui/InputEvent.hpp>
#include <tui/MockTerminalOutput.hpp>
#include <tui/Terminal.hpp>

#include <chrono>
#include <tuple>
#include <variant>

#include <platform/Clock.hpp>

/// @file
/// Platform-agnostic @c Terminal members. These contain no OS-specific code, so
/// they live in one translation unit compiled on every platform rather than
/// being duplicated in each `Terminal*.cpp`. Keeping the protocol-report policy
/// here (atop the shared @c tui::isProtocolReport predicate) ensures POSIX and
/// Windows classify and dispatch terminal reports identically.

namespace tui
{

namespace
{
    /// How long a query waits for its reply, on the injected clock.
    constexpr auto QueryReplyTimeout = std::chrono::milliseconds(100);
} // namespace

Terminal::Terminal():
    _output(std::make_unique<TerminalOutput>()), _clock(endo::platform::defaultSteadyClock())
{
}

Terminal::Terminal(std::unique_ptr<TerminalOutput> output):
    _output(std::move(output)),
    _clock(endo::platform::defaultSteadyClock()),
    _mockMode(dynamic_cast<MockTerminalOutput*>(_output.get()) != nullptr)
{
}

Terminal::Terminal(std::unique_ptr<TerminalOutput> output,
                   TerminalQueryInput& queryInput,
                   endo::platform::IClock& clock):
    _output(std::move(output)),
    _clock(clock),
    _queryInput(&queryInput),
    _mockMode(dynamic_cast<MockTerminalOutput*>(_output.get()) != nullptr)
{
}

auto Terminal::canQuery() const noexcept -> bool
{
    return _queryInput != nullptr || !_mockMode;
}

auto Terminal::awaitReport(std::function<bool(InputEvent const&)> const& isReply) -> std::optional<InputEvent>
{
    auto reply = std::optional<InputEvent> {};
    auto others = std::vector<InputEvent> {};
    auto const deadline = _clock.now() + QueryReplyTimeout;

    // The deadline alone ends the wait. An empty poll is not a timeout by itself -- a resize, a
    // wakeup or half of an escape sequence all return early with nothing decoded -- so it loops.
    while (!reply)
    {
        auto const now = _clock.now();
        if (now >= deadline)
            break;

        // Rounded UP: a sub-millisecond remainder truncated to 0 makes the last wait a spin.
        auto const remaining = std::chrono::ceil<std::chrono::milliseconds>(deadline - now).count();
        auto events = _queryInput != nullptr ? _queryInput->poll(static_cast<int>(remaining))
                                             : _input.poll(static_cast<int>(remaining));
        for (auto& event: events)
        {
            if (!reply && isReply(event))
                reply = std::move(event);
            else
                others.push_back(std::move(event));
        }
    }

    std::ignore = consumeProtocolReports(others);
    if (!others.empty())
    {
        if (_queryInput != nullptr)
            _queryInput->unread(std::move(others));
        else
            _input.unread(std::move(others));
    }
    return reply;
}

void Terminal::awaitColorScheme()
{
    if (_colorScheme != ColorScheme::Unknown || !canQuery())
        return;

    _output->writeRaw("\033[?996n"); // CSI ? 996 n -- query color scheme
    _output->flush();
    auto reply =
        awaitReport([](InputEvent const& event) { return std::holds_alternative<ColorSchemeReport>(event); });
    if (reply)
    {
        auto events = std::vector<InputEvent> { std::move(*reply) };
        std::ignore = consumeProtocolReports(events);
    }
}

auto Terminal::queryCursorPosition() -> std::expected<std::pair<int, int>, QueryUnanswered>
{
    if (!canQuery())
    {
        if (auto* mock = dynamic_cast<MockTerminalOutput*>(_output.get()))
            return std::pair { mock->cursorRow() + 1, mock->cursorCol() + 1 }; // Convert 0-based to 1-based
        return std::unexpected(QueryUnanswered::NotAsked);
    }

    // Send DSR (Device Status Report) to query cursor position.
    // Response will be: CSI row ; col R
    _output->requestCursorPosition();
    _output->flush();

    auto const reply = awaitReport(
        [](InputEvent const& event) { return std::holds_alternative<CursorPositionReport>(event); });
    if (!reply)
        return std::unexpected(QueryUnanswered::NoReply);
    auto const& report = std::get<CursorPositionReport>(*reply);
    return std::pair { report.row, report.column };
}

auto Terminal::queryCellSize() -> std::expected<std::pair<int, int>, QueryUnanswered>
{
    if (!canQuery())
        return std::unexpected(QueryUnanswered::NotAsked);

    // Send CSI 16 t to query cell pixel dimensions.
    // Response will be: CSI 6 ; height ; width t
    _output->requestCellSize();
    _output->flush();

    auto const reply =
        awaitReport([](InputEvent const& event) { return std::holds_alternative<CellSizeReport>(event); });
    if (!reply)
        return std::unexpected(QueryUnanswered::NoReply);
    auto const& report = std::get<CellSizeReport>(*reply);
    return std::pair { report.width, report.height };
}

auto Terminal::queryDecMode(int mode) -> DecModeStatus
{
    if (!canQuery())
        return DecModeStatus::NotAsked;

    // Send DECRQM: CSI ? mode $ p
    // Response: CSI ? mode ; status $ y (DecModeReport)
    //
    // Shared by both platform arms. On Windows the console reads with ENABLE_VIRTUAL_TERMINAL_INPUT,
    // so the reply arrives in the input stream exactly as on POSIX and the same loop reads it. A
    // console that does not answer DECRQM is NoReply at the deadline -- one bounded wait, never a hang.
    _output->requestDecMode(mode);
    _output->flush();

    auto const reply = awaitReport([mode](InputEvent const& event) {
        auto const* report = std::get_if<DecModeReport>(&event);
        return report != nullptr && report->mode == mode;
    });
    if (!reply)
        return DecModeStatus::NoReply;
    return decModeStatusFromReply(std::get<DecModeReport>(*reply).status);
}

auto Terminal::queryDeviceAttributes() -> std::expected<DeviceAttributesReport, QueryUnanswered>
{
    if (!canQuery())
        return std::unexpected(QueryUnanswered::NotAsked);

    // Send DA1: CSI c. Response: CSI ? p1 ; p2 ; ... c
    _output->requestDeviceAttributes();
    _output->flush();

    auto reply = awaitReport(
        [](InputEvent const& event) { return std::holds_alternative<DeviceAttributesReport>(event); });
    if (!reply)
        return std::unexpected(QueryUnanswered::NoReply);
    return std::get<DeviceAttributesReport>(std::move(*reply));
}

bool Terminal::handleFocusEvent(bool focused)
{
    if (focused == _focused)
        return false;

    _focused = focused;
    for (auto const& cb: _focusCallbacks)
        cb(focused);
    return true;
}

bool Terminal::consumeProtocolReports(std::vector<InputEvent>& events)
{
    // Consume protocol-level response events internally — do not pass to application.
    // The color-scheme and focus reports are dispatched to their handlers; the
    // remaining reports (cursor-position, cell-size, DEC-mode, DCS) are dropped,
    // their values having been consumed synchronously by the query methods.
    auto focusChanged = false;
    std::erase_if(events, [&](InputEvent const& event) {
        if (auto const* csr = std::get_if<ColorSchemeReport>(&event))
        {
            auto const scheme = (csr->mode == 2) ? ColorScheme::Light : ColorScheme::Dark;
            handleColorSchemeReport(scheme);
            return true;
        }
        if (auto const* fe = std::get_if<FocusEvent>(&event))
        {
            focusChanged = handleFocusEvent(fe->focused) || focusChanged;
            return true;
        }
        return isProtocolReport(event);
    });
    return focusChanged;
}

} // namespace tui
