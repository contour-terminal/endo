// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstring>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <libunicode/utf8.h>

namespace endo
{

enum class Token
{
    Invalid,

    AmpNumber,      // '&' DIGIT+
    Backslash,      // '\'
    DollarDollar,   // $$
    DollarName,     // $NAME
    DollarNot,      // $!
    DollarQuestion, // $?
    DollarNumber,   // '$' DIGIT+
    EndOfInput,     // EOF
    LineFeed,       // LF
    Equal,          // =
    Greater,        // >
    GreaterEqual,   // >=
    GreaterGreater, // >>
    Less,           // <
    LessEqual,      // <=
    LessLess,       // <<
    LessRndOpen,    // <(
    Not,            // !
    Number,         // DIGIT+
    Pipe,           // |
    RndClose,       // )
    RndOpen,        // (
    Semicolon,      // ;
    String,         // "..." and '...'
    Identifier,     // space delimited text not containing any of the unescaped symbols above

    // New tokens added below to avoid shifting existing enum values
    DollarBraceName,  // ${NAME}
    LessLessLess,     // <<<
    LessLessDash,     // <<-
    GreaterAmp,       // >&
    AmpAmp,           // &&
    PipePipe,         // ||
    DollarRndOpen,    // $(
    GreaterRndOpen,   // >(
    Backtick,         // `
    Tilde,            // ~ at word start (for tilde expansion)
    DollarBraceParam, // ${VAR:-default}, ${#VAR}, etc. (parameter expansion)
    DollarDblRndOpen, // $((  (arithmetic expansion)
    DblRndClose,      // ))    (arithmetic expansion close)
    DblRndOpen,       // ((    (C-style for loop)
    DblSemicolon,     // ;;    (case clause terminator)
    Ampersand,        // &     (background execution)

    // String interpolation tokens
    DblQuoteStart,  // " at the start of a double-quoted string
    DblQuoteEnd,    // " at the end of a double-quoted string
    StringFragment, // Literal text fragment within double-quoted string

    // F#-style interpolated string tokens
    FStringStart,     // $" at the start of an F# interpolated string
    FStringEnd,       // " at the end of an F# interpolated string
    FStringExprStart, // { that opens an expression hole in an F# interpolated string
    FStringExprEnd,   // } that closes an expression hole in an F# interpolated string

    // F# style keywords
    Let,   // 'let'
    Mut,   // 'mut'
    Fun,   // 'fun'
    Match, // 'match'
    With,  // 'with'
    When,  // 'when'
    Type,  // 'type'
    Of,    // 'of'
    Rec,   // 'rec'
    And,   // 'and' (mutual recursion)
    As,    // 'as' (pattern alias)
    True,  // 'true' (boolean literal)
    False, // 'false' (boolean literal)
    // Note: 'in' is NOT a keyword token - it's recognized contextually by the parser

    // F# style constructors
    OptionSome,  // 'Some' (Option constructor)
    OptionNone,  // 'None' (Option constructor)
    ResultOk,    // 'Ok' (Result constructor)
    ResultError, // 'Error' (Result constructor)

    // F# style keywords for error handling
    Try,     // 'try' (try-with/try-finally expression)
    Finally, // 'finally' (try-finally expression)

    // F# style operators
    Arrow,       // '->'
    LeftArrow,   // '<-' (mutation)
    ForwardPipe, // '|>'
    DotDot,      // '..' (range operator)
    EqualEqual,  // '==' (equality comparison)
    NotEqual,    // '!=' (inequality comparison)

    // F# arithmetic/structural operators (context-sensitive, only in F# mode)
    Plus,         // '+'
    Minus,        // '-' (when not followed by digit)
    Star,         // '*'
    Slash,        // '/'
    Percent,      // '%'
    StarStar,     // '**' (exponentiation)
    Caret,        // '^' (bitwise XOR)
    ColonColon,   // '::' (list cons)
    At,           // '@' (list concatenation)
    Comma,        // ','
    Colon,        // ':'
    BracketOpen,  // '['
    BracketClose, // ']'
    BraceOpen,    // '{'
    BraceClose,   // '}'
    Question,     // '?'
    QuestionPipe, // '?|'
    QuestionDot,  // '?.' (optional chaining)
    Dot,          // '.' (field access, F# mode only)
    Ellipsis,     // '...' (variadic parameter / splat, F# mode only)
    // Note: >> uses GreaterGreater token (context determines compose vs redirect)
    // Note: << uses LessLess token (context determines back-compose vs here-doc)
};

enum class BuiltinFunction
{
    Exit,
    Cd,
    Pwd,
    Env,
    Fg,
    Bg,
    Read,  // read VAR
    Time,  // time COMMAND
    If,    // if (EXPR) COMMAND [else COMMAND]
    While, // while (EXPR) COMMAND
};

struct LineColumn
{
    int line = 0;   // 0-based index
    int column = 0; // 0-based index
};

struct SourceLocation
{
    int line = 0;          // 0-based index
    int column = 0;        // 0-based index
    std::string_view name; // e.g. stdin, or a filename
};

struct SourceLocationRange
{
    LineColumn begin;
    LineColumn end;
    std::string_view name; // e.g. stdin, or a filename
};

struct TokenInfo
{
    Token token;
    std::string literal;
    SourceLocationRange location;
};

class Source
{
  public:
    virtual ~Source() = default;

    virtual void rewind() = 0;
    [[nodiscard]] virtual char32_t readChar() = 0;
    [[nodiscard]] virtual char32_t peekChar() const = 0; ///< Look at next char without consuming
    [[nodiscard]] virtual std::string_view readGraphemeCluster() = 0;
    [[nodiscard]] virtual SourceLocation currentSourceLocation() const noexcept = 0;
};

class StringSource final: public Source
{
  public:
    explicit StringSource(std::string source): _source { std::move(source) } {}

    void rewind() override
    {
        _location.line = 0;
        _location.column = 0;
    }

    [[nodiscard]] char32_t readChar() override
    {
        if (_offset >= _source.size())
        {
            // On first EOF, advance column to maintain consistent "past-the-end" semantics
            // so confirmToken captures the correct end position for the last token.
            if (_offset == _source.size())
            {
                ++_location.column;
                ++_offset; // Prevent re-advancing on subsequent EOF reads
            }
            return static_cast<char32_t>(-1);
        }

        size_t bytesConsumed = 0;
        auto const result = unicode::from_utf8(_source.data() + _offset, &bytesConsumed);

        if (!std::holds_alternative<unicode::Success>(result))
        {
            // Invalid UTF-8: skip one byte, return replacement character
            ++_offset;
            ++_location.column;
            return U'\uFFFD';
        }

        auto const ch = std::get<unicode::Success>(result).value;
        _offset += bytesConsumed;
        ++_location.column;

        if (ch == U'\n')
        {
            ++_location.line;
            _location.column = 0;
        }

        return ch;
    }

    [[nodiscard]] std::string_view readGraphemeCluster() override
    {
        if (_offset >= _source.size())
            return {};

        size_t bytesConsumed = 0;
        auto const result = unicode::from_utf8(_source.data() + _offset, &bytesConsumed);

        if (std::holds_alternative<unicode::Success>(result))
            return std::string_view(_source.data() + _offset, bytesConsumed);

        // Invalid UTF-8: return single byte
        return std::string_view(_source.data() + _offset, 1);
    }

    [[nodiscard]] SourceLocation currentSourceLocation() const noexcept override { return _location; }

    [[nodiscard]] char32_t peekChar() const override
    {
        if (_offset >= _source.size())
            return static_cast<char32_t>(-1);

        size_t bytesConsumed = 0;
        auto const result = unicode::from_utf8(_source.data() + _offset, &bytesConsumed);

        if (!std::holds_alternative<unicode::Success>(result))
            return U'\uFFFD'; // Invalid UTF-8: return replacement character

        return std::get<unicode::Success>(result).value;
    }

  private:
    SourceLocation _location;
    std::string _source;
    size_t _offset = 0;
};

class Lexer
{
  public:
    explicit Lexer(std::unique_ptr<Source> source): _source { std::move(source) }
    {
        nextChar();
        nextToken();
    }

    Token nextToken();

    [[nodiscard]] Token currentToken() const noexcept { return _currentToken.token; }

    [[nodiscard]] std::string const& currentLiteral() const noexcept { return _currentToken.literal; }

    [[nodiscard]] SourceLocationRange currentRange() const noexcept { return _currentToken.location; }

    [[nodiscard]] bool isDirective(std::string_view name) const noexcept
    {
        return currentToken() == Token::Identifier && currentLiteral() == name;
    }

    static std::vector<TokenInfo> tokenize(std::unique_ptr<Source> source);

    /// Enter F# expression mode - operators become separate tokens
    void enterFSharpExpr() { ++_fsharpDepth; }

    /// Leave F# expression mode
    void leaveFSharpExpr()
    {
        if (_fsharpDepth > 0)
            --_fsharpDepth;
    }

    /// Check if currently in F# expression mode
    [[nodiscard]] bool inFSharpMode() const noexcept { return _fsharpDepth > 0; }

    /// Pushes back a token so that currentToken() returns it immediately,
    /// and the current token is deferred to the next nextToken() call.
    void pushBackToken(Token token, std::string literal)
    {
        _pushedBack = true;
        _pushedBackToken = _currentToken;
        _currentToken = TokenInfo { .token = token, .literal = std::move(literal) };
    }

    /// @brief Pushes back a token with its source location preserved.
    void pushBackToken(Token token, std::string literal, SourceLocationRange location)
    {
        _pushedBack = true;
        _pushedBackToken = _currentToken;
        _currentToken = TokenInfo { .token = token, .literal = std::move(literal), .location = location };
    }

  private:
    [[nodiscard]] bool eof() const noexcept { return _currentChar == char32_t(-1); }

    [[nodiscard]] char32_t currentChar() const noexcept { return _currentChar; }

    void consumeWhitespace();
    Token consumeNumber();
    Token consumeIdentifier();
    Token consumeIdentifier(Token token);
    Token consumeSingleQuotedString();
    Token consumeDoubleQuotedContent();
    Token consumeFStringContent();
    Token consumeBracedVariable();
    Token consumeTilde();
    char32_t nextChar();
    Token consumeCharAndConfirmToken(Token token);
    Token confirmToken(Token token);

    std::unique_ptr<Source> _source;
    char32_t _currentChar = 0;
    TokenInfo _currentToken = TokenInfo {};
    TokenInfo _nextToken = TokenInfo {};
    SourceLocationRange _currentRange {};
    bool _inDoubleQuote = false;   // State: inside double-quoted string
    int _dquoteSubstDepth = 0;     // Nesting depth for $() and backticks inside double quotes
    int _arithDepth = 0;           // Nesting depth for $(()), operators are reserved when > 0
    int _fsharpDepth = 0;          // Nesting depth for F# expressions, operators are tokens when > 0
    bool _inFString = false;       // State: inside an F#-style interpolated string
    int _fstringBraceDepth = 0;    // Brace nesting depth inside expression holes (0 = string content)
    std::string _fragmentBuffer;   // Buffer for accumulating string fragments
    bool _pushedBack = false;      // True if a token has been pushed back
    TokenInfo _pushedBackToken {}; // Token deferred for next nextToken() call
};

/// Converts a Token to its string representation.
inline std::string_view tos(Token token)
{
    using enum Token;
    switch (token)
    {
        case AmpNumber: return "AmpNumber";
        case Backslash: return "\\";
        case DollarDollar: return "$$";
        case DollarName: return "DollarName";
        case DollarNot: return "$!";
        case DollarQuestion: return "$?";
        case DollarNumber: return "DollarNumber";
        case DollarBraceName: return "DollarBraceName";
        case EndOfInput: return "EndOfInput";
        case Equal: return "=";
        case Greater: return ">";
        case GreaterEqual: return ">=";
        case GreaterGreater: return ">>";
        case GreaterAmp: return ">&";
        case Identifier: return "Identifier";
        case Invalid: return "Invalid";
        case Less: return "<";
        case LessEqual: return "<=";
        case LessLess: return "<<";
        case LessLessDash: return "<<-";
        case LessLessLess: return "<<<";
        case LessRndOpen: return "<(";
        case LineFeed: return "LineFeed";
        case Not: return "!";
        case Number: return "Number";
        case Pipe: return "|";
        case RndClose: return ")";
        case RndOpen: return "(";
        case Semicolon: return ";";
        case String: return "String";
        case AmpAmp: return "&&";
        case PipePipe: return "||";
        case DollarRndOpen: return "$(";
        case DollarDblRndOpen: return "$((";
        case DblRndClose: return "))";
        case DblRndOpen: return "((";
        case DblSemicolon: return ";;";
        case GreaterRndOpen: return ">(";
        case Backtick: return "`";
        case Tilde: return "~";
        case DollarBraceParam: return "DollarBraceParam";
        case Ampersand: return "&";
        case DblQuoteStart: return "DblQuoteStart";
        case DblQuoteEnd: return "DblQuoteEnd";
        case StringFragment: return "StringFragment";
        case FStringStart: return "FStringStart";
        case FStringEnd: return "FStringEnd";
        case FStringExprStart: return "FStringExprStart";
        case FStringExprEnd: return "FStringExprEnd";
        case Let: return "let";
        case Mut: return "mut";
        case Fun: return "fun";
        case Match: return "match";
        case With: return "with";
        case When: return "when";
        case Type: return "type";
        case Of: return "of";
        case Rec: return "rec";
        case And: return "and";
        case As: return "as";
        case True: return "true";
        case False: return "false";
        case OptionSome: return "Some";
        case OptionNone: return "None";
        case ResultOk: return "Ok";
        case ResultError: return "Error";
        case Try: return "try";
        case Finally: return "finally";
        case Arrow: return "->";
        case LeftArrow: return "<-";
        case ForwardPipe: return "|>";
        case DotDot: return "..";
        case EqualEqual: return "==";
        case NotEqual: return "!=";
        case Plus: return "+";
        case Minus: return "-";
        case Star: return "*";
        case Slash: return "/";
        case Percent: return "%";
        case StarStar: return "**";
        case Caret: return "^";
        case ColonColon: return "::";
        case At: return "@";
        case Comma: return ",";
        case Colon: return ":";
        case BracketOpen: return "[";
        case BracketClose: return "]";
        case BraceOpen: return "{";
        case BraceClose: return "}";
        case Question: return "?";
        case QuestionPipe: return "?|";
        case QuestionDot: return "?.";
        case Dot: return ".";
        case Ellipsis: return "...";
    }
    return "?";
}

} // namespace endo

template <>
struct std::formatter<endo::LineColumn>: std::formatter<std::string>
{
    auto format(const endo::LineColumn lineColumn, format_context& ctx) const -> format_context::iterator
    {
        return formatter<std::string>::format(std::format("{}:{}", lineColumn.line, lineColumn.column), ctx);
    }
};

template <>
struct std::formatter<endo::SourceLocation>: std::formatter<std::string>
{
    auto format(const endo::SourceLocation location, format_context& ctx) const -> format_context::iterator
    {
        return formatter<std::string>::format(
            std::format("{}:{}:{}", location.name, location.line, location.column), ctx);
    }
};

template <>
struct std::formatter<endo::SourceLocationRange>: std::formatter<std::string>
{
    auto format(const endo::SourceLocationRange range, format_context& ctx) const -> format_context::iterator
    {
        auto lc = [](endo::LineColumn const& v) {
            return std::to_string(v.line) + ":" + std::to_string(v.column);
        };
        return formatter<std::string>::format(
            std::string(range.name) + "(" + lc(range.begin) + " - " + lc(range.end) + ")", ctx);
    }
};

template <>
struct std::formatter<endo::Token>: std::formatter<std::string_view>
{
    auto format(const endo::Token token, format_context& ctx) const -> format_context::iterator
    {
        return formatter<std::string_view>::format(endo::tos(token), ctx);
    }
};

template <>
struct std::formatter<endo::TokenInfo>
{
    static constexpr auto parse(format_parse_context& ctx) -> format_parse_context::iterator
    {
        return ctx.begin();
    }

    auto format(endo::TokenInfo const& info, format_context& ctx) const -> format_context::iterator
    {
        auto lc = [](endo::LineColumn const& v) {
            return std::to_string(v.line) + ":" + std::to_string(v.column);
        };
        auto locStr = std::string(info.location.name) + "(" + lc(info.location.begin) + " - "
                      + lc(info.location.end) + ")";
        auto result = "(" + std::string(endo::tos(info.token)) + ", " + info.literal + ", " + locStr + ")";
        return std::format_to(ctx.out(), "{}", result);
    }
};
