// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <endo-language/ide/HoverInfo.hpp>

#include <cstddef>
#include <string_view>
#include <vector>

namespace endo
{

/// @brief Builds a table of byte offsets for line starts.
/// @param source The full source text.
/// @return A vector where index i is the byte offset of line i's first character.
[[nodiscard]] std::vector<std::size_t> buildLineStartOffsets(std::string_view source);

/// @brief Converts a codepoint-based column (1-based) to a byte offset within the source.
/// @param source The full source text.
/// @param lineStartByte The byte offset where this line starts.
/// @param column The 1-based codepoint column from the Lexer.
/// @return The byte offset corresponding to the given column.
[[nodiscard]] std::size_t columnToByteOffset(std::string_view source,
                                             std::size_t lineStartByte,
                                             int column) noexcept;

/// @brief Converts a 0-based SourcePosition to a byte offset within the source.
/// @param source The full source text.
/// @param lineStarts Precomputed line start byte offsets (from buildLineStartOffsets).
/// @param pos The 0-based source position (line, character in codepoints).
/// @return The byte offset corresponding to the given position.
[[nodiscard]] std::size_t positionToByteOffset(std::string_view source,
                                               std::vector<std::size_t> const& lineStarts,
                                               SourcePosition pos) noexcept;

} // namespace endo
