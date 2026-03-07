// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>

#include "LspTypes.hpp"

namespace endo::lsp
{

/// Computes folding ranges for collapsible regions in the given source.
///
/// Walks the AST and collects multi-line constructs (functions, match expressions,
/// if-then-else, loops, blocks, type definitions) as region folds, and multi-line
/// comments as comment folds.
///
/// @param source The full document text
/// @return A vector of folding ranges (empty if parsing fails or no multi-line constructs exist)
[[nodiscard]] std::vector<FoldingRange> computeFoldingRanges(std::string const& source);

} // namespace endo::lsp
