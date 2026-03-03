// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>

#include "LspTypes.hpp"

namespace endo::lsp
{

/// Computes inlay hints for inferred types in the given source.
///
/// Walks the AST and runs Hindley-Milner type inference to produce inline
/// type annotations for:
/// - Untyped function parameters
/// - Unannotated function return types
/// - Let-binding variable types
///
/// @param source The full document text
/// @param range The visible range to restrict hints to
/// @return A vector of inlay hints (empty if inference fails or no hints apply)
[[nodiscard]] std::vector<InlayHint> computeInlayHints(std::string const& source, Range range);

} // namespace endo::lsp
