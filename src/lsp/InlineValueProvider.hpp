// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>

#include "LspTypes.hpp"

namespace endo::lsp
{

/// Computes inline value variable lookups for the given range.
///
/// This is a skeleton implementation for future DAP integration.
/// Returns InlineValueVariableLookup entries for all variable references
/// found within the requested range.
///
/// @param source The full document text
/// @param range The visible range to restrict inline values to
/// @return A vector of inline value variable lookup entries
[[nodiscard]] std::vector<InlineValueVariableLookup> computeInlineValues(std::string const& source,
                                                                         Range range);

} // namespace endo::lsp
