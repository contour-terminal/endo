// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>
#include <string>

#include "LspTypes.hpp"

namespace endo::lsp
{

/// Computes the definition location for the symbol at the given cursor position.
///
/// @param source The full document text
/// @param uri The document URI (used to construct the Location result)
/// @param position The cursor position (0-based line and character)
/// @return Location of the definition, or std::nullopt if not found
[[nodiscard]] std::optional<Location> computeDefinition(std::string const& source,
                                                        std::string const& uri,
                                                        Position position);

} // namespace endo::lsp
