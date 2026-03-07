// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>

#include "LspTypes.hpp"

namespace endo::lsp
{

/// Computes selection ranges for smart expand/shrink selection.
///
/// For each requested cursor position, walks the AST to find all nodes containing
/// that position, sorts them by range size (smallest first), and builds a linked
/// SelectionRange chain from innermost to outermost.
///
/// @param source The full document text
/// @param positions The cursor positions to compute selection ranges for
/// @return A vector of SelectionRange objects (one per input position)
[[nodiscard]] std::vector<SelectionRange> computeSelectionRanges(std::string const& source,
                                                                 std::vector<Position> const& positions);

} // namespace endo::lsp
