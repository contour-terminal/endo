// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

namespace tui
{

/// @brief Key codes for keyboard events.
///
/// Printable characters use their Unicode codepoint directly (cast to KeyCode).
/// Non-printable keys use values in the 0x10000+ range to avoid collision.
enum class KeyCode : std::uint32_t
{
    // Non-printable keys (above Unicode BMP to avoid collision with codepoints)
    Enter = 0x10000,
    Tab,
    Backspace,
    Delete,
    Escape,
    Up,
    Down,
    Left,
    Right,
    Home,
    End,
    PageUp,
    PageDown,
    Insert,
    F1,
    F2,
    F3,
    F4,
    F5,
    F6,
    F7,
    F8,
    F9,
    F10,
    F11,
    F12,
};

/// @brief Checks whether a key code represents a printable Unicode character.
/// @param key The key code to test.
/// @return True if the key code is a printable codepoint.
[[nodiscard]] constexpr auto isPrintable(KeyCode key) noexcept -> bool
{
    return static_cast<std::uint32_t>(key) < 0x10000 && static_cast<std::uint32_t>(key) >= 32;
}

/// @brief Converts a Unicode codepoint to a KeyCode.
/// @param codepoint The Unicode codepoint.
/// @return The corresponding KeyCode.
[[nodiscard]] constexpr auto keyCodeFromCodepoint(char32_t codepoint) noexcept -> KeyCode
{
    return static_cast<KeyCode>(codepoint);
}

/// @brief Extracts the Unicode codepoint from a printable KeyCode.
/// @param key The key code.
/// @return The Unicode codepoint, or 0 if not printable.
[[nodiscard]] constexpr auto codepointFromKeyCode(KeyCode key) noexcept -> char32_t
{
    return isPrintable(key) ? static_cast<char32_t>(key) : 0;
}

} // namespace tui
