// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <editor-protocol/DocumentStore.hpp>

#include <string>
#include <vector>

#include "LspTypes.hpp"

namespace endo::lsp
{

/// Computes workspace symbols by searching across all open documents.
///
/// Iterates all documents in the store, collects symbols from each,
/// and filters by query (case-insensitive substring match).
///
/// @param documents The document store containing all open documents
/// @param query The search query (empty returns all symbols)
/// @return A vector of matching SymbolInformation entries
[[nodiscard]] std::vector<SymbolInformation> computeWorkspaceSymbols(
    endo::editor_protocol::DocumentStore const& documents, std::string const& query);

} // namespace endo::lsp
