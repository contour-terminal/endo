// SPDX-License-Identifier: Apache-2.0
#include <tui/Box.hpp>
#include <tui/GenericSyntaxHighlighter.hpp>
#include <tui/MarkdownInline.hpp>
#include <tui/MarkdownRenderer.hpp>
#include <tui/MarkdownTable.hpp>
#include <tui/Theme.hpp>

#include <limits>
#include <string>
#include <string_view>
#include <utility>

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

    /// @brief Extracts the language tag from a code fence line (e.g. "cpp" from "```cpp").
    /// @param line The fence line.
    /// @return The language tag, or empty if none.
    auto extractFenceLanguage(std::string_view line) -> std::string_view
    {
        auto pos = std::size_t { 0 };
        // Skip leading whitespace (up to 3 spaces)
        while (pos < line.size() && pos < 3 && line[pos] == ' ')
            ++pos;
        // Skip fence characters
        if (pos < line.size() && (line[pos] == '`' || line[pos] == '~'))
        {
            auto const fenceChar = line[pos];
            while (pos < line.size() && line[pos] == fenceChar)
                ++pos;
        }
        // Skip whitespace between fence and language tag
        while (pos < line.size() && line[pos] == ' ')
            ++pos;
        // Extract language tag (everything until whitespace or end of line)
        auto const tagStart = pos;
        while (pos < line.size() && line[pos] != ' ' && line[pos] != '\t')
            ++pos;
        return line.substr(tagStart, pos - tagStart);
    }

    /// @brief Counts the heading level (number of leading '#' chars).
    /// @param line The line to check.
    /// @return Heading level (1-6), or 0 if not a heading.
    auto detectHeadingLevel(std::string_view line) -> int
    {
        auto level = 0;
        while (std::cmp_less(level, line.size()) && level < 6 && line[static_cast<std::size_t>(level)] == '#')
            ++level;
        if (level > 0 && std::cmp_less(level, line.size()) && line[static_cast<std::size_t>(level)] == ' ')
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

void MarkdownRenderer::setFullWidthMode(bool enabled) noexcept
{
    _fullWidthMode = enabled;
}

void MarkdownRenderer::setMaxWidth(int width) noexcept
{
    _maxWidth = width;
}

void MarkdownRenderer::setTableRenderStyle(TableRenderStyle style) noexcept
{
    _tableRenderStyle = style;
}

void MarkdownRenderer::setCellStyleCallback(CellStyleFn fn)
{
    _cellStyleFn = std::move(fn);
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
                _output.writeText(line, _theme.thinkBlock);
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
                _codeLanguage = LanguageId::None;
                _codeHighlightState = HighlightState::Normal;
            }
            else
            {
                if (_codeLanguage != LanguageId::None)
                {
                    auto [highlights, newState] = highlightLine(line, _codeLanguage, _codeHighlightState);
                    _codeHighlightState = newState;
                    renderHighlightedLine(_output, line, highlights, _theme.codeBlock, currentTheme());
                }
                else
                {
                    _output.writeText(line, _theme.codeBlock);
                }
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
                    _codeLanguage = detectLanguageFromFenceTag(extractFenceLanguage(line));
                    _codeHighlightState = HighlightState::Normal;
                }
                else
                {
                    renderLine(line);
                }
            }
        }

        pos = (lineEnd != std::string_view::npos) ? lineEnd + 1 : markdown.size();
    }

    // Flush any pending table at the end of batch rendering.
    if (_inTable)
        flushTable();
}

void MarkdownRenderer::beginStream()
{
    _streaming = true;
    _streamBuffer.clear();
    _inCodeBlock = false;
    _inThinkBlock = false;
    _codeFence.clear();
    _codeLanguage = LanguageId::None;
    _codeHighlightState = HighlightState::Normal;
    _inTable = false;
    _tableLines.clear();
    _tableSeparatorSeen = false;
}

void MarkdownRenderer::feedToken(std::string_view token)
{
    _streamBuffer.append(token);
    processStreamBuffer();
}

void MarkdownRenderer::endStream()
{
    // Flush any pending table before final cleanup.
    if (_inTable)
        flushTable();

    // Flush any remaining content
    if (!_streamBuffer.empty())
    {
        if (_inCodeBlock)
        {
            _output.writeText(_streamBuffer, _theme.codeBlock);
        }
        else if (_inThinkBlock)
        {
            _output.writeText(_streamBuffer, _theme.thinkBlock);
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

    auto headingEmphasis = Style {};
    headingEmphasis.bold = true;
    headingEmphasis.fg = 0xFFD700_rgb; // Gold — stands out against heading base style

    auto codeBlock = Style {};
    codeBlock.bg = 0x323440_rgb;

    auto codeInline = Style {};
    codeInline.bg = 0x323440_rgb;

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

    auto tableBorderStyle = Style {};
    tableBorderStyle.dim = true;

    auto tableHeaderStyle = Style {};
    tableHeaderStyle.bold = true;

    auto tableCellStyle = Style {};

    return MarkdownTheme {
        .heading1 = heading1,
        .heading2 = heading2,
        .heading3 = heading3,
        .headingEmphasis = headingEmphasis,
        .codeBlock = codeBlock,
        .codeInline = codeInline,
        .bold = boldStyle,
        .italic = italicStyle,
        .link = linkStyle,
        .listMarker = listMarkerStyle,
        .blockquote = blockquoteStyle,
        .thinkBlock = thinkBlockStyle,
        .tableBorder = tableBorderStyle,
        .tableHeader = tableHeaderStyle,
        .tableCell = tableCellStyle,
    };
}

void MarkdownRenderer::renderLine(std::string_view line)
{
    // Table row detection (must come before other block-level checks)
    if (handleTableLine(line))
        return;

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
        _output.writeText("│ ", _theme.blockquote);
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
            _output.writeText(line.substr(0, markerEnd + 1), _theme.listMarker);
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

void MarkdownRenderer::renderInline(std::string_view text, Style const* baseStyle, Style const* boldStyle)
{
    auto effectiveBold = boldStyle ? *boldStyle : _theme.bold;
    auto effectiveItalic = _theme.italic;
    auto effectiveCode = _theme.codeInline;
    auto effectiveLink = _theme.link;

    // Inherit underline from base style so decorations aren't interrupted by inline emphasis.
    if (baseStyle && baseStyle->underline)
    {
        effectiveBold.underline = true;
        effectiveItalic.underline = true;
        effectiveCode.underline = true;
        effectiveLink.underline = true;
    }

    auto pos = std::size_t { 0 };
    while (pos < text.size())
    {
        // Inline code: `...` or ``...`` (CommonMark multi-backtick)
        if (text[pos] == '`')
        {
            if (auto const span = findInlineCodeEnd(text, pos))
            {
                _output.writeText(span->content, effectiveCode);
                pos = span->endPos;
                continue;
            }
        }

        // Bold: **...**
        if (pos + 1 < text.size() && text[pos] == '*' && text[pos + 1] == '*')
        {
            auto const endBold = text.find("**", pos + 2);
            if (endBold != std::string_view::npos)
            {
                _output.writeText(text.substr(pos + 2, endBold - pos - 2), effectiveBold);
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
                _output.writeText(text.substr(pos + 1, endItalic - pos - 1), effectiveItalic);
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
                _output.writeText(text.substr(pos + 2, endBold - pos - 2), effectiveBold);
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
                    _output.writeText(linkText, effectiveLink);
                    pos = endParen + 1;
                    continue;
                }
            }
        }

        // Regular character — find the next special character
        auto const nextSpecial = text.find_first_of("`*_[", pos + 1);
        auto const end = (nextSpecial != std::string_view::npos) ? nextSpecial : text.size();
        auto const span = text.substr(pos, end - pos);
        if (baseStyle)
            _output.writeText(span, *baseStyle);
        else
            _output.writeRaw(span);
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
                _output.writeText(line, _theme.thinkBlock);
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
                _codeLanguage = LanguageId::None;
                _codeHighlightState = HighlightState::Normal;
            }
            else
            {
                if (_codeLanguage != LanguageId::None)
                {
                    auto [highlights, newState] = highlightLine(line, _codeLanguage, _codeHighlightState);
                    _codeHighlightState = newState;
                    renderHighlightedLine(_output, line, highlights, _theme.codeBlock, currentTheme());
                }
                else
                {
                    _output.writeText(line, _theme.codeBlock);
                }
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
                    _codeLanguage = detectLanguageFromFenceTag(extractFenceLanguage(line));
                    _codeHighlightState = HighlightState::Normal;
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

auto MarkdownRenderer::handleTableLine(std::string_view line) -> bool
{
    if (_inTable)
    {
        if (detectTableRow(line))
        {
            _tableLines.emplace_back(line);
            if (detectTableSeparator(line))
                _tableSeparatorSeen = true;
            return true;
        }
        // Line is not a table row — flush the table, let caller render this line normally.
        flushTable();
        return false;
    }

    // Not currently in a table — check if this line starts one.
    if (detectTableRow(line))
    {
        _inTable = true;
        _tableLines.clear();
        _tableSeparatorSeen = false;
        _tableLines.emplace_back(line);
        if (detectTableSeparator(line))
            _tableSeparatorSeen = true;
        return true;
    }

    return false;
}

void MarkdownRenderer::flushTable()
{
    _inTable = false;

    if (!_tableSeparatorSeen)
    {
        // Not a valid table — render buffered lines as plain paragraphs.
        for (auto const& bufferedLine: _tableLines)
        {
            renderInline(bufferedLine);
            _output.writeRaw("\n");
        }
        _tableLines.clear();
        _tableSeparatorSeen = false;
        return;
    }

    // Parse into a ParsedTable.
    auto table = ParsedTable {};

    // Find the separator row index (should be index 1 for a valid GFM table).
    auto separatorIdx = std::size_t { 0 };
    for (std::size_t i = 0; i < _tableLines.size(); ++i)
    {
        if (detectTableSeparator(_tableLines[i]))
        {
            separatorIdx = i;
            break;
        }
    }

    // Header is the row before the separator.
    if (separatorIdx > 0)
        table.headers = splitTableRow(_tableLines[separatorIdx - 1]);

    table.alignments = parseTableAlignments(_tableLines[separatorIdx]);
    table.columnCount = table.headers.size();

    // Data rows come after the separator.
    for (auto i = separatorIdx + 1; i < _tableLines.size(); ++i)
    {
        auto cells = splitTableRow(_tableLines[i]);
        // Pad or truncate to match column count.
        cells.resize(table.columnCount);
        table.rows.push_back(std::move(cells));
    }

    // Ensure alignments match column count.
    table.alignments.resize(table.columnCount, TableAlignment::Left);

    renderTable(table);

    _tableLines.clear();
    _tableSeparatorSeen = false;
}

void MarkdownRenderer::renderTable(ParsedTable const& table)
{
    if (_tableRenderStyle == TableRenderStyle::Compact)
    {
        renderTableCompact(table, /*showHeader=*/true);
        return;
    }
    if (_tableRenderStyle == TableRenderStyle::Plain)
    {
        renderTableCompact(table, /*showHeader=*/false);
        return;
    }

    auto widths = computeColumnWidths(table);
    auto const border = BorderChars::fromStyle(BorderStyle::Rounded);

    // Constrain widths to terminal width if set.
    if (_maxWidth > 0)
        constrainColumnWidths(widths, _maxWidth);

    // Helper to render a horizontal border line.
    auto renderHLine = [&](std::string_view left, std::string_view mid, std::string_view right) {
        _output.writeText(left, _theme.tableBorder);
        for (std::size_t col = 0; col < table.columnCount; ++col)
        {
            // width + 2 for padding spaces around cell content
            for (auto i = 0; i < widths[col] + 2; ++i)
                _output.writeText(border.horizontal, _theme.tableBorder);
            if (col + 1 < table.columnCount)
                _output.writeText(mid, _theme.tableBorder);
        }
        _output.writeText(right, _theme.tableBorder);
        _output.writeRaw("\n");
    };

    // Helper to render a single physical line of cells.
    auto renderCellLine = [&](std::vector<std::string> const& cellTexts,
                              std::vector<int> const& colWidths,
                              Style const& cellStyle) {
        _output.writeText(border.vertical, _theme.tableBorder);
        for (std::size_t col = 0; col < table.columnCount; ++col)
        {
            auto const& rawText = (col < cellTexts.size()) ? cellTexts[col] : std::string {};
            auto const alignment =
                (col < table.alignments.size()) ? table.alignments[col] : TableAlignment::Left;
            auto const renderedWidth = inlineDisplayWidth(rawText);

            // Truncate as last-resort guard if text still overflows column width.
            auto const text =
                (renderedWidth > colWidths[col]) ? truncateToDisplayWidth(rawText, colWidths[col]) : rawText;
            auto const finalWidth =
                (renderedWidth > colWidths[col]) ? inlineDisplayWidth(text) : renderedWidth;
            auto const padding = std::max(0, colWidths[col] - finalWidth);

            auto leftPad = 0;
            auto rightPad = 0;
            switch (alignment)
            {
                case TableAlignment::Left: rightPad = padding; break;
                case TableAlignment::Right: leftPad = padding; break;
                case TableAlignment::Center:
                    leftPad = padding / 2;
                    rightPad = padding - leftPad;
                    break;
            }

            _output.writeRaw(" ");
            _output.writeRaw(std::string(static_cast<std::size_t>(leftPad), ' '));
            renderInline(text, &cellStyle);
            _output.writeRaw(std::string(static_cast<std::size_t>(rightPad), ' '));
            _output.writeRaw(" ");
            _output.writeText(border.vertical, _theme.tableBorder);
        }
        _output.writeRaw("\n");
    };

    // Helper to render a row with word wrapping when maxWidth is active.
    auto renderRow = [&](std::vector<std::string> const& cells, Style const& cellStyle) {
        if (_maxWidth <= 0)
        {
            // No wrapping — single-line row.
            renderCellLine(cells, widths, cellStyle);
            return;
        }

        // Wrap each cell and find the maximum number of physical lines.
        auto wrappedCells = std::vector<std::vector<std::string>> {};
        wrappedCells.reserve(table.columnCount);
        auto maxLines = std::size_t { 1 };

        for (std::size_t col = 0; col < table.columnCount; ++col)
        {
            auto const& text = (col < cells.size()) ? cells[col] : std::string {};
            auto wrapped = wrapText(text, widths[col]);
            maxLines = std::max(maxLines, wrapped.size());
            wrappedCells.push_back(std::move(wrapped));
        }

        // Render each physical line.
        for (std::size_t line = 0; line < maxLines; ++line)
        {
            auto lineTexts = std::vector<std::string>(table.columnCount);
            for (std::size_t col = 0; col < table.columnCount; ++col)
            {
                if (col < wrappedCells.size() && line < wrappedCells[col].size())
                    lineTexts[col] = wrappedCells[col][line];
            }
            renderCellLine(lineTexts, widths, cellStyle);
        }
    };

    // Top border
    renderHLine(border.topLeft, border.topT, border.topRight);

    // Header row
    renderRow(table.headers, _theme.tableHeader);

    // Separator between header and data
    renderHLine(border.leftT, border.cross, border.rightT);

    // Data rows
    for (auto const& row: table.rows)
        renderRow(row, _theme.tableCell);

    // Bottom border
    renderHLine(border.bottomLeft, border.bottomT, border.bottomRight);
}

void MarkdownRenderer::renderTableCompact(ParsedTable const& table, bool showHeader)
{
    auto widths = computeColumnWidths(table);

    if (_maxWidth > 0 && table.columnCount >= 2)
    {
        // Compact overhead: 2 (indent) + 2*(N-1) (gaps).
        auto const overhead = 2 + (2 * (static_cast<int>(table.columnCount) - 1));
        auto fixedTotal = 0;
        for (std::size_t col = 0; col + 1 < table.columnCount; ++col)
            fixedTotal += widths[col];

        // Shrink only the last column to fit the terminal width; minimum 3 chars.
        auto const lastCol = static_cast<int>(table.columnCount) - 1;
        auto const available = _maxWidth - overhead - fixedTotal;
        if (available >= 3 && available < widths[static_cast<std::size_t>(lastCol)])
            widths[static_cast<std::size_t>(lastCol)] = available;
    }

    constexpr auto indent = "  ";
    constexpr auto gap = "  ";

    // Helper: render a single physical line of a (possibly wrapped) row.
    auto renderPhysicalLine = [&](std::vector<std::string> const& cellTexts,
                                  Style const& cellStyle,
                                  std::size_t rowIdx) {
        _output.writeRaw(indent);
        for (std::size_t col = 0; col < table.columnCount; ++col)
        {
            auto const& rawText = (col < cellTexts.size()) ? cellTexts[col] : std::string {};
            auto const alignment =
                (col < table.alignments.size()) ? table.alignments[col] : TableAlignment::Left;
            auto const renderedWidth = inlineDisplayWidth(rawText);

            // Truncate as last-resort guard.
            auto const text =
                (renderedWidth > widths[col]) ? truncateToDisplayWidth(rawText, widths[col]) : rawText;
            auto const finalWidth = (renderedWidth > widths[col]) ? inlineDisplayWidth(text) : renderedWidth;
            auto const padding = std::max(0, widths[col] - finalWidth);

            auto leftPad = 0;
            auto rightPad = 0;
            switch (alignment)
            {
                case TableAlignment::Left: rightPad = padding; break;
                case TableAlignment::Right: leftPad = padding; break;
                case TableAlignment::Center:
                    leftPad = padding / 2;
                    rightPad = padding - leftPad;
                    break;
            }

            _output.writeRaw(std::string(static_cast<std::size_t>(leftPad), ' '));

            // Determine cell style: callback overrides default.
            auto const customStyle = _cellStyleFn ? _cellStyleFn(rowIdx, col, rawText) : std::nullopt;
            auto const& effectiveStyle = customStyle ? *customStyle : cellStyle;

            renderInline(text, &effectiveStyle);
            _output.writeRaw(std::string(static_cast<std::size_t>(rightPad), ' '));
            if (col + 1 < table.columnCount)
                _output.writeRaw(gap);
        }
        _output.writeRaw("\n");
    };

    // Helper: wrap cells and render potentially multi-line row.
    auto renderRow = [&](std::vector<std::string> const& cells, Style const& cellStyle, std::size_t rowIdx) {
        if (_maxWidth <= 0)
        {
            renderPhysicalLine(cells, cellStyle, rowIdx);
            return;
        }

        // Wrap each cell and find the maximum number of physical lines.
        auto wrappedCells = std::vector<std::vector<std::string>> {};
        wrappedCells.reserve(table.columnCount);
        auto maxLines = std::size_t { 1 };

        for (std::size_t col = 0; col < table.columnCount; ++col)
        {
            auto const& text = (col < cells.size()) ? cells[col] : std::string {};
            auto wrapped = wrapText(text, widths[col]);
            maxLines = std::max(maxLines, wrapped.size());
            wrappedCells.push_back(std::move(wrapped));
        }

        for (std::size_t line = 0; line < maxLines; ++line)
        {
            auto lineTexts = std::vector<std::string>(table.columnCount);
            for (std::size_t col = 0; col < table.columnCount; ++col)
            {
                if (col < wrappedCells.size() && line < wrappedCells[col].size())
                    lineTexts[col] = wrappedCells[col][line];
            }
            renderPhysicalLine(lineTexts, cellStyle, rowIdx);
        }
    };

    // Header row.
    if (!table.headers.empty())
    {
        auto const headerStyle = showHeader ? _theme.tableHeader : _theme.tableCell;
        renderRow(table.headers, headerStyle, std::numeric_limits<std::size_t>::max());
    }

    // Underline separator.
    if (showHeader)
    {
        _output.writeRaw(indent);
        for (std::size_t col = 0; col < table.columnCount; ++col)
        {
            auto const underline = std::string(static_cast<std::size_t>(widths[col]), '-');
            _output.writeText(underline, _theme.tableBorder);
            if (col + 1 < table.columnCount)
                _output.writeRaw(gap);
        }
        _output.writeRaw("\n");
    }

    // Data rows.
    for (std::size_t rowIdx = 0; rowIdx < table.rows.size(); ++rowIdx)
        renderRow(table.rows[rowIdx], _theme.tableCell, rowIdx);
}

void MarkdownRenderer::renderHeading(int level, std::string_view text)
{
    auto const& headingStyle = [&]() -> Style const& {
        switch (level)
        {
            case 1: return _theme.heading1;
            case 2: return _theme.heading2;
            default: return _theme.heading3;
        }
    }();

    if (_fullWidthMode && level == 1)
    {
        // Double-height + double-width: top half, then bottom half (same text on both lines)
        _output.setDoubleHeightTop();
        renderInline(text, &headingStyle, &_theme.headingEmphasis);
        _output.writeRaw("\n");
        _output.setDoubleHeightBottom();
        renderInline(text, &headingStyle, &_theme.headingEmphasis);
        _output.writeRaw("\n");
        _output.setSingleWidth();
    }
    else if (_fullWidthMode && level == 2)
    {
        // Double-width only: single line
        _output.setDoubleWidth();
        renderInline(text, &headingStyle, &_theme.headingEmphasis);
        _output.writeRaw("\n");
        _output.setSingleWidth();
    }
    else
    {
        renderInline(text, &headingStyle, &_theme.headingEmphasis);
        _output.writeRaw("\n");
    }
}

} // namespace tui
