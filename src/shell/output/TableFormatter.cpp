// SPDX-License-Identifier: Apache-2.0
#include "TableFormatter.hpp"

#include <endo-language/builtins/BuiltinImpls.hpp>

#include <tui/Box.hpp>

#include <CoreVM/types/TypeDescriptor.hpp>
#include <CoreVM/types/TypedObject.hpp>

#include <algorithm>
#include <bit>
#include <string>
#include <utility>
#include <vector>

#include "FileTypeStyle.hpp"

namespace endo
{

namespace
{
    /// Converts a single field value to a display string.
    std::string fieldValueToString(uint64_t slotVal, CoreVM::FieldInfo const& field, CoreVM::Runner* runner)
    {
        switch (field.type)
        {
            case CoreVM::LiteralType::Float: {
                auto const f = std::bit_cast<double>(slotVal);
                return std::format("{:.1f}", f);
            }
            case CoreVM::LiteralType::String: {
                auto const* str =
                    reinterpret_cast<CoreVM::CoreString const*>(static_cast<uintptr_t>(slotVal));
                return str ? std::string(*str) : "(null)";
            }
            case CoreVM::LiteralType::Boolean: return slotVal ? "true" : "false";
            case CoreVM::LiteralType::Object: {
                // Check for known nested object types (e.g., DateTime)
                if (runner && runner->isKnownObject(slotVal))
                {
                    auto* obj = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(slotVal));
                    if (obj->type->id == CoreVM::BuiltinTypeId::DateTime)
                    {
                        auto const year = static_cast<int>(obj->getSlot(0));
                        auto const month = static_cast<int>(obj->getSlot(1));
                        auto const day = static_cast<int>(obj->getSlot(2));
                        auto const hour = static_cast<int>(obj->getSlot(3));
                        auto const minute = static_cast<int>(obj->getSlot(4));
                        auto const second = static_cast<int>(obj->getSlot(5));
                        return std::format("{:04d}-{:02d}-{:02d} {:02d}:{:02d}:{:02d}",
                                           year,
                                           month,
                                           day,
                                           hour,
                                           minute,
                                           second);
                    }
                    if (obj->type->id == CoreVM::BuiltinTypeId::Size)
                    {
                        auto text = builtins::formatSizeToString(static_cast<int64_t>(obj->getSlot(0)));
                        // Pad bare "B" suffix to match 2-char unit suffixes (KB, MB, …) for right-alignment
                        if (text.ends_with(" B"))
                            text += ' ';
                        return text;
                    }
                    if (obj->type->id == CoreVM::BuiltinTypeId::FileMode)
                    {
                        return builtins::formatFileModeToString(static_cast<int64_t>(obj->getSlot(0)));
                    }
                }
                return std::to_string(static_cast<int64_t>(slotVal));
            }
            default: return std::to_string(static_cast<int64_t>(slotVal));
        }
    }

    /// Returns the display width (visual columns) of a UTF-8 string.
    int displayWidth(std::string const& str)
    {
        int width = 0;
        for (size_t i = 0; i < str.size();)
        {
            auto const byte = static_cast<unsigned char>(str[i]);
            if (byte < 0x80)
            {
                ++width;
                i += 1;
            } // ASCII
            else if (byte < 0xC0)
            {
                i += 1;
            } // continuation byte (skip)
            else if (byte < 0xE0)
            {
                ++width;
                i += 2;
            } // 2-byte sequence
            else if (byte < 0xF0)
            {
                ++width;
                i += 3;
            } // 3-byte sequence (includes …)
            else
            {
                width += 2;
                i += 4;
            } // 4-byte sequence (emoji, typically 2 cols)
        }
        return width;
    }

    /// Truncates a string to a maximum display width, adding ellipsis if needed.
    std::string truncate(std::string const& str, int maxWidth)
    {
        if (maxWidth <= 0 || displayWidth(str) <= maxWidth)
            return str;
        if (maxWidth <= 3)
            return str.substr(0, static_cast<size_t>(maxWidth));
        // Take maxWidth-1 characters (assuming ASCII prefix) and append ellipsis
        return str.substr(0, static_cast<size_t>(maxWidth - 1)) + "\u2026"; // …
    }

    /// Pads a string to a given display width with trailing spaces (left-aligned).
    std::string padRight(std::string const& str, int width)
    {
        auto const dw = displayWidth(str);
        if (dw >= width)
            return str;
        return str + std::string(static_cast<size_t>(width - dw), ' ');
    }

    /// Pads a string to a given display width with leading spaces (right-aligned).
    std::string padLeft(std::string const& str, int width)
    {
        auto const dw = displayWidth(str);
        if (dw >= width)
            return str;
        return std::string(static_cast<size_t>(width - dw), ' ') + str;
    }

    /// Returns true if a column should be right-aligned based on its field type
    /// and the actual runtime object type of the first row's value.
    bool isRightAlignedColumn(CoreVM::FieldInfo const& field,
                              CoreVM::TypedObject* firstRecord,
                              CoreVM::Runner* runner)
    {
        if (field.type == CoreVM::LiteralType::Number || field.type == CoreVM::LiteralType::Float)
            return true;
        if (field.type == CoreVM::LiteralType::Object && runner)
        {
            auto const slotVal = firstRecord->getSlot(field.offset);
            if (runner->isKnownObject(slotVal))
            {
                auto const* obj =
                    reinterpret_cast<CoreVM::TypedObject const*>(static_cast<uintptr_t>(slotVal));
                if (obj->type->id == CoreVM::BuiltinTypeId::Size)
                    return true;
            }
        }
        return false;
    }

    /// Pads a cell string according to the column's alignment.
    std::string padCell(std::string const& str, int width, bool rightAligned)
    {
        return rightAligned ? padLeft(str, width) : padRight(str, width);
    }
} // namespace

bool isListOfRecords(CoreVM::TypedObject* obj, CoreVM::Runner* runner)
{
    if (!obj || obj->type->id != CoreVM::BuiltinTypeId::List)
        return false;

    // Must be non-empty (Cons, tag == 1)
    if (obj->tag != 1)
        return false;

    // Walk the list and check each element
    auto* cur = obj;
    while (cur && cur->type->id == CoreVM::BuiltinTypeId::List && cur->tag == 1)
    {
        auto const elemRaw = cur->getSlot(0);
        if (!runner->isKnownObject(elemRaw))
            return false;

        auto* elem = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(elemRaw));
        if (elem->type->kind != CoreVM::TypeKind::Product)
            return false;

        // Must have at least one field
        if (elem->type->fields.empty())
            return false;

        cur = reinterpret_cast<CoreVM::TypedObject*>(cur->getSlot(1));
    }
    return true;
}

std::string formatRecordTable(CoreVM::TypedObject* listHead,
                              CoreVM::Runner* runner,
                              TableConfig const& config)
{
    // Collect all records from the list
    std::vector<CoreVM::TypedObject*> records;
    auto* cur = listHead;
    while (cur && cur->type->id == CoreVM::BuiltinTypeId::List && cur->tag == 1)
    {
        auto* elem = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(cur->getSlot(0)));
        records.push_back(elem);
        cur = reinterpret_cast<CoreVM::TypedObject*>(cur->getSlot(1));
    }

    if (records.empty())
        return "[]\n";

    // Get field metadata from the first record's type.
    // For Tuple2/Tuple3, the TypeDescriptor fields have empty names and default Number type.
    // Override by reading the packed type tags from the extra slot and assign ordinal column names.
    auto const recordTypeId = records[0]->type->id;
    auto const isTuple2 = (recordTypeId == CoreVM::BuiltinTypeId::Tuple2);
    auto const isTuple3 = (recordTypeId == CoreVM::BuiltinTypeId::Tuple3);

    std::vector<CoreVM::FieldInfo> resolvedFields;
    if (isTuple2 || isTuple3)
    {
        auto const numElems = isTuple2 ? size_t { 2 } : size_t { 3 };
        auto const tagSlot = isTuple2 ? uint8_t { 2 } : uint8_t { 3 };
        auto const packedTags = records[0]->getSlot(tagSlot);
        for (size_t i = 0; i < numElems; ++i)
        {
            auto const elemType = CoreVM::unpackTypeTag(packedTags, static_cast<uint8_t>(i));
            resolvedFields.push_back(
                { .name = std::to_string(i + 1), .offset = static_cast<uint8_t>(i), .type = elemType });
        }
    }

    bool const isFileInfo = (recordTypeId == CoreVM::BuiltinTypeId::FileInfo);

    // Hide fields the type marks non-display (FieldInfo::display == false). Such fields stay
    // accessible via field access/completion but are not rendered as columns — they typically
    // drive presentation instead (e.g. FileInfo's isDir/isSymlink/target feed the folder slash,
    // symlink icon, and "name -> target" suffix). Tuples have no hidden fields.
    if (resolvedFields.empty())
    {
        auto const& allFields = records[0]->type->fields;
        if (std::ranges::any_of(allFields, [](auto const& f) { return !f.display; }))
            for (auto const& f: allFields)
                if (f.display)
                    resolvedFields.push_back(f);
    }

    auto const& fields = resolvedFields.empty() ? records[0]->type->fields : resolvedFields;
    auto const numCols = fields.size();
    bool const decorateFiles = isFileInfo && config.useColor;

    // Determine per-column alignment and FileMode columns from the first record's runtime types
    std::vector<bool> rightAligned(numCols, false);
    std::vector<bool> isFileModeCol(numCols, false);
    for (size_t col = 0; col < numCols; ++col)
    {
        rightAligned[col] = isRightAlignedColumn(fields[col], records[0], runner);
        if (fields[col].type == CoreVM::LiteralType::Object && runner)
        {
            auto const slotVal = records[0]->getSlot(fields[col].offset);
            if (runner->isKnownObject(slotVal))
            {
                auto const* obj =
                    reinterpret_cast<CoreVM::TypedObject const*>(static_cast<uintptr_t>(slotVal));
                if (obj->type->id == CoreVM::BuiltinTypeId::FileMode)
                    isFileModeCol[col] = true;
            }
        }
    }

    // Build header names
    std::vector<std::string> headers;
    headers.reserve(numCols);
    for (auto const& field: fields)
        headers.push_back(field.name);

    // Build cell data and compute column widths.
    // When decorating FileInfo, per-row file decorations are stored for the name column.
    std::vector<std::vector<std::string>> rows;
    rows.reserve(records.size());
    std::vector<int> colWidths(numCols, 0);
    std::vector<FileDecoration> fileDecorations; // one per row (only when decorateFiles)

    for (size_t col = 0; col < numCols; ++col)
        colWidths[col] = static_cast<int>(headers[col].size());

    // When showing icons, reserve space for "icon " prefix in the name column header width
    constexpr int IconDisplayWidth = 2; // 1 glyph (1 cell) + 1 space
    if (decorateFiles && config.showIcons)
        colWidths[0] = std::max(colWidths[0], static_cast<int>(headers[0].size()) + IconDisplayWidth);

    for (auto* record: records)
    {
        std::vector<std::string> row;
        row.reserve(numCols);

        // FileInfo hidden slots that drive visuals: isDir (4), isSymlink (5), target (6).
        auto const isDir = isFileInfo && record->getSlot(4) != 0;
        auto const isSymlink = isFileInfo && record->getSlot(5) != 0;
        if (decorateFiles)
        {
            auto const nameSlot = record->getSlot(0);
            auto const* nameStr =
                reinterpret_cast<CoreVM::CoreString const*>(static_cast<uintptr_t>(nameSlot));
            int64_t mode = 0;
            auto const modeSlot = record->getSlot(2);
            if (runner && runner->isKnownObject(modeSlot))
            {
                auto const* modeObj =
                    reinterpret_cast<CoreVM::TypedObject const*>(static_cast<uintptr_t>(modeSlot));
                if (modeObj->type->id == CoreVM::BuiltinTypeId::FileMode)
                    mode = static_cast<int64_t>(modeObj->getSlot(0));
            }
            fileDecorations.push_back(
                getFileDecoration(nameStr ? std::string_view(*nameStr) : "", isDir, mode, isSymlink));
        }

        for (size_t col = 0; col < numCols; ++col)
        {
            auto slotVal = record->getSlot(fields[col].offset);
            auto cell = fieldValueToString(slotVal, fields[col], runner);
            if (config.showDirectorySlash && col == 0 && isDir)
                cell += '/';
            // Append the symlink target to the name cell ("name -> target"), GNU ls -l style.
            if (isSymlink && col == 0)
            {
                auto const targetSlot = record->getSlot(6);
                auto const* targetStr =
                    reinterpret_cast<CoreVM::CoreString const*>(static_cast<uintptr_t>(targetSlot));
                if (targetStr && !targetStr->empty())
                    cell += " -> " + *targetStr;
            }
            if (std::cmp_not_equal(col, config.autoGrowColumn))
                cell = truncate(cell, config.maxColumnWidth);
            auto cellDisplayWidth = displayWidth(cell);
            if (decorateFiles && config.showIcons && col == 0)
                cellDisplayWidth += IconDisplayWidth;
            colWidths[col] = std::max(colWidths[col], cellDisplayWidth);
            row.push_back(std::move(cell));
        }
        rows.push_back(std::move(row));
    }

    // Cap column widths at maxColumnWidth (auto-grow column is exempt)
    for (size_t col = 0; col < colWidths.size(); ++col)
        if (std::cmp_not_equal(col, config.autoGrowColumn))
            colWidths[col] = std::min(colWidths[col], config.maxColumnWidth);

    // Terminal-width-aware shrinking
    if (config.terminalWidth > 0)
    {
        // Compute total table width including overhead
        int overhead = 0;
        if (config.style == TableStyle::Bordered)
            overhead = static_cast<int>(numCols + 1) + (2 * static_cast<int>(numCols)); // borders + padding
        else if (config.style == TableStyle::Compact)
            overhead = 1 + (2 * static_cast<int>(numCols - 1)); // leading space + gaps
        else
            overhead = 2 * static_cast<int>(numCols - 1); // gaps only

        int totalColWidth = 0;
        for (auto const w: colWidths)
            totalColWidth += w;

        int const totalWidth = totalColWidth + overhead;
        if (totalWidth > config.terminalWidth && totalColWidth > 0)
        {
            int const available = std::max(static_cast<int>(numCols) * 4, config.terminalWidth - overhead);
            for (auto& w: colWidths)
            {
                auto const newW =
                    std::max(4, static_cast<int>(static_cast<int64_t>(w) * available / totalColWidth));
                w = newW;
            }
        }
    }

    // After terminal-width shrinking, truncate auto-grow column cells to fit the final width
    if (config.autoGrowColumn >= 0 && std::cmp_less(config.autoGrowColumn, numCols))
    {
        auto const col = static_cast<size_t>(config.autoGrowColumn);
        auto const iconOffset = (decorateFiles && config.showIcons && col == 0) ? IconDisplayWidth : 0;
        auto const maxWidth = colWidths[col] - iconOffset;
        for (auto& row: rows)
            row[col] = truncate(row[col], maxWidth);
    }

    std::string result;

    // Helper: render a data cell, applying file decoration (icon prefix + SGR color) for the name
    // column. The cell text is padded to the column width. SGR sequences wrap the padded content so
    // they don't affect alignment. The icon (when shown) is part of the visible content and included
    // in the padding calculation.
    auto renderDataCell = [&](std::string& out, size_t rowIdx, size_t col, int width, bool rightAlign) {
        auto const& cellText = rows[rowIdx][col];
        if (decorateFiles && col == 0)
        {
            auto const& deco = fileDecorations[rowIdx];
            auto const sgr = sgrSequence(deco.style);
            auto const sgrReset = sgr.empty() ? std::string {} : std::string { "\033[m" };

            if (config.showIcons)
            {
                // Pad name alone, then prepend the icon + space so that
                // icon (1 col) + space (1 col) + paddedName (width-2 cols) = width cols.
                auto paddedName = padCell(cellText, width - IconDisplayWidth, rightAlign);
                out += sgr;
                out += std::string(deco.icon);
                out += ' ';
                out += paddedName;
                out += sgrReset;
            }
            else
            {
                out += sgr;
                out += padCell(cellText, width, rightAlign);
                out += sgrReset;
            }
        }
        else if (config.useColor && isFileModeCol[col])
        {
            // Permission string is always 9 chars; colorize each character by type.
            // Padding must use the plain-text length, not the SGR-inflated length.
            auto const colored = colorizePermissions(cellText);
            out += colored;
            auto const padding = width - displayWidth(cellText);
            if (padding > 0)
                out += std::string(static_cast<size_t>(padding), ' ');
        }
        else
        {
            out += padCell(cellText, width, rightAlign);
        }
    };

    if (config.style == TableStyle::Bordered)
    {
        auto const bc = tui::BorderChars::fromStyle(tui::BorderStyle::Rounded);
        auto const* const dim = config.useColor ? "\033[2m" : "";
        auto const* const bold = config.useColor ? "\033[1m" : "";
        auto const* const reset = config.useColor ? "\033[0m" : "";

        // Helper: build a horizontal rule line
        auto horizontalRule = [&](std::string_view left, std::string_view mid, std::string_view right) {
            std::string line;
            line += dim;
            line += left;
            for (size_t col = 0; col < numCols; ++col)
            {
                if (col > 0)
                    line += mid;
                for (int i = 0; i < colWidths[col] + 2; ++i) // +2 for padding
                    line += bc.horizontal;
            }
            line += right;
            line += reset;
            line += '\n';
            return line;
        };

        // Top border
        result += horizontalRule(bc.topLeft, bc.topT, bc.topRight);

        // Header row
        result += dim;
        result += bc.vertical;
        result += reset;
        for (size_t col = 0; col < numCols; ++col)
        {
            result += ' ';
            result += bold;
            result += padCell(headers[col], colWidths[col], rightAligned[col]);
            result += reset;
            result += ' ';
            result += dim;
            result += bc.vertical;
            result += reset;
        }
        result += '\n';

        // Separator
        result += horizontalRule(bc.leftT, bc.cross, bc.rightT);

        // Data rows
        for (size_t rowIdx = 0; rowIdx < rows.size(); ++rowIdx)
        {
            result += dim;
            result += bc.vertical;
            result += reset;
            for (size_t col = 0; col < numCols; ++col)
            {
                result += ' ';
                renderDataCell(result, rowIdx, col, colWidths[col], rightAligned[col]);
                result += ' ';
                result += dim;
                result += bc.vertical;
                result += reset;
            }
            result += '\n';
        }

        // Bottom border
        result += horizontalRule(bc.bottomLeft, bc.bottomT, bc.bottomRight);
    }
    else if (config.style == TableStyle::Compact)
    {
        auto const* const bold = config.useColor ? "\033[1m" : "";
        auto const* const reset = config.useColor ? "\033[0m" : "";

        // Header row
        result += ' ';
        for (size_t col = 0; col < numCols; ++col)
        {
            if (col > 0)
                result += "  "; // 2-space gap
            result += bold;
            result += padCell(headers[col], colWidths[col], rightAligned[col]);
            result += reset;
        }
        result += '\n';

        // Underline
        result += ' ';
        for (size_t col = 0; col < numCols; ++col)
        {
            if (col > 0)
                result += "  ";
            for (int i = 0; i < colWidths[col]; ++i)
                result += "\u2500"; // ─
        }
        result += '\n';

        // Data rows
        for (size_t rowIdx = 0; rowIdx < rows.size(); ++rowIdx)
        {
            result += ' ';
            for (size_t col = 0; col < numCols; ++col)
            {
                if (col > 0)
                    result += "  ";
                renderDataCell(result, rowIdx, col, colWidths[col], rightAligned[col]);
            }
            result += '\n';
        }
    }
    else // Plain
    {
        // Header row (no icons/colors in Plain mode)
        for (size_t col = 0; col < numCols; ++col)
        {
            if (col > 0)
                result += "  ";
            result += padCell(headers[col], colWidths[col], rightAligned[col]);
        }
        result += '\n';

        // Data rows
        for (auto const& row: rows)
        {
            for (size_t col = 0; col < numCols; ++col)
            {
                if (col > 0)
                    result += "  ";
                result += padCell(row[col], colWidths[col], rightAligned[col]);
            }
            result += '\n';
        }
    }

    return result;
}

} // namespace endo
