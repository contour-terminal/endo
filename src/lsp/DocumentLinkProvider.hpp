// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>

#include "LspTypes.hpp"

namespace endo::lsp
{

/// Computes document links for source file references and file paths.
///
/// Finds string literals in:
/// - `source "filepath"` shell commands
/// - `File.open "path"` calls
///
/// @param source The full document text
/// @param uri The document URI (for resolving relative paths)
/// @return A vector of document links
[[nodiscard]] std::vector<DocumentLink> computeDocumentLinks(std::string const& source,
                                                             std::string const& uri);

/// Resolves a document link's target.
///
/// @param link The document link to resolve
/// @return The resolved document link
[[nodiscard]] DocumentLink resolveDocumentLink(DocumentLink link);

} // namespace endo::lsp
