// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <endo-language/Lexer.hpp>

namespace endo
{

/// @brief Semantic category for a lexer token, shared between LSP and shell syntax highlighting.
enum class TokenCategory
{
    Default,
    Keyword,
    Number,
    String,
    Operator,
    Variable,
    Constructor,
    Comment,
    Type,
    Punctuation,
};

/// @brief Classifies an endo Token into a semantic category.
/// @param token The token to classify.
/// @return The semantic category of the token.
[[nodiscard]] constexpr TokenCategory classifyTokenCategory(Token token) noexcept
{
    using enum Token;
    switch (token)
    {
        // Keywords
        case Let:
        case Mut:
        case Fun:
        case Match:
        case With:
        case When:
        case Type:
        case Of:
        case Rec:
        case And:
        case As:
        case Try:
        case Finally:
        case True:
        case False: return TokenCategory::Keyword;

        // Numbers
        case Number: return TokenCategory::Number;

        // Strings
        case String:
        case DblQuoteStart:
        case DblQuoteEnd:
        case StringFragment:
        case FStringStart:
        case FStringEnd: return TokenCategory::String;

        // Constructors (enum members)
        case OptionSome:
        case OptionNone:
        case ResultOk:
        case ResultError: return TokenCategory::Constructor;

        // Operators
        case Plus:
        case Minus:
        case Star:
        case Slash:
        case Percent:
        case StarStar:
        case Arrow:
        case ForwardPipe:
        case Pipe:
        case EqualEqual:
        case NotEqual:
        case Less:
        case Greater:
        case LessEqual:
        case GreaterEqual:
        case ColonColon:
        case DotDot:
        case Ellipsis:
        case LeftArrow:
        case Question:
        case Caret:
        case AmpAmp:
        case PipePipe:
        case Not:
        case Equal: return TokenCategory::Operator;

        // Shell variables
        case DollarName:
        case DollarBraceName:
        case DollarQuestion:
        case DollarDollar:
        case DollarNot:
        case DollarNumber:
        case DollarBraceParam: return TokenCategory::Variable;

        // Identifiers
        case Identifier: return TokenCategory::Variable;

        // Punctuation
        case RndOpen:
        case RndClose:
        case BracketOpen:
        case BracketClose:
        case BraceOpen:
        case BraceClose:
        case Comma:
        case Colon:
        case Semicolon:
        case FStringExprStart:
        case FStringExprEnd: return TokenCategory::Punctuation;

        // Everything else (whitespace, EOF, etc.)
        default: return TokenCategory::Default;
    }
}

} // namespace endo
