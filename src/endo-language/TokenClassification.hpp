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
    Function, ///< Shell builtins (cd, export, exit, etc.)
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

/// @brief Checks whether a token is an F#-style keyword that starts an F# statement.
/// @param token The token to check.
/// @return True if the token is a known F# statement-starting keyword or constructor.
[[nodiscard]] constexpr bool isFSharpStartToken(Token token) noexcept
{
    using enum Token;
    switch (token)
    {
        case Let:
        case Fun:
        case Match:
        case Type:
        case Rec:
        case Try:
        case Mut:
        case True:
        case False:
        case OptionSome:
        case OptionNone:
        case ResultOk:
        case ResultError: return true;
        default: return false;
    }
}

/// @brief Checks whether an identifier is a known F# function that triggers F# mode at statement start.
/// @param name The identifier to check.
/// @return True if the identifier is a known F# function.
[[nodiscard]] constexpr bool isKnownFSharpFunction(std::string_view name) noexcept
{
    return name == "print" || name == "println" || name == "each" || name == "rand" || name == "exec";
}

/// @brief Checks whether an identifier is a known shell builtin command.
/// @param name The identifier to check.
/// @return True if the identifier is a known shell builtin.
[[nodiscard]] constexpr bool isShellBuiltin(std::string_view name) noexcept
{
    return name == "cd" || name == "echo" || name == "exit" || name == "export" || name == "pwd"
           || name == "env" || name == "read" || name == "time" || name == "fg" || name == "bg"
           || name == "source" || name == "alias" || name == "unalias" || name == "set" || name == "unset"
           || name == "jobs" || name == "kill" || name == "wait" || name == "eval" || name == "test"
           || name == "true" || name == "false" || name == "set_prompt_preset"
           || name == "set_prompt_indicator" || name == "set_prompt_layout" || name == "set_prompt_separator"
           || name == "set_prompt_transient" || name == "set_prompt_duration_threshold"
           || name == "set_prompt_spacing";
}

} // namespace endo
