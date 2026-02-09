// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>
#include <string>

#include "LspTypes.hpp"

namespace endo::lsp
{

/// Computes rename edits for the symbol at the given position.
///
/// Finds all references (including the declaration) and creates TextEdit entries
/// to replace each occurrence with the new name.
///
/// @param source The full document text
/// @param uri The document URI (used to construct the WorkspaceEdit)
/// @param position The cursor position (0-based line and character)
/// @param newName The new name to rename the symbol to
/// @return WorkspaceEdit with TextEdit entries, or std::nullopt if no symbol at position
[[nodiscard]] std::optional<WorkspaceEdit> computeRename(std::string const& source,
                                                         std::string const& uri,
                                                         Position position,
                                                         std::string const& newName);

/// Validates that rename is possible at the given position (prepareRename).
///
/// @param source The full document text
/// @param position The cursor position (0-based line and character)
/// @return Range of the identifier at the cursor, or std::nullopt if rename is not possible
[[nodiscard]] std::optional<Range> prepareRename(std::string const& source, Position position);

} // namespace endo::lsp
