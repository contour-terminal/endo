// SPDX-License-Identifier: Apache-2.0
#include <tui/InputEvent.hpp>
#include <tui/Terminal.hpp>

/// @file
/// Platform-agnostic @c Terminal members. These contain no OS-specific code, so
/// they live in one translation unit compiled on every platform rather than
/// being duplicated in each `Terminal*.cpp`. Keeping the protocol-report policy
/// here (atop the shared @c tui::isProtocolReport predicate) ensures POSIX and
/// Windows classify and dispatch terminal reports identically.

namespace tui
{

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
