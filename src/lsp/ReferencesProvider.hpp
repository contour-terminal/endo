// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>

#include "LspTypes.hpp"

namespace endo::lsp
{

/// Computes all reference locations for the symbol at the given cursor position.
///
/// @param source The full document text
/// @param uri The document URI (used to construct Location results)
/// @param position The cursor position (0-based line and character)
/// @param includeDeclaration Whether to include the declaration itself in the results
/// @return Vector of reference locations (may be empty)
[[nodiscard]] std::vector<Location> computeReferences(std::string const& source,
                                                      std::string const& uri,
                                                      Position position,
                                                      bool includeDeclaration);

} // namespace endo::lsp
