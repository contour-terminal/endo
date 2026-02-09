// SPDX-License-Identifier: Apache-2.0
#include "HoverProvider.hpp"

#include <unordered_map>

#include <endo-language/Lexer.hpp>

namespace endo::lsp
{

namespace
{

    /// Checks if a 0-based LSP position falls within a source location range.
    /// The lexer uses 1-based columns, so we convert during comparison.
    [[nodiscard]] bool containsPosition(SourceLocationRange const& range, Position pos)
    {
        // Convert lexer 1-based columns to 0-based for comparison
        auto const beginCol = range.begin.column > 0 ? range.begin.column - 1 : 0;
        auto const endCol = range.end.column > 0 ? range.end.column - 1 : 0;

        // Check if position is on or after the start
        if (pos.line < range.begin.line)
            return false;
        if (pos.line == range.begin.line && pos.character < beginCol)
            return false;

        // Check if position is before the end
        if (pos.line > range.end.line)
            return false;
        if (pos.line == range.end.line && pos.character >= endCol)
            return false;

        return true;
    }

    /// Returns hover markdown for a keyword token.
    [[nodiscard]] std::optional<std::string> keywordHover(Token token)
    {
        using enum Token;
        switch (token)
        {
            case Let:
                return "`let` \u2014 Introduces an immutable binding\n\n```\nlet name = value\nlet f x y = "
                       "body\n```";
            case Mut:
                return "`mut` \u2014 Marks a binding as mutable\n\n```\nlet mut counter = 0\ncounter <- "
                       "counter + 1\n```";
            case Fun:
                return "`fun` \u2014 Lambda expression (anonymous function)\n\n```\nfun x -> x + 1\nfun x y "
                       "-> x + y\n```";
            case Match:
                return "`match` \u2014 Pattern matching expression\n\n```\nmatch value with\n| pattern1 -> "
                       "result1\n| pattern2 -> result2\n```";
            case With: return "`with` \u2014 Introduces match arms or exception handlers";
            case When:
                return "`when` \u2014 Guard clause in pattern matching\n\n```\n| x when x > 0 -> "
                       "\"positive\"\n```";
            case Type:
                return "`type` \u2014 Defines a discriminated union type\n\n```\ntype Shape =\n| Circle of "
                       "float\n| Rectangle of float * float\n```";
            case Of: return "`of` \u2014 Specifies the payload type in a union case";
            case Rec:
                return "`rec` \u2014 Marks a binding as recursive\n\n```\nlet rec factorial n =\n  if n <= 1 "
                       "then 1\n  else n * factorial (n - 1)\n```";
            case And:
                return "`and` \u2014 Defines mutually recursive functions\n\n```\nlet rec isEven n = ... and "
                       "isOdd n = ...\n```";
            case As:
                return "`as` \u2014 Pattern alias, binds the whole matched value\n\n```\n| (Some x) as opt "
                       "-> ...\n```";
            case Try:
                return "`try` \u2014 Error handling expression\n\n```\ntry expression with\n| Error e -> "
                       "handler\n```";
            case Finally:
                return "`finally` \u2014 Code that always executes after try\n\n```\ntry expression finally "
                       "cleanup\n```";
            default: return std::nullopt;
        }
    }

    /// Returns hover markdown for constructor tokens.
    [[nodiscard]] std::optional<std::string> constructorHover(Token token)
    {
        using enum Token;
        switch (token)
        {
            case OptionSome: return "`Some` : `'a -> option<'a>`\n\nWraps a value in an option type.";
            case OptionNone: return "`None` : `option<'a>`\n\nRepresents the absence of a value.";
            case ResultOk: return "`Ok` : `'a -> result<'a, 'e>`\n\nWraps a success value in a result type.";
            case ResultError:
                return "`Error` : `'e -> result<'a, 'e>`\n\nWraps an error value in a result type.";
            default: return std::nullopt;
        }
    }

    /// Returns hover markdown for operator tokens.
    [[nodiscard]] std::optional<std::string> operatorHover(Token token)
    {
        using enum Token;
        switch (token)
        {
            case ForwardPipe:
                return "`|>` \u2014 Forward pipe operator\n\nPasses the left operand as the last argument to "
                       "the right function.\n\n```\nvalue |> f |> g\n```";
            case Arrow:
                return "`->` \u2014 Arrow operator\n\nUsed in function types, lambda expressions, and match "
                       "arms.";
            case LeftArrow:
                return "`<-` \u2014 Mutation operator\n\nAssigns a new value to a mutable binding.";
            case ColonColon:
                return "`::` \u2014 List cons operator\n\nPrepends an element to a list.\n\n```\n1 :: [2; 3] "
                       " // [1; 2; 3]\n```";
            case DotDot:
                return "`..` \u2014 Range operator\n\nCreates a range of values.\n\n```\n[1..10]\n```";
            case Question:
                return "`?` \u2014 Error propagation operator\n\nUnwraps Ok/Some or returns early with "
                       "Error/None.";
            case EqualEqual: return "`==` \u2014 Equality comparison";
            case NotEqual: return "`!=` \u2014 Inequality comparison";
            case AmpAmp: return "`&&` \u2014 Logical AND";
            case PipePipe: return "`||` \u2014 Logical OR";
            case StarStar: return "`**` \u2014 Exponentiation operator";
            case Pipe: return "`|` \u2014 Process pipe (shell) or match arm separator";
            default: return std::nullopt;
        }
    }

    /// Returns hover markdown for builtin function identifiers.
    [[nodiscard]] std::optional<std::string> builtinHover(std::string const& name)
    {
        static std::unordered_map<std::string, std::string> const builtins = {
            { "print", "`print` : `'a -> unit`\n\nPrints a value to stdout without a trailing newline." },
            { "println", "`println` : `'a -> unit`\n\nPrints a value to stdout followed by a newline." },
            { "string_length", "`string_length` : `string -> int`\n\nReturns the length of a string." },
            { "string_concat",
              "`string_concat` : `string -> string -> string`\n\nConcatenates two strings." },
            { "string_substring",
              "`string_substring` : `int -> int -> string -> string`\n\nExtracts a substring (start, length, "
              "string)." },
            { "int_to_string",
              "`int_to_string` : `int -> string`\n\nConverts an integer to its string representation." },
            { "string_to_int",
              "`string_to_int` : `string -> option<int>`\n\nParses a string as an integer, returning None on "
              "failure." },
            { "true", "`true` : `bool`\n\nBoolean true value." },
            { "false", "`false` : `bool`\n\nBoolean false value." },
        };

        if (auto const it = builtins.find(name); it != builtins.end())
            return it->second;
        return std::nullopt;
    }

} // namespace

std::optional<Hover> computeHover(std::string const& source, Position position)
{
    // Tokenize with F# mode for proper operator recognition
    auto lexer = Lexer { std::make_unique<StringSource>(source) };
    lexer.enterFSharpExpr();

    std::vector<TokenInfo> tokens;
    while (lexer.currentToken() != Token::EndOfInput)
    {
        tokens.emplace_back(TokenInfo { lexer.currentToken(), lexer.currentLiteral(), lexer.currentRange() });
        lexer.nextToken();
    }

    for (auto const& tokenInfo: tokens)
    {
        if (tokenInfo.token == Token::EndOfInput)
            continue;

        if (!containsPosition(tokenInfo.location, position))
            continue;

        auto const range = toRange(tokenInfo.location);

        // Try keyword hover
        if (auto text = keywordHover(tokenInfo.token))
            return Hover { .contents = MarkupContent { .value = std::move(*text) }, .range = range };

        // Try constructor hover
        if (auto text = constructorHover(tokenInfo.token))
            return Hover { .contents = MarkupContent { .value = std::move(*text) }, .range = range };

        // Try operator hover
        if (auto text = operatorHover(tokenInfo.token))
            return Hover { .contents = MarkupContent { .value = std::move(*text) }, .range = range };

        // Try builtin hover for identifiers
        if (tokenInfo.token == Token::Identifier)
        {
            if (auto text = builtinHover(tokenInfo.literal))
                return Hover { .contents = MarkupContent { .value = std::move(*text) }, .range = range };
        }

        // No hover info for this token
        return std::nullopt;
    }

    return std::nullopt;
}

} // namespace endo::lsp
