// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <endo-language/Lexer.hpp>
#include <endo-language/TokenClassification.hpp>

namespace endo
{

/// @brief A token with its classification determined by context-aware mode tracking.
///
/// Unlike raw lexer tokens, the category here accounts for whether the token
/// appears in shell context (paths stay as single tokens) or F# context
/// (operators are tokenized individually).
struct ClassifiedToken
{
    Token token;                  ///< The lexer token type.
    std::string literal;          ///< The token's literal text.
    SourceLocationRange location; ///< Source location range.
    TokenCategory category;       ///< Context-aware semantic category.
};

/// @brief Tokenizes source with context-aware shell/F# mode tracking.
///
/// Starts in shell mode and switches to F# mode based on the first token of each
/// statement, mirroring the parser's own heuristic. Shell builtins are classified
/// as TokenCategory::Function. Shell command arguments keep paths as single tokens.
///
/// @param source The input text to tokenize.
/// @return A vector of classified tokens.
[[nodiscard]] std::vector<ClassifiedToken> tokenizeWithContext(std::string_view source);

} // namespace endo
