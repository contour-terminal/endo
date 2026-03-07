// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>

#include "LspTypes.hpp"

namespace endo::lsp
{

/// Computes code lenses for function definitions in the source.
///
/// Each function definition gets a code lens with deferred reference count.
///
/// @param source The full document text
/// @param uri The document URI
/// @return A vector of code lenses (one per function definition)
[[nodiscard]] std::vector<CodeLens> computeCodeLenses(std::string const& source, std::string const& uri);

/// Resolves a code lens by computing reference count for the function.
///
/// @param source The full document text
/// @param lens The code lens to resolve
/// @return The resolved code lens with command title set
[[nodiscard]] CodeLens resolveCodeLens(std::string const& source, CodeLens lens);

} // namespace endo::lsp
