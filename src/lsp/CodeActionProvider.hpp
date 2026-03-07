// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>

#include "LspTypes.hpp"

namespace endo::lsp
{

/// Computes code actions (quick fixes) for the given document range and diagnostics.
///
/// Extracts raw suggestions from diagnostic data fields and generates CodeAction
/// objects with appropriate TextEdits. Supports:
/// - "Did you mean?" suggestions for misspelled identifiers
/// - General informational suggestions without edits
///
/// @param source The full document text
/// @param uri The document URI (for workspace edit construction)
/// @param range The requested range (typically the current selection or cursor)
/// @param diagnostics The client-provided diagnostics overlapping the range
/// @return A vector of code actions (empty if no suggestions apply)
[[nodiscard]] std::vector<CodeAction> computeCodeActions(std::string const& source,
                                                         std::string const& uri,
                                                         Range range,
                                                         std::vector<Diagnostic> const& diagnostics);

} // namespace endo::lsp
