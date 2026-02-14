// SPDX-License-Identifier: Apache-2.0
#include "SyntaxHighlighter.hpp"

#include <endo-language/ContextAwareTokenizer.hpp>

#include "SourceOffsetUtils.hpp"

namespace endo
{

HighlightMap computeHighlightMap(std::string_view source)
{
    auto map = HighlightMap(source.size(), TokenCategory::Default);
    if (source.empty())
        return map;

    auto const lineStartOffsets = buildLineStartOffsets(source);
    auto const tokens = tokenizeWithContext(source);

    for (auto const& classified: tokens)
    {
        auto const category = classified.category;
        if (category == TokenCategory::Default)
            continue;

        auto const range = classified.location;
        auto const& literal = classified.literal;

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
        // Start with literal size, then take the maximum with the range-based calculation.
        // This handles tokens where literal excludes delimiters (e.g., single-quoted strings
        // where literal is "hello" but source span is "'hello'").
        auto byteLen = literal.size();
        if (range.begin.line == range.end.line)
        {
            auto const lineStart = lineStartOffsets[static_cast<std::size_t>(beginLine)];
            auto const byteEnd = columnToByteOffset(source, lineStart, range.end.column);
            if (byteEnd > byteStart && byteEnd - byteStart > byteLen)
                byteLen = byteEnd - byteStart;
        }
        // Fallback: token has no literal and zero range (e.g., last token before EOF)
        if (byteLen == 0 && byteStart < source.size())
            byteLen = 1;

        // Fill the map for this token's byte range
        for (std::size_t i = 0; i < byteLen && byteStart + i < map.size(); ++i)
            map[byteStart + i] = category;
    }

    return map;
}

} // namespace endo
