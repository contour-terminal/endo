// SPDX-License-Identifier: Apache-2.0
#include "StyledText.hpp"

#include <tui/Canvas.hpp>
#include <tui/MarkdownRenderer.hpp>
#include <tui/Unicode.hpp>

#if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wold-style-cast"
#endif
#include <libunicode/utf8_grapheme_segmenter.h>
#if defined(__clang__)
    #pragma clang diagnostic pop
#endif

namespace tui
{

namespace
{
    /// @brief Checks if a line is a code fence (``` or ~~~, optionally with language tag).
    auto detectCodeFence(std::string_view line) -> std::string_view
    {
        auto pos = std::size_t { 0 };
        while (pos < line.size() && pos < 3 && line[pos] == ' ')
            ++pos;

        if (pos >= line.size())
            return {};

        auto const fenceChar = line[pos];
        if (fenceChar != '`' && fenceChar != '~')
            return {};

        auto const fenceStart = pos;
        while (pos < line.size() && line[pos] == fenceChar)
            ++pos;

        if (pos - fenceStart >= 3)
            return line.substr(fenceStart, pos - fenceStart);

        return {};
    }

    /// @brief Counts the heading level (number of leading '#' chars).
    auto detectHeadingLevel(std::string_view line) -> int
    {
        auto level = 0;
        while (level < static_cast<int>(line.size()) && level < 6
               && line[static_cast<std::size_t>(level)] == '#')
            ++level;
        if (level > 0 && level < static_cast<int>(line.size())
            && line[static_cast<std::size_t>(level)] == ' ')
            return level;
        return 0;
    }

    /// @brief Checks if a line starts with a list marker.
    auto detectListMarker(std::string_view line) -> std::size_t
    {
        auto pos = std::size_t { 0 };
        while (pos < line.size() && line[pos] == ' ')
            ++pos;

        if (pos >= line.size())
            return 0;

        if ((line[pos] == '-' || line[pos] == '*' || line[pos] == '+') && pos + 1 < line.size()
            && line[pos + 1] == ' ')
            return pos + 2;

        auto const digitStart = pos;
        while (pos < line.size() && line[pos] >= '0' && line[pos] <= '9')
            ++pos;
        if (pos > digitStart && pos < line.size() && (line[pos] == '.' || line[pos] == ')')
            && pos + 1 < line.size() && line[pos + 1] == ' ')
            return pos + 2;

        return 0;
    }

    /// @brief Checks if a line starts with a blockquote marker (>).
    auto detectBlockquote(std::string_view line) -> std::size_t
    {
        if (!line.empty() && line[0] == '>')
        {
            if (line.size() > 1 && line[1] == ' ')
                return 2;
            return 1;
        }
        return 0;
    }

    /// @brief Calculates display width of a string.
    int displayWidth(std::string_view text)
    {
        int width = 0;
        auto segmenter = unicode::utf8_grapheme_segmenter(text);
        for (auto const& cluster: segmenter)
            width += graphemeClusterWidth(cluster);
        return width;
    }

    /// @brief Parses inline markdown elements and returns styled spans.
    std::vector<StyledSpan> parseInlineMarkdown(std::string_view text, MarkdownTheme const& theme)
    {
        std::vector<StyledSpan> spans;
        auto pos = std::size_t { 0 };

        while (pos < text.size())
        {
            // Inline code: `...`
            if (text[pos] == '`')
            {
                auto const endTick = text.find('`', pos + 1);
                if (endTick != std::string_view::npos)
                {
                    spans.push_back(
                        { std::string(text.substr(pos + 1, endTick - pos - 1)), theme.codeInline });
                    pos = endTick + 1;
                    continue;
                }
            }

            // Bold: **...**
            if (pos + 1 < text.size() && text[pos] == '*' && text[pos + 1] == '*')
            {
                auto const endBold = text.find("**", pos + 2);
                if (endBold != std::string_view::npos)
                {
                    spans.push_back({ std::string(text.substr(pos + 2, endBold - pos - 2)), theme.bold });
                    pos = endBold + 2;
                    continue;
                }
            }

            // Italic: *...*
            if (text[pos] == '*')
            {
                auto const endItalic = text.find('*', pos + 1);
                if (endItalic != std::string_view::npos)
                {
                    spans.push_back({ std::string(text.substr(pos + 1, endItalic - pos - 1)), theme.italic });
                    pos = endItalic + 1;
                    continue;
                }
            }

            // Bold: __...__
            if (pos + 1 < text.size() && text[pos] == '_' && text[pos + 1] == '_')
            {
                auto const endBold = text.find("__", pos + 2);
                if (endBold != std::string_view::npos)
                {
                    spans.push_back({ std::string(text.substr(pos + 2, endBold - pos - 2)), theme.bold });
                    pos = endBold + 2;
                    continue;
                }
            }

            // Link: [text](url)
            if (text[pos] == '[')
            {
                auto const endBracket = text.find(']', pos + 1);
                if (endBracket != std::string_view::npos && endBracket + 1 < text.size()
                    && text[endBracket + 1] == '(')
                {
                    auto const endParen = text.find(')', endBracket + 2);
                    if (endParen != std::string_view::npos)
                    {
                        auto const linkText = text.substr(pos + 1, endBracket - pos - 1);
                        spans.push_back({ std::string(linkText), theme.link });
                        pos = endParen + 1;
                        continue;
                    }
                }
            }

            // Regular character - find the next special character
            auto const nextSpecial = text.find_first_of("`*_[", pos + 1);
            auto const end = (nextSpecial != std::string_view::npos) ? nextSpecial : text.size();
            spans.push_back({ std::string(text.substr(pos, end - pos)), {} });
            pos = end;
        }

        return spans;
    }

} // namespace

StyledText StyledText::fromPlainText(std::string_view text, int maxWidth, Style baseStyle)
{
    StyledText result;

    auto pos = std::size_t { 0 };
    while (pos < text.size())
    {
        auto const lineEnd = text.find('\n', pos);
        auto const line =
            (lineEnd != std::string_view::npos) ? text.substr(pos, lineEnd - pos) : text.substr(pos);

        StyledLine styledLine;
        styledLine.push_back({ std::string(line), baseStyle });

        if (maxWidth > 0)
        {
            auto wrapped = wrapLine(styledLine, maxWidth);
            for (auto& wrappedLine: wrapped)
                result.addLine(std::move(wrappedLine));
        }
        else
        {
            result.addLine(std::move(styledLine));
        }

        pos = (lineEnd != std::string_view::npos) ? lineEnd + 1 : text.size();
    }

    // Handle trailing newline (empty line)
    if (!text.empty() && text.back() == '\n')
        result.addLine({});

    return result;
}

StyledText StyledText::fromMarkdown(std::string_view markdown, int maxWidth, MarkdownTheme const* theme)
{
    static auto const defaultTheme = MarkdownRenderer::defaultTheme();
    MarkdownTheme const& mdTheme = theme ? *theme : defaultTheme;

    StyledText result;
    bool inCodeBlock = false;
    std::string codeFence;

    auto pos = std::size_t { 0 };
    while (pos < markdown.size())
    {
        auto const lineEnd = markdown.find('\n', pos);
        auto const line =
            (lineEnd != std::string_view::npos) ? markdown.substr(pos, lineEnd - pos) : markdown.substr(pos);

        StyledLine styledLine;

        if (inCodeBlock)
        {
            auto const fence = detectCodeFence(line);
            if (!fence.empty() && fence.size() >= codeFence.size())
            {
                inCodeBlock = false;
                codeFence.clear();
            }
            else
            {
                styledLine.push_back({ std::string(line), mdTheme.codeBlock });
            }
        }
        else
        {
            auto const fence = detectCodeFence(line);
            if (!fence.empty())
            {
                inCodeBlock = true;
                codeFence = std::string(fence);
            }
            else if (line.empty())
            {
                // Empty line
            }
            else
            {
                // Heading
                auto const headingLevel = detectHeadingLevel(line);
                if (headingLevel > 0)
                {
                    auto const text = line.substr(static_cast<std::size_t>(headingLevel) + 1);
                    Style headingStyle;
                    switch (headingLevel)
                    {
                        case 1: headingStyle = mdTheme.heading1; break;
                        case 2: headingStyle = mdTheme.heading2; break;
                        default: headingStyle = mdTheme.heading3; break;
                    }
                    styledLine.push_back({ std::string(text), headingStyle });
                }
                // Blockquote
                else if (auto const bqLen = detectBlockquote(line); bqLen > 0)
                {
                    styledLine.push_back(
                        { "\xe2\x94\x82 ", mdTheme.blockquote }); // U+2502 BOX DRAWINGS LIGHT VERTICAL
                    auto inlineSpans = parseInlineMarkdown(line.substr(bqLen), mdTheme);
                    for (auto& span: inlineSpans)
                    {
                        if (std::holds_alternative<std::monostate>(span.style.fg)
                            && std::holds_alternative<std::monostate>(span.style.bg))
                            span.style = mdTheme.blockquote;
                        styledLine.push_back(std::move(span));
                    }
                }
                // List item
                else if (auto const listLen = detectListMarker(line); listLen > 0)
                {
                    auto const markerEnd = line.find(' ', 0);
                    if (markerEnd != std::string_view::npos && markerEnd < listLen)
                    {
                        styledLine.push_back(
                            { std::string(line.substr(0, markerEnd + 1)), mdTheme.listMarker });
                        auto inlineSpans = parseInlineMarkdown(line.substr(listLen), mdTheme);
                        for (auto& span: inlineSpans)
                            styledLine.push_back(std::move(span));
                    }
                    else
                    {
                        auto inlineSpans = parseInlineMarkdown(line, mdTheme);
                        for (auto& span: inlineSpans)
                            styledLine.push_back(std::move(span));
                    }
                }
                // Regular paragraph line
                else
                {
                    auto inlineSpans = parseInlineMarkdown(line, mdTheme);
                    for (auto& span: inlineSpans)
                        styledLine.push_back(std::move(span));
                }
            }
        }

        if (maxWidth > 0 && !styledLine.empty())
        {
            auto wrapped = wrapLine(styledLine, maxWidth);
            for (auto& wrappedLine: wrapped)
                result.addLine(std::move(wrappedLine));
        }
        else
        {
            result.addLine(std::move(styledLine));
        }

        pos = (lineEnd != std::string_view::npos) ? lineEnd + 1 : markdown.size();
    }

    return result;
}

void StyledText::renderTo(Canvas& canvas, int startLine, int lineCount) const
{
    int const linesToRender = (lineCount < 0)
                                  ? static_cast<int>(_lines.size()) - startLine
                                  : std::min(lineCount, static_cast<int>(_lines.size()) - startLine);

    for (int i = 0; i < linesToRender && i < canvas.height(); ++i)
    {
        int const lineIdx = startLine + i;
        if (lineIdx < 0 || lineIdx >= static_cast<int>(_lines.size()))
            continue;

        auto const& line = _lines[static_cast<std::size_t>(lineIdx)];
        int col = 0;
        for (auto const& span: line)
        {
            col += canvas.putString(i, col, span.text, span.style);
        }
    }
}

void StyledText::clear()
{
    _lines.clear();
    _maxLineWidth = 0;
}

void StyledText::addLine(StyledLine line)
{
    int const width = lineWidth(line);
    if (width > _maxLineWidth)
        _maxLineWidth = width;
    _lines.push_back(std::move(line));
}

int StyledText::lineWidth(StyledLine const& line)
{
    int width = 0;
    for (auto const& span: line)
        width += displayWidth(span.text);
    return width;
}

std::vector<StyledLine> StyledText::wrapLine(StyledLine const& line, int maxWidth)
{
    if (maxWidth <= 0 || lineWidth(line) <= maxWidth)
        return { line };

    std::vector<StyledLine> result;
    StyledLine currentLine;
    int currentWidth = 0;

    for (auto const& span: line)
    {
        // Simple word-based wrapping within each span
        auto text = std::string_view(span.text);
        auto pos = std::size_t { 0 };

        while (pos < text.size())
        {
            // Find next word boundary
            auto wordEnd = text.find(' ', pos);
            if (wordEnd == std::string_view::npos)
                wordEnd = text.size();

            auto word = text.substr(pos, wordEnd - pos);
            auto wordWidth = displayWidth(word);

            // Include trailing space if present
            bool hasSpace = (wordEnd < text.size());
            if (hasSpace)
            {
                wordEnd++;
                wordWidth++;
            }

            // Check if word fits on current line
            if (currentWidth + wordWidth > maxWidth && currentWidth > 0)
            {
                // Start new line
                result.push_back(std::move(currentLine));
                currentLine = {};
                currentWidth = 0;
            }

            // Add word to current line
            if (hasSpace)
                currentLine.push_back({ std::string(text.substr(pos, wordEnd - pos)), span.style });
            else
                currentLine.push_back({ std::string(word), span.style });
            currentWidth += wordWidth;
            pos = wordEnd;
        }
    }

    if (!currentLine.empty())
        result.push_back(std::move(currentLine));

    return result;
}

} // namespace tui
