// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <string>
#include <type_traits>

namespace tui
{

/// @brief Appends a UTF-16 code unit as UTF-8 bytes to a string.
///
/// Handles BMP characters (U+0000..U+FFFF). Surrogate pairs are not handled
/// here since Windows console input events deliver one code unit at a time.
///
/// @param output The string to append UTF-8 bytes to.
/// @param codeUnit The UTF-16 code unit to convert.
inline void appendUtf16AsUtf8(std::string& output, wchar_t codeUnit)
{
    auto const cp = static_cast<uint32_t>(static_cast<std::make_unsigned_t<wchar_t>>(codeUnit));
    if (cp < 0x80)
    {
        output += static_cast<char>(cp);
    }
    else if (cp < 0x800)
    {
        output += static_cast<char>(0xC0 | (cp >> 6));
        output += static_cast<char>(0x80 | (cp & 0x3F));
    }
    else
    {
        output += static_cast<char>(0xE0 | (cp >> 12));
        output += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        output += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

} // namespace tui
