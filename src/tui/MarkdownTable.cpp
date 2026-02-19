// SPDX-License-Identifier: Apache-2.0
#include <tui/MarkdownTable.hpp>
#include <tui/Unicode.hpp>

#if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wold-style-cast"
#endif
#include <libunicode/utf8_grapheme_segmenter.h>
#if defined(__clang__)
    #pragma clang diagnostic pop
#endif

#include <algorithm>

namespace tui
{

namespace
{
    /// @brief Trims leading and trailing whitespace from a string_view.
    auto trim(std::string_view sv) -> std::string_view
    {
        auto const start = sv.find_first_not_of(" \t");
        if (start == std::string_view::npos)
            return {};
        auto const end = sv.find_last_not_of(" \t");
        return sv.substr(start, end - start + 1);
    }

    /// @brief Calculates display width of a string using grapheme cluster segmentation.
    auto displayWidth(std::string_view text) -> int
    {
        auto width = 0;
        auto segmenter = unicode::utf8_grapheme_segmenter(text);
        for (auto const& cluster: segmenter)
            width += graphemeClusterWidth(cluster);
        return width;
    }
} // namespace

auto detectTableRow(std::string_view line) -> bool
{
    auto const trimmed = trim(line);
    return !trimmed.empty() && trimmed.front() == '|';
}

auto detectTableSeparator(std::string_view line) -> bool
{
    auto const trimmed = trim(line);
    if (trimmed.empty() || trimmed.front() != '|')
        return false;

    // Must contain at least one cell with dashes
    auto pos = std::size_t { 1 }; // skip leading |
    auto foundCell = false;

    while (pos < trimmed.size())
    {
        // Find next |
        auto const pipePos = trimmed.find('|', pos);
        auto const cellEnd = (pipePos != std::string_view::npos) ? pipePos : trimmed.size();
        auto const cell = trim(trimmed.substr(pos, cellEnd - pos));

        if (!cell.empty())
        {
            // Cell must match pattern: :?-+:?
            auto cellPos = std::size_t { 0 };
            if (cellPos < cell.size() && cell[cellPos] == ':')
                ++cellPos;

            auto const dashStart = cellPos;
            while (cellPos < cell.size() && cell[cellPos] == '-')
                ++cellPos;

            if (cellPos == dashStart)
                return false; // no dashes found

            if (cellPos < cell.size() && cell[cellPos] == ':')
                ++cellPos;

            if (cellPos != cell.size())
                return false; // unexpected characters

            foundCell = true;
        }

        if (pipePos == std::string_view::npos)
            break;
        pos = pipePos + 1;
    }

    return foundCell;
}

auto splitTableRow(std::string_view line) -> std::vector<std::string>
{
    auto const trimmed = trim(line);
    auto result = std::vector<std::string> {};

    if (trimmed.empty() || trimmed.front() != '|')
        return result;

    // Strip leading and optional trailing pipe
    auto content = trimmed.substr(1);
    if (!content.empty() && content.back() == '|')
        content.remove_suffix(1);

    auto pos = std::size_t { 0 };
    while (pos <= content.size())
    {
        auto const pipePos = content.find('|', pos);
        auto const cellEnd = (pipePos != std::string_view::npos) ? pipePos : content.size();
        auto const cell = trim(content.substr(pos, cellEnd - pos));
        result.emplace_back(cell);
        if (pipePos == std::string_view::npos)
            break;
        pos = pipePos + 1;
    }

    return result;
}

auto parseTableAlignments(std::string_view line) -> std::vector<TableAlignment>
{
    auto const cells = splitTableRow(line);
    auto result = std::vector<TableAlignment> {};
    result.reserve(cells.size());

    for (auto const& cell: cells)
    {
        auto const trimmed = trim(cell);
        if (trimmed.empty())
        {
            result.push_back(TableAlignment::Left);
            continue;
        }

        auto const leftColon = trimmed.front() == ':';
        auto const rightColon = trimmed.back() == ':';

        if (leftColon && rightColon)
            result.push_back(TableAlignment::Center);
        else if (rightColon)
            result.push_back(TableAlignment::Right);
        else
            result.push_back(TableAlignment::Left);
    }

    return result;
}

auto computeColumnWidths(ParsedTable const& table) -> std::vector<int>
{
    auto widths = std::vector<int>(table.columnCount, 3); // minimum width 3

    for (std::size_t col = 0; col < table.columnCount; ++col)
    {
        if (col < table.headers.size())
            widths[col] = std::max(widths[col], displayWidth(table.headers[col]));

        for (auto const& row: table.rows)
        {
            if (col < row.size())
                widths[col] = std::max(widths[col], displayWidth(row[col]));
        }
    }

    return widths;
}

auto alignCell(std::string_view text, int width, TableAlignment alignment) -> std::string
{
    auto const textWidth = displayWidth(text);
    auto const padding = std::max(0, width - textWidth);

    switch (alignment)
    {
        case TableAlignment::Right: {
            auto result = std::string(static_cast<std::size_t>(padding), ' ');
            result.append(text);
            return result;
        }
        case TableAlignment::Center: {
            auto const leftPad = padding / 2;
            auto const rightPad = padding - leftPad;
            auto result = std::string(static_cast<std::size_t>(leftPad), ' ');
            result.append(text);
            result.append(static_cast<std::size_t>(rightPad), ' ');
            return result;
        }
        case TableAlignment::Left:
        default: {
            auto result = std::string(text);
            result.append(static_cast<std::size_t>(padding), ' ');
            return result;
        }
    }
}

} // namespace tui
