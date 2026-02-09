// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>

#include "LspTypes.hpp"

namespace endo::lsp
{

/// Creates the semantic tokens legend that describes available token types and modifiers.
///
/// Token types: keyword, function, variable, number, string, operator, enumMember, comment, type
/// Token modifiers: declaration, modification
/// @return The semantic tokens legend
[[nodiscard]] SemanticTokensLegend createSemanticTokensLegend();

/// Computes semantic tokens for the given source text.
///
/// Tokenizes the source using the Endo Lexer and maps each token to its LSP semantic token type.
/// The result is delta-encoded as per the LSP specification: each token is represented as
/// 5 integers [deltaLine, deltaStartChar, length, tokenType, tokenModifiers].
///
/// @param source The full document text
/// @return The delta-encoded semantic tokens
[[nodiscard]] SemanticTokens computeSemanticTokens(std::string const& source);

} // namespace endo::lsp
