// SPDX-License-Identifier: Apache-2.0
#include "SemanticTokens.hpp"

#include <endo-language/lexer/ContextAwareTokenizer.hpp>
#include <endo-language/lexer/Lexer.hpp>
#include <endo-language/lexer/TokenClassification.hpp>

namespace endo::lsp
{

namespace
{

    // Semantic token type indices (must match legend order)
    constexpr int TypeKeyword = 0;
    constexpr int TypeFunction = 1;
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
            case Keyword: return { .type = TypeKeyword, .modifiers = 0 };
            case Number: return { .type = TypeNumber, .modifiers = 0 };
            case String: return { .type = TypeString, .modifiers = 0 };
            case Constructor: return { .type = TypeEnumMember, .modifiers = 0 };
            case Operator: return { .type = TypeOperator, .modifiers = 0 };
            case Function: return { .type = TypeFunction, .modifiers = 0 };
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
                    case DollarBraceParam: return { .type = TypeVariable, .modifiers = ModModification };
                    default: return { .type = TypeVariable, .modifiers = 0 };
                }
            }
            case Comment: return { .type = TypeComment, .modifiers = 0 };
            case Type: return { .type = TypeType, .modifiers = 0 };
            case Punctuation:
            case Default: return { .type = -1, .modifiers = 0 };
        }
        return { .type = -1, .modifiers = 0 };
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

SemanticTokens computeSemanticTokens(std::string const& source)
{
    auto const tokens = tokenizeWithContext(source);

    SemanticTokens result;
    int prevLine = 0;
    int prevChar = 0;

    for (auto const& classified: tokens)
    {
        // Skip non-semantic tokens
        if (classified.token == Token::EndOfInput || classified.token == Token::LineFeed
            || classified.token == Token::Semicolon)
            continue;

        auto const category = classified.category;
        auto const classification = toLspClassification(category, classified.token);
        if (classification.type < 0)
            continue;

        auto const line = classified.location.begin.line;
        auto const character = classified.location.begin.column;

        // Compute length from source location range and literal size.
        // Use the maximum of range-based length and literal length because:
        // 1. Operator tokens have empty literals (range is correct)
        // 2. The last token before EOF may have a truncated range
        auto rangeLength = 0;
        if (classified.location.begin.line == classified.location.end.line)
            rangeLength = classified.location.end.column - classified.location.begin.column;

        auto const literalLength = static_cast<int>(classified.literal.size());
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
