// SPDX-License-Identifier: Apache-2.0
#include "SemanticTokens.hpp"

#include <endo-language/Lexer.hpp>
#include <endo-language/TokenClassification.hpp>

namespace endo::lsp
{

namespace
{

    // Semantic token type indices (must match legend order)
    constexpr int TypeKeyword = 0;
    [[maybe_unused]] constexpr int TypeFunction = 1;
    constexpr int TypeVariable = 2;
    constexpr int TypeNumber = 3;
    constexpr int TypeString = 4;
    constexpr int TypeOperator = 5;
    constexpr int TypeEnumMember = 6;
    constexpr int TypeComment = 7;
    constexpr int TypeType = 8;

    // Semantic token modifier bit masks (must match legend order)
    [[maybe_unused]] constexpr int ModDeclaration = 1 << 0;
    constexpr int ModModification = 1 << 1;

    /// @brief LSP-specific classification result with type index and modifier bitmask.
    struct LspTokenClassification
    {
        int type = -1;     ///< -1 means skip this token
        int modifiers = 0; ///< Bitmask of modifier flags
    };

    /// @brief Maps a TokenCategory to an LSP semantic token type/modifier pair.
    /// @param category The shared token category.
    /// @param token The original token (needed for shell variable modifier detection).
    /// @return LSP-specific classification.
    [[nodiscard]] LspTokenClassification toLspClassification(TokenCategory category, Token token)
    {
        using enum TokenCategory;
        switch (category)
        {
            case Keyword: return { TypeKeyword, 0 };
            case Number: return { TypeNumber, 0 };
            case String: return { TypeString, 0 };
            case Constructor: return { TypeEnumMember, 0 };
            case Operator: return { TypeOperator, 0 };
            case Variable: {
                // Shell variables get the modification modifier
                using enum Token;
                switch (token)
                {
                    case DollarName:
                    case DollarBraceName:
                    case DollarQuestion:
                    case DollarDollar:
                    case DollarNot:
                    case DollarNumber:
                    case DollarBraceParam: return { TypeVariable, ModModification };
                    default: return { TypeVariable, 0 };
                }
            }
            case Comment: return { TypeComment, 0 };
            case Type: return { TypeType, 0 };
            case Punctuation:
            case Default: return { -1, 0 };
        }
        return { -1, 0 };
    }

} // namespace

SemanticTokensLegend createSemanticTokensLegend()
{
    return SemanticTokensLegend {
        .tokenTypes = { "keyword",
                        "function",
                        "variable",
                        "number",
                        "string",
                        "operator",
                        "enumMember",
                        "comment",
                        "type" },
        .tokenModifiers = { "declaration", "modification" },
    };
}

namespace
{

    /// Tokenizes source with F# mode enabled for proper operator tokenization.
    [[nodiscard]] std::vector<TokenInfo> tokenizeForLsp(std::string const& source)
    {
        auto tokens = std::vector<TokenInfo> {};
        auto lexer = Lexer { std::make_unique<StringSource>(source) };
        lexer.enterFSharpExpr(); // Enable F# mode for operator tokenization

        while (lexer.currentToken() != Token::EndOfInput)
        {
            tokens.emplace_back(
                TokenInfo { lexer.currentToken(), lexer.currentLiteral(), lexer.currentRange() });
            lexer.nextToken();
        }

        return tokens;
    }

} // namespace

SemanticTokens computeSemanticTokens(std::string const& source)
{
    auto tokens = tokenizeForLsp(source);

    SemanticTokens result;
    int prevLine = 0;
    int prevChar = 0;

    for (auto const& tokenInfo: tokens)
    {
        // Skip non-semantic tokens
        if (tokenInfo.token == Token::EndOfInput || tokenInfo.token == Token::LineFeed
            || tokenInfo.token == Token::Semicolon)
            continue;

        auto const category = classifyTokenCategory(tokenInfo.token);
        auto const classification = toLspClassification(category, tokenInfo.token);
        if (classification.type < 0)
            continue;

        // Convert from lexer's 1-based columns to 0-based for LSP
        auto const line = tokenInfo.location.begin.line;
        auto const character = tokenInfo.location.begin.column > 0 ? tokenInfo.location.begin.column - 1 : 0;

        // Compute length from source location range and literal size.
        // Use the maximum of range-based length and literal length because:
        // 1. Operator tokens have empty literals (range is correct)
        // 2. The last token before EOF may have a truncated range
        auto rangeLength = 0;
        if (tokenInfo.location.begin.line == tokenInfo.location.end.line)
            rangeLength = tokenInfo.location.end.column - tokenInfo.location.begin.column;

        auto const literalLength = static_cast<int>(tokenInfo.literal.size());
        auto const length = std::max(rangeLength, literalLength);

        if (length <= 0)
            continue;

        auto const deltaLine = line - prevLine;
        auto const deltaStartChar = (deltaLine == 0) ? (character - prevChar) : character;

        result.data.push_back(deltaLine);
        result.data.push_back(deltaStartChar);
        result.data.push_back(length);
        result.data.push_back(classification.type);
        result.data.push_back(classification.modifiers);

        prevLine = line;
        prevChar = character;
    }

    return result;
}

} // namespace endo::lsp
