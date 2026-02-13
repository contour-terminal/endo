// SPDX-License-Identifier: Apache-2.0
#include "TableFormatter.hpp"

#include <CoreVM/types/TypeDescriptor.hpp>
#include <CoreVM/types/TypedObject.hpp>

#include <algorithm>
#include <bit>
#include <string>
#include <vector>

#include <tui/Box.hpp>

namespace endo
{

namespace
{
    /// Converts a single field value to a display string.
    std::string fieldValueToString(uint64_t slotVal,
                                   CoreVM::FieldInfo const& field,
                                   CoreVM::Runner* runner,
                                   bool isProcessInfo)
    {
        // ProcessInfo "cpu" field stores a double as bit_cast<uint64_t>
        if (isProcessInfo && field.name == "cpu")
        {
            auto const cpuVal = std::bit_cast<double>(slotVal);
            return std::format("{:.1f}", cpuVal);
        }

        switch (field.type)
        {
            case CoreVM::LiteralType::String: {
                auto const* str =
                    reinterpret_cast<CoreVM::CoreString const*>(static_cast<uintptr_t>(slotVal));
                return str ? std::string(*str) : "(null)";
            }
            case CoreVM::LiteralType::Boolean: return slotVal ? "true" : "false";
            default: return std::to_string(static_cast<int64_t>(slotVal));
        }
    }

    /// Truncates a string to a maximum width, adding ellipsis if needed.
    std::string truncate(std::string const& str, int maxWidth)
    {
        if (maxWidth <= 0 || static_cast<int>(str.size()) <= maxWidth)
            return str;
        if (maxWidth <= 3)
            return str.substr(0, static_cast<size_t>(maxWidth));
        return str.substr(0, static_cast<size_t>(maxWidth - 1)) + "\u2026"; // …
    }

    /// Pads a string to a given width with trailing spaces.
    std::string padRight(std::string const& str, int width)
    {
        if (static_cast<int>(str.size()) >= width)
            return str;
        return str + std::string(static_cast<size_t>(width) - str.size(), ' ');
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

    // Get field metadata from the first record's type
    auto const& fields = records[0]->type->fields;
    auto const numCols = fields.size();
    bool const isProcessInfo = (records[0]->type->id == CoreVM::BuiltinTypeId::ProcessInfo);

    // Build header names
    std::vector<std::string> headers;
    headers.reserve(numCols);
    for (auto const& field: fields)
        headers.push_back(field.name);

    // Build cell data and compute column widths
    std::vector<std::vector<std::string>> rows;
    rows.reserve(records.size());
    std::vector<int> colWidths(numCols, 0);

    for (size_t col = 0; col < numCols; ++col)
        colWidths[col] = static_cast<int>(headers[col].size());

    for (auto* record: records)
    {
        std::vector<std::string> row;
        row.reserve(numCols);
        for (size_t col = 0; col < numCols; ++col)
        {
            auto slotVal = record->getSlot(static_cast<uint8_t>(col));
            auto cell = fieldValueToString(slotVal, fields[col], runner, isProcessInfo);
            cell = truncate(cell, config.maxColumnWidth);
            colWidths[col] = std::max(colWidths[col], static_cast<int>(cell.size()));
            row.push_back(std::move(cell));
        }
        rows.push_back(std::move(row));
    }

    // Cap column widths at maxColumnWidth
    for (auto& w: colWidths)
        w = std::min(w, config.maxColumnWidth);

    // Terminal-width-aware shrinking
    if (config.terminalWidth > 0)
    {
        // Compute total table width including overhead
        int overhead = 0;
        if (config.style == TableStyle::Bordered)
            overhead = static_cast<int>(numCols + 1) + 2 * static_cast<int>(numCols); // borders + padding
        else if (config.style == TableStyle::Compact)
            overhead = 1 + 2 * static_cast<int>(numCols - 1); // leading space + gaps
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

    std::string result;

    if (config.style == TableStyle::Bordered)
    {
        auto const bc = tui::BorderChars::fromStyle(tui::BorderStyle::Rounded);
        auto const dim = config.useColor ? "\033[2m" : "";
        auto const bold = config.useColor ? "\033[1m" : "";
        auto const reset = config.useColor ? "\033[0m" : "";

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
            result += padRight(headers[col], colWidths[col]);
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
        for (auto const& row: rows)
        {
            result += dim;
            result += bc.vertical;
            result += reset;
            for (size_t col = 0; col < numCols; ++col)
            {
                result += ' ';
                result += padRight(row[col], colWidths[col]);
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
        auto const bold = config.useColor ? "\033[1m" : "";
        auto const reset = config.useColor ? "\033[0m" : "";

        // Header row
        result += ' ';
        for (size_t col = 0; col < numCols; ++col)
        {
            if (col > 0)
                result += "  "; // 2-space gap
            result += bold;
            result += padRight(headers[col], colWidths[col]);
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
        for (auto const& row: rows)
        {
            result += ' ';
            for (size_t col = 0; col < numCols; ++col)
            {
                if (col > 0)
                    result += "  ";
                result += padRight(row[col], colWidths[col]);
            }
            result += '\n';
        }
    }
    else // Plain
    {
        // Header row
        for (size_t col = 0; col < numCols; ++col)
        {
            if (col > 0)
                result += "  ";
            result += padRight(headers[col], colWidths[col]);
        }
        result += '\n';

        // Data rows
        for (auto const& row: rows)
        {
            for (size_t col = 0; col < numCols; ++col)
            {
                if (col > 0)
                    result += "  ";
                result += padRight(row[col], colWidths[col]);
            }
            result += '\n';
        }
    }

    return result;
}

} // namespace endo
