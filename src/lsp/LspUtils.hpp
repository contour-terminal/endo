// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <endo-language/lexer/Lexer.hpp>

#include <sstream>
#include <string>
#include <vector>

namespace endo::lsp
{

/// Token entry used by LSP providers for token-based position lookups.
struct TokenEntry
{
    Token token;
    std::string literal;
    SourceLocationRange range;
};

/// Tokenizes source in F# expression mode and returns all tokens.
///
/// @param source The full document text.
/// @return Vector of token entries with token type, literal text, and source range.
[[nodiscard]] inline std::vector<TokenEntry> tokenize(std::string const& source)
{
    std::vector<TokenEntry> tokens;
    auto lexer = Lexer { std::make_unique<StringSource>(source) };
    lexer.enterFSharpExpr();
    while (lexer.currentToken() != Token::EndOfInput)
    {
        tokens.push_back(TokenEntry {
            .token = lexer.currentToken(),
            .literal = lexer.currentLiteral(),
            .range = lexer.currentRange(),
        });
        lexer.nextToken();
    }
    return tokens;
}

/// Splits text into individual lines.
///
/// @param text The text to split.
/// @return Vector of lines (at least one element, even for empty input).
[[nodiscard]] inline std::vector<std::string> splitLines(std::string const& text)
{
    std::vector<std::string> lines;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line))
        lines.push_back(line);
    if (lines.empty())
        lines.emplace_back();
    return lines;
}

} // namespace endo::lsp
