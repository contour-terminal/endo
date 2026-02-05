// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

namespace tui
{

/// @brief Bitmask enumeration for keyboard modifier keys.
enum class Modifier : std::uint8_t
{
    None = 0,
    Shift = 1 << 0,
    Alt = 1 << 1,
    Ctrl = 1 << 2,
    Super = 1 << 3,
};

/// @brief Bitwise OR for combining modifiers.
[[nodiscard]] constexpr auto operator|(Modifier lhs, Modifier rhs) noexcept -> Modifier
{
    return static_cast<Modifier>(static_cast<std::uint8_t>(lhs) | static_cast<std::uint8_t>(rhs));
}

/// @brief Bitwise AND for testing modifiers.
[[nodiscard]] constexpr auto operator&(Modifier lhs, Modifier rhs) noexcept -> Modifier
{
    return static_cast<Modifier>(static_cast<std::uint8_t>(lhs) & static_cast<std::uint8_t>(rhs));
}

/// @brief Bitwise OR assignment for combining modifiers.
constexpr auto operator|=(Modifier& lhs, Modifier rhs) noexcept -> Modifier&
{
    lhs = lhs | rhs;
    return lhs;
}

/// @brief Tests whether a modifier flag is set.
/// @param mods The modifier bitmask to test.
/// @param flag The flag to check for.
/// @return True if the flag is set.
[[nodiscard]] constexpr auto hasModifier(Modifier mods, Modifier flag) noexcept -> bool
{
    return (mods & flag) != Modifier::None;
}

} // namespace tui
