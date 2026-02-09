// SPDX-License-Identifier: Apache-2.0
#include "SyntaxHighlighter.hpp"

#include "SourceOffsetUtils.hpp"
#include <endo-language/Lexer.hpp>

namespace endo
{

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
