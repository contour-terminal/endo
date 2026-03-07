// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>

#include "LspTypes.hpp"

namespace endo::lsp
{

/// Computes formatting edits for a range of lines in an Endo source document.
///
/// Formats the full document via SourceFormatter::format() and emits TextEdits
/// only for lines within the requested range.
///
/// @param source The full document source text
/// @param range The range to format
/// @return A list of TextEdits covering the requested range, or empty if unchanged
[[nodiscard]] std::vector<TextEdit> computeRangeFormatting(std::string const& source, Range range);

} // namespace endo::lsp
