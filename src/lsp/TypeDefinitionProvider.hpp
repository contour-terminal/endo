// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>
#include <string>

#include "LspTypes.hpp"

namespace endo::lsp
{

/// Computes the type definition location for the symbol at the given position.
///
/// If the variable at the cursor has a type annotation matching a user-defined
/// record or union type, returns the location of that type definition.
///
/// @param source The full document text
/// @param uri The document URI
/// @param position The cursor position
/// @return The type definition location, or std::nullopt if not applicable
[[nodiscard]] std::optional<Location> computeTypeDefinition(std::string const& source,
                                                            std::string const& uri,
                                                            Position position);

} // namespace endo::lsp
