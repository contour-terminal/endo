// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tui/KeyCode.hpp>
#include <tui/Modifier.hpp>

#include <cstdint>
#include <string>
#include <variant>

namespace tui
{

/// @brief Keyboard input event.
struct KeyEvent
{
    KeyCode key {};                      ///< Key code (printable uses codepoint, special keys use enum).
    Modifier modifiers = Modifier::None; ///< Active modifier keys.
    char32_t codepoint = 0;              ///< Original Unicode codepoint (0 for non-printable keys).
};

/// @brief Mouse input event.
struct MouseEvent
{
    /// @brief Type of mouse action.
    enum class Type : std::uint8_t
    {
        Press,
        Release,
        Move,
        ScrollUp,
        ScrollDown,
    };

    Type type {};                        ///< Action type.
    int button = 0;                      ///< Button: 0=left, 1=middle, 2=right.
    int x = 0;                           ///< Column (1-based).
    int y = 0;                           ///< Row (1-based).
    Modifier modifiers = Modifier::None; ///< Active modifier keys.
    bool uiHandled = false;              ///< True if terminal UI consumed this event.
};

/// @brief Terminal resize event.
struct ResizeEvent
{
    int columns; ///< New terminal width in columns.
    int rows;    ///< New terminal height in rows.
};

/// @brief Bracketed paste event.
struct PasteEvent
{
    std::string text; ///< Pasted text content.
};

/// @brief Cursor position report event (response to DSR).
struct CursorPositionReport
{
    int row;    ///< Cursor row (1-based).
    int column; ///< Cursor column (1-based).
};

/// @brief Color scheme report from the terminal (CSI ? 997 ; N n).
///
/// This is an internal event consumed by Terminal::poll() and not propagated
/// to application code. It signals a dark/light mode change.
struct ColorSchemeReport
{
    int mode; ///< 1 = dark, 2 = light.
};

/// @brief Cell size report from the terminal (response to CSI 16 t).
///
/// Reports the cell dimensions in pixels. This is an internal event
/// consumed by Terminal during initialization and not propagated to
/// application code.
struct CellSizeReport
{
    int height; ///< Cell height in pixels.
    int width;  ///< Cell width in pixels.
};

/// @brief Discriminated union of all possible terminal input events.
using InputEvent = std::variant<KeyEvent,
                                MouseEvent,
                                ResizeEvent,
                                PasteEvent,
                                CursorPositionReport,
                                ColorSchemeReport,
                                CellSizeReport>;

} // namespace tui
