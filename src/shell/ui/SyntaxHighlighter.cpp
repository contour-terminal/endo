// SPDX-License-Identifier: Apache-2.0
#include "SyntaxHighlighter.hpp"

#include <endo-language/lexer/ContextAwareTokenizer.hpp>

#if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wold-style-cast"
#endif
#include <libunicode/utf8_grapheme_segmenter.h>
#if defined(__clang__)
    #pragma clang diagnostic pop
#endif

#include "SourceOffsetUtils.hpp"

namespace endo
{

HighlightMap computeHighlightMap(std::string_view source)
{
    if (source.empty())
        return {};

    // Step 1: Build a per-byte map (tokenizer operates on byte ranges)
    auto byteMap = std::vector<TokenCategory>(source.size(), TokenCategory::Default);

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

        // Fill the byte map for this token's byte range
        for (std::size_t i = 0; i < byteLen && byteStart + i < byteMap.size(); ++i)
            byteMap[byteStart + i] = category;
    }

    // Step 2: Compress per-byte map to per-grapheme-cluster map.
    // Take the category at each cluster's first byte offset.
    auto result = HighlightMap {};
    result.reserve(source.size()); // Upper bound for ASCII (will be exact for ASCII)

    auto segmenter = unicode::utf8_grapheme_segmenter(source);
    for (auto it = segmenter.begin(); it != segmenter.end(); ++it)
    {
        auto const byteOffset = static_cast<std::size_t>(it._clusterStart - source.data());
        result.push_back(byteOffset < byteMap.size() ? byteMap[byteOffset] : TokenCategory::Default);
    }

    return result;
}

} // namespace endo
