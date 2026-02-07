// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "Lexer.hpp"

#include <CoreVM/CoreVM.hpp>

#include <optional>
#include <string>
#include <string_view>

namespace endo
{

/// Converts shell's SourceLocationRange (0-based line/column) to CoreVM's SourceLocation.
///
/// @param range The shell source location range to convert
/// @return CoreVM SourceLocation with the begin position from the range
[[nodiscard]] CoreVM::SourceLocation toCoreLoc(SourceLocationRange const& range);

/// Converts shell's SourceLocation (0-based) to CoreVM's SourceLocation.
///
/// @param loc The shell source location to convert
/// @return CoreVM SourceLocation
[[nodiscard]] CoreVM::SourceLocation toCoreLoc(SourceLocation const& loc);

/// Extracts a specific line from source text.
///
/// @param source The full source text
/// @param line The 0-based line number to extract
/// @return The line content (without newline), or empty string if line is out of range
[[nodiscard]] std::string extractSourceLine(std::string_view source, int line);

/// Creates a caret line pointing to a specific column with optional extent.
///
/// @param column The 0-based column where the caret should point
/// @param length The length of the underline (default 1)
/// @return A string like "     ^~~~" with spaces, caret, and tildes
[[nodiscard]] std::string createCaretLine(int column, int length = 1);

} // namespace endo
