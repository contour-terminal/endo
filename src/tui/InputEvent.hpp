// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <string>
#include <variant>

#include <tui/KeyCode.hpp>
#include <tui/Modifier.hpp>

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

/// @brief Discriminated union of all possible terminal input events.
using InputEvent = std::variant<KeyEvent, MouseEvent, ResizeEvent, PasteEvent>;

} // namespace tui
