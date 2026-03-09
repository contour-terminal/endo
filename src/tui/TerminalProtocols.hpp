// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file TerminalProtocols.hpp
/// @brief Terminal protocol escape sequence constants shared between platform implementations.

#include <string_view>

namespace tui::protocols
{

using namespace std::string_view_literals;

// Kitty keyboard protocol flags (CSI > flags u):
//   1 = Disambiguate escape codes
//   2 = Report event types (press, repeat, release)
//   4 = Report alternate keys
//   8 = Report all keys as escape codes
//  16 = Report associated text
//
// We use flags 1|4|8=13 to ensure modifiers are reported for all keys including Enter.
// Flag 4 is needed to receive the shifted_key for non-letter shifted characters
// (e.g., Shift+3→'#' sends key='3':shifted_key='#').
// Flag 8 is needed because some terminals only report Shift+Enter with this flag.
constexpr auto EnableCsiU =
    "\033[>13u"sv; ///< Enable Kitty keyboard protocol (disambiguate + alternate + all keys).
constexpr auto DisableCsiU = "\033[<u"sv; ///< Pop Kitty keyboard protocol.

constexpr auto EnableBracketedPaste = "\033[?2004h"sv;  ///< Enable bracketed paste mode.
constexpr auto DisableBracketedPaste = "\033[?2004l"sv; ///< Disable bracketed paste mode.

// Color scheme change notifications (DEC mode 2031)
// When enabled, the terminal sends CSI ? 997 ; N n when the palette changes
// N: 1 = dark, 2 = light
constexpr auto EnableColorSchemeNotify = "\033[?2031h"sv;  ///< Enable color scheme change notifications.
constexpr auto DisableColorSchemeNotify = "\033[?2031l"sv; ///< Disable color scheme change notifications.

// One-shot color scheme query: CSI ? 996 n
// Response: CSI ? 997 ; N n (same format as notifications)
constexpr auto QueryColorScheme = "\033[?996n"sv; ///< Query current color scheme.

// SGR extended mouse format (mode 1006) - allows coordinates > 223
// Uses CSI < button ; column ; row M/m format instead of legacy X10 encoding
constexpr auto EnableSGRMouse = "\033[?1006h"sv;  ///< Enable SGR extended mouse format.
constexpr auto DisableSGRMouse = "\033[?1006l"sv; ///< Disable SGR extended mouse format.

// Any-motion mouse tracking (mode 1003) - reports ALL mouse movements
// Required for hover tooltips - tracks mouse even without button held
// Without this, only button press/release events are reported
constexpr auto EnableAnyMotionTracking = "\033[?1003h"sv;  ///< Enable any-motion mouse tracking.
constexpr auto DisableAnyMotionTracking = "\033[?1003l"sv; ///< Disable any-motion mouse tracking.

// Passive mouse tracking (DEC mode 2029) - Contour terminal extension
// Adds an additional uiHandled parameter indicating whether the terminal UI
// consumed the event (e.g., for scrollback selection).
constexpr auto EnablePassiveMouseTracking = "\033[?2029h"sv;  ///< Enable passive mouse tracking.
constexpr auto DisablePassiveMouseTracking = "\033[?2029l"sv; ///< Disable passive mouse tracking.

// Focus tracking (DEC mode 1004)
// When enabled, the terminal sends CSI I on focus-in and CSI O on focus-out.
constexpr auto EnableFocusTracking = "\033[?1004h"sv;  ///< Enable focus in/out notifications.
constexpr auto DisableFocusTracking = "\033[?1004l"sv; ///< Disable focus in/out notifications.

// Win32 input mode (DEC private mode 9001) — Windows Terminal extension.
// Sends raw Win32 key events as CSI Vk ; Sc ; Uc ; Kd ; Cs ; Rc _ sequences,
// preserving full modifier fidelity that is lost in standard VT translation.
constexpr auto EnableWin32InputMode = "\033[?9001h"sv;  ///< Enable win32-input-mode.
constexpr auto DisableWin32InputMode = "\033[?9001l"sv; ///< Disable win32-input-mode.

} // namespace tui::protocols
