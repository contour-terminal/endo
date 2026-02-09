// SPDX-License-Identifier: Apache-2.0
#include "SyntaxHighlighter.hpp"

#include <endo-language/Lexer.hpp>

namespace endo
{

namespace
{

    /// @brief Builds a table of byte offsets for line starts.
    /// @param source The full source text.
    /// @return A vector where index i is the byte offset of line i's first character.
    [[nodiscard]] std::vector<std::size_t> buildLineStartOffsets(std::string_view source)
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

    /// @brief Converts a codepoint-based column (1-based) to a byte offset within the source.
    /// @param source The full source text.
    /// @param lineStartByte The byte offset where this line starts.
    /// @param column The 1-based codepoint column from the Lexer.
    /// @return The byte offset corresponding to the given column.
    [[nodiscard]] std::size_t columnToByteOffset(std::string_view source,
                                                 std::size_t lineStartByte,
                                                 int column) noexcept
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

} // namespace

HighlightMap computeHighlightMap(std::string_view source)
{
    auto map = HighlightMap(source.size(), TokenCategory::Default);
    if (source.empty())
        return map;

    auto const lineStartOffsets = buildLineStartOffsets(source);

    auto lexer = Lexer { std::make_unique<StringSource>(std::string(source)) };
    lexer.enterFSharpExpr();

    while (lexer.currentToken() != Token::EndOfInput)
    {
        auto const token = lexer.currentToken();
        auto const category = classifyTokenCategory(token);

        if (category != TokenCategory::Default)
        {
            auto const range = lexer.currentRange();
            auto const& literal = lexer.currentLiteral();

            auto const beginLine = range.begin.line;
            auto const beginCol = range.begin.column;

            // Compute byte start offset
            std::size_t byteStart = 0;
            if (beginLine >= 0 && static_cast<std::size_t>(beginLine) < lineStartOffsets.size())
            {
                auto const lineStart = lineStartOffsets[static_cast<std::size_t>(beginLine)];
                byteStart = columnToByteOffset(source, lineStart, beginCol);
            }

            // Determine token byte length.
            // Use literal size if available, otherwise compute from source location range.
            // Fall back to 1 for tokens with empty literal and zero range (e.g., last token before EOF).
            auto byteLen = literal.size();
            if (byteLen == 0)
            {
                if (range.begin.line == range.end.line)
                {
                    auto const lineStart = lineStartOffsets[static_cast<std::size_t>(beginLine)];
                    auto const byteEnd = columnToByteOffset(source, lineStart, range.end.column);
                    if (byteEnd > byteStart)
                        byteLen = byteEnd - byteStart;
                }
                // Fallback: token has no literal and zero range (e.g., last token before EOF)
                if (byteLen == 0 && byteStart < source.size())
                    byteLen = 1;
            }

            // Fill the map for this token's byte range
            for (std::size_t i = 0; i < byteLen && byteStart + i < map.size(); ++i)
                map[byteStart + i] = category;
        }

        lexer.nextToken();
    }

    return map;
}

} // namespace endo
