// SPDX-License-Identifier: Apache-2.0
#include <string>
#include <string_view>

#include <tui/MarkdownRenderer.hpp>

namespace tui
{

namespace
{
    /// @brief Checks if a line is a code fence (``` or ~~~, optionally with language tag).
    /// @param line The line to check.
    /// @return The fence string if it's a code fence, empty string otherwise.
    auto detectCodeFence(std::string_view line) -> std::string_view
    {
        auto pos = std::size_t { 0 };
        // Skip leading whitespace (up to 3 spaces)
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
    /// @param line The line to check.
    /// @return Heading level (1-6), or 0 if not a heading.
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
    /// @param line The line to check.
    /// @return The number of characters consumed by the marker (0 if not a list item).
    auto detectListMarker(std::string_view line) -> std::size_t
    {
        auto pos = std::size_t { 0 };
        // Skip leading whitespace
        while (pos < line.size() && line[pos] == ' ')
            ++pos;

        if (pos >= line.size())
            return 0;

        // Unordered: - or * or +
        if ((line[pos] == '-' || line[pos] == '*' || line[pos] == '+') && pos + 1 < line.size()
            && line[pos + 1] == ' ')
            return pos + 2;

        // Ordered: digits followed by . or )
        auto const digitStart = pos;
        while (pos < line.size() && line[pos] >= '0' && line[pos] <= '9')
            ++pos;
        if (pos > digitStart && pos < line.size() && (line[pos] == '.' || line[pos] == ')')
            && pos + 1 < line.size() && line[pos + 1] == ' ')
            return pos + 2;

        return 0;
    }

    /// @brief Checks if a line starts with a blockquote marker (>).
    /// @param line The line to check.
    /// @return The number of characters consumed (0 if not a blockquote).
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
} // namespace

MarkdownRenderer::MarkdownRenderer(TerminalOutput& output, MarkdownTheme theme):
    _output(output), _theme(theme)
{
}

void MarkdownRenderer::render(std::string_view markdown)
{
    auto pos = std::size_t { 0 };
    while (pos < markdown.size())
    {
        auto const lineEnd = markdown.find('\n', pos);
        auto const line =
            (lineEnd != std::string_view::npos) ? markdown.substr(pos, lineEnd - pos) : markdown.substr(pos);

        if (_inThinkBlock)
        {
            if (line.find("</think>") != std::string_view::npos)
            {
                _inThinkBlock = false;
            }
            else
            {
                _output.write(line, _theme.thinkBlock);
                _output.writeRaw("\n");
            }
        }
        else if (_inCodeBlock)
        {
            auto const fence = detectCodeFence(line);
            if (!fence.empty() && fence.size() >= _codeFence.size())
            {
                _inCodeBlock = false;
                _codeFence.clear();
            }
            else
            {
                _output.write(line, _theme.codeBlock);
                _output.writeRaw("\n");
            }
        }
        else
        {
            // Check for think block start
            if (line.find("<think>") != std::string_view::npos)
            {
                _inThinkBlock = true;
            }
            else
            {
                auto const fence = detectCodeFence(line);
                if (!fence.empty())
                {
                    _inCodeBlock = true;
                    _codeFence = std::string(fence);
                }
                else
                {
                    renderLine(line);
                }
            }
        }

        pos = (lineEnd != std::string_view::npos) ? lineEnd + 1 : markdown.size();
    }
}

void MarkdownRenderer::beginStream()
{
    _streaming = true;
    _streamBuffer.clear();
    _inCodeBlock = false;
    _inThinkBlock = false;
    _codeFence.clear();
}

void MarkdownRenderer::feedToken(std::string_view token)
{
    _streamBuffer.append(token);
    processStreamBuffer();
}

void MarkdownRenderer::endStream()
{
    // Flush any remaining content
    if (!_streamBuffer.empty())
    {
        if (_inCodeBlock)
        {
            _output.write(_streamBuffer, _theme.codeBlock);
        }
        else if (_inThinkBlock)
        {
            _output.write(_streamBuffer, _theme.thinkBlock);
        }
        else
        {
            renderInline(_streamBuffer);
        }
        _streamBuffer.clear();
    }
    _output.writeRaw("\n");
    _output.flush();
    _streaming = false;
    _inCodeBlock = false;
    _inThinkBlock = false;
    _codeFence.clear();
}

auto MarkdownRenderer::defaultTheme() -> MarkdownTheme
{
    auto heading1 = Style {};
    heading1.bold = true;

    auto heading2 = Style {};
    heading2.bold = true;
    heading2.underline = true;

    auto heading3 = Style {};
    heading3.bold = true;

    auto codeBlock = Style {};
    codeBlock.dim = true;

    auto codeInline = Style {};
    codeInline.dim = true;

    auto boldStyle = Style {};
    boldStyle.bold = true;

    auto italicStyle = Style {};
    italicStyle.italic = true;

    auto linkStyle = Style {};
    linkStyle.fg = 0x00B4D8_rgb;
    linkStyle.underline = true;

    auto listMarkerStyle = Style {};
    listMarkerStyle.fg = 0x00B400_rgb;

    auto blockquoteStyle = Style {};
    blockquoteStyle.italic = true;
    blockquoteStyle.dim = true;

    auto thinkBlockStyle = Style {};
    thinkBlockStyle.dim = true;

    return MarkdownTheme {
        .heading1 = heading1,
        .heading2 = heading2,
        .heading3 = heading3,
        .codeBlock = codeBlock,
        .codeInline = codeInline,
        .bold = boldStyle,
        .italic = italicStyle,
        .link = linkStyle,
        .listMarker = listMarkerStyle,
        .blockquote = blockquoteStyle,
        .thinkBlock = thinkBlockStyle,
    };
}

void MarkdownRenderer::renderLine(std::string_view line)
{
    // Empty line
    if (line.empty())
    {
        _output.writeRaw("\n");
        return;
    }

    // Heading
    auto const headingLevel = detectHeadingLevel(line);
    if (headingLevel > 0)
    {
        auto const text = line.substr(static_cast<std::size_t>(headingLevel) + 1);
        renderHeading(headingLevel, text);
        return;
    }

    // Blockquote
    auto const bqLen = detectBlockquote(line);
    if (bqLen > 0)
    {
        _output.write("│ ", _theme.blockquote);
        renderInline(line.substr(bqLen));
        _output.writeRaw("\n");
        return;
    }

    // List item
    auto const listLen = detectListMarker(line);
    if (listLen > 0)
    {
        auto const markerEnd = line.find(' ', 0);
        if (markerEnd != std::string_view::npos && markerEnd < listLen)
        {
            _output.write(line.substr(0, markerEnd + 1), _theme.listMarker);
            renderInline(line.substr(listLen));
        }
        else
        {
            renderInline(line);
        }
        _output.writeRaw("\n");
        return;
    }

    // Regular paragraph line
    renderInline(line);
    _output.writeRaw("\n");
}

void MarkdownRenderer::renderInline(std::string_view text)
{
    auto pos = std::size_t { 0 };
    while (pos < text.size())
    {
        // Inline code: `...`
        if (text[pos] == '`')
        {
            auto const endTick = text.find('`', pos + 1);
            if (endTick != std::string_view::npos)
            {
                _output.write(text.substr(pos + 1, endTick - pos - 1), _theme.codeInline);
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
                _output.write(text.substr(pos + 2, endBold - pos - 2), _theme.bold);
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
                _output.write(text.substr(pos + 1, endItalic - pos - 1), _theme.italic);
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
                _output.write(text.substr(pos + 2, endBold - pos - 2), _theme.bold);
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
                    _output.write(linkText, _theme.link);
                    pos = endParen + 1;
                    continue;
                }
            }
        }

        // Regular character — find the next special character
        auto const nextSpecial = text.find_first_of("`*_[", pos + 1);
        auto const end = (nextSpecial != std::string_view::npos) ? nextSpecial : text.size();
        _output.writeRaw(text.substr(pos, end - pos));
        pos = end;
    }
}

void MarkdownRenderer::processStreamBuffer()
{
    // Process complete lines from the stream buffer
    while (true)
    {
        auto const newlinePos = _streamBuffer.find('\n');
        if (newlinePos == std::string::npos)
            break;

        auto const line = std::string_view(_streamBuffer).substr(0, newlinePos);

        if (_inThinkBlock)
        {
            if (line.find("</think>") != std::string_view::npos)
            {
                _inThinkBlock = false;
            }
            else
            {
                _output.write(line, _theme.thinkBlock);
                _output.writeRaw("\n");
            }
        }
        else if (_inCodeBlock)
        {
            auto const fence = detectCodeFence(line);
            if (!fence.empty() && fence.size() >= _codeFence.size())
            {
                _inCodeBlock = false;
                _codeFence.clear();
            }
            else
            {
                _output.write(line, _theme.codeBlock);
                _output.writeRaw("\n");
            }
        }
        else
        {
            if (line.find("<think>") != std::string_view::npos)
            {
                _inThinkBlock = true;
            }
            else
            {
                auto const fence = detectCodeFence(line);
                if (!fence.empty())
                {
                    _inCodeBlock = true;
                    _codeFence = std::string(fence);
                }
                else
                {
                    renderLine(line);
                }
            }
        }

        _streamBuffer.erase(0, newlinePos + 1);
        _output.flush();
    }
}

void MarkdownRenderer::renderHeading(int level, std::string_view text)
{
    switch (level)
    {
        case 1: _output.write(text, _theme.heading1); break;
        case 2: _output.write(text, _theme.heading2); break;
        default: _output.write(text, _theme.heading3); break;
    }
    _output.writeRaw("\n");
}

} // namespace tui
