// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>

#include "LspTypes.hpp"

namespace endo::lsp
{

/// Computes a hierarchical list of document symbols for the outline view.
///
/// Top-level `let` bindings become root symbols. Function parameters become
/// children of their enclosing function symbol.
///
/// @param source The full document text
/// @return Vector of DocumentSymbol entries (may be empty on parse failure)
[[nodiscard]] std::vector<DocumentSymbol> computeDocumentSymbols(std::string const& source);

} // namespace endo::lsp
