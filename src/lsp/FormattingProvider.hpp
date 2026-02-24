// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>

#include "LspTypes.hpp"

namespace endo::lsp
{

/// Computes formatting edits for an entire Endo source document.
///
/// Uses the SourceFormatter to reformat the source and returns a single TextEdit
/// covering the entire document range.
///
/// @param source The full document source text.
/// @return A list of TextEdits (typically one covering the full document), or empty if unchanged.
[[nodiscard]] std::vector<TextEdit> computeFormatting(std::string const& source);

} // namespace endo::lsp
