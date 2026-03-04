// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>

#include "LspTypes.hpp"

namespace endo::lsp
{

/// Computes document highlights for the symbol at the given cursor position.
///
/// Returns all occurrences of the same symbol within the document, with
/// definitions marked as Write and references marked as Read.
///
/// @param source The full document text
/// @param position The cursor position (0-based line and character)
/// @return Vector of document highlights (may be empty)
[[nodiscard]] std::vector<DocumentHighlight> computeDocumentHighlights(std::string const& source,
                                                                       Position position);

} // namespace endo::lsp
