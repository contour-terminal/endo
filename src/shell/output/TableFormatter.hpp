// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <CoreVM/CoreVM.hpp>
#include <CoreVM/types/TypedObject.hpp>

#include <cstdint>
#include <string>

namespace endo
{

/// Table rendering style.
enum class TableStyle : uint8_t
{
    Bordered, ///< Full Unicode box-drawing borders (default for terminals)
    Compact,  ///< Header underline only, space-separated columns
    Plain,    ///< No borders or escape sequences (for pipes/non-terminal)
};

/// Configuration for table rendering.
struct TableConfig
{
    TableStyle style = TableStyle::Bordered; ///< Table style
    bool useColor = true;                    ///< Emit SGR escape sequences
    bool showIcons = true;                   ///< Show file type icons (only when useColor is true)
    bool showDirectorySlash = true;          ///< Append trailing '/' to directory names
    int maxColumnWidth = 40;                 ///< Max width per column
    int terminalWidth = 0;                   ///< Terminal width in columns (0 = no constraint)
};

/// Checks if a TypedObject is a non-empty list where all elements are Product-type records.
///
/// @param obj  Head of the list (must be a List-typed object)
/// @param runner  VM runner for type inspection
/// @return true if the list is non-empty and all elements are Product-type records
[[nodiscard]] bool isListOfRecords(CoreVM::TypedObject* obj, CoreVM::Runner* runner);

/// Formats a list of records as a styled table string.
///
/// @param listHead  Head of the list (must pass isListOfRecords check)
/// @param runner  VM runner for value conversion
/// @param config  Table rendering configuration
/// @return Formatted table string (includes trailing newline)
[[nodiscard]] std::string formatRecordTable(CoreVM::TypedObject* listHead,
                                            CoreVM::Runner* runner,
                                            TableConfig const& config = {});

} // namespace endo
