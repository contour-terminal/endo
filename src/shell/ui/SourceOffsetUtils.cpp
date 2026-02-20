// SPDX-License-Identifier: Apache-2.0
#include "SourceOffsetUtils.hpp"

namespace endo
{

std::vector<std::size_t> buildLineStartOffsets(std::string_view source)
{
    auto offsets = std::vector<std::size_t> {};
    offsets.push_back(0);
    for (std::size_t i = 0; i < source.size(); ++i)
    {
        if (source[i] == '\n')
            offsets.push_back(i + 1);
    }
    return offsets;
}

std::size_t columnToByteOffset(std::string_view source, std::size_t lineStartByte, int column) noexcept
{
    auto pos = lineStartByte;
    auto codepointsSkipped = 0;
    // Column is 1-based: column 1 = first character
    while (codepointsSkipped < column - 1 && pos < source.size() && source[pos] != '\n')
    {
        // Skip one UTF-8 codepoint
        auto const byte = static_cast<unsigned char>(source[pos]);
        if (byte < 0x80)
            pos += 1;
        else if ((byte & 0xE0) == 0xC0)
            pos += 2;
        else if ((byte & 0xF0) == 0xE0)
            pos += 3;
        else if ((byte & 0xF8) == 0xF0)
            pos += 4;
        else
            pos += 1; // Invalid UTF-8 byte, skip one
        ++codepointsSkipped;
    }
    return pos;
}

std::size_t positionToByteOffset(std::string_view source,
                                 std::vector<std::size_t> const& lineStarts,
                                 SourcePosition pos) noexcept
{
    if (pos.line < 0 || static_cast<std::size_t>(pos.line) >= lineStarts.size())
        return source.size();

    auto const lineStart = lineStarts[static_cast<std::size_t>(pos.line)];

    // Walk codepoints from line start to find the byte offset for the 0-based character index
    auto bytePos = lineStart;
    auto codepointsWalked = 0;
    while (codepointsWalked < pos.character && bytePos < source.size() && source[bytePos] != '\n')
    {
        // Skip one UTF-8 codepoint
        auto const byte = static_cast<unsigned char>(source[bytePos]);
        if (byte < 0x80)
            bytePos += 1;
        else if ((byte & 0xE0) == 0xC0)
            bytePos += 2;
        else if ((byte & 0xF0) == 0xE0)
            bytePos += 3;
        else if ((byte & 0xF8) == 0xF0)
            bytePos += 4;
        else
            bytePos += 1; // Invalid UTF-8 byte, skip one
        ++codepointsWalked;
    }
    return bytePos;
}

} // namespace endo
