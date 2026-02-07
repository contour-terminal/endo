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
    // Note: 'in' is NOT a keyword token - it's recognized contextually by the parser

    // F# style constructors
    OptionSome, // 'Some' (Option constructor)
    OptionNone, // 'None' (Option constructor)
    ResultOk,   // 'Ok' (Result constructor)
    // Note: Error already exists contextually (ResultError)

    // F# style operators
    Arrow,       // '->'
    LeftArrow,   // '<-' (mutation)
    ForwardPipe, // '|>'
    // Note: >> uses GreaterGreater token (context determines compose vs redirect)
    // Note: << uses LessLess token (context determines back-compose vs here-doc)
    // Note: :: : .. [ ] , ? are lexed as part of identifiers to preserve shell compatibility.
    //       The parser handles these in F# expression context.
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
    int line;   // 0-based index
    int column; // 0-based index
};

struct SourceLocation
{
    int line;              // 0-based index
    int column;            // 0-based index
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
            return static_cast<char32_t>(-1);

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

    [[nodiscard]] SourceLocationRange currentRange() const noexcept { return _currentRange; }

    [[nodiscard]] bool isDirective(std::string_view name) const noexcept
    {
        return currentToken() == Token::Identifier && currentLiteral() == name;
    }

    static std::vector<TokenInfo> tokenize(std::unique_ptr<Source> source);

  private:
    [[nodiscard]] bool eof() const noexcept { return _currentChar == char32_t(-1); }

    [[nodiscard]] char32_t currentChar() const noexcept { return _currentChar; }

    void consumeWhitespace();
    Token consumeNumber();
    Token consumeIdentifier();
    Token consumeIdentifier(Token token);
    Token consumeSingleQuotedString();
    Token consumeDoubleQuotedContent();
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
    bool _inDoubleQuote = false; // State: inside double-quoted string
    int _dquoteSubstDepth = 0;   // Nesting depth for $() and backticks inside double quotes
    int _arithDepth = 0;         // Nesting depth for $(()), operators are reserved when > 0
    std::string _fragmentBuffer; // Buffer for accumulating string fragments
};

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
        return formatter<std::string>::format(std::format("{}({} - {})", range.name, range.begin, range.end),
                                              ctx);
    }
};

template <>
struct std::formatter<endo::Token>: std::formatter<std::string_view>
{
    auto format(const endo::Token token, format_context& ctx) const -> format_context::iterator
    {
        std::string_view name;
        using enum endo::Token;
        switch (token)
        {
            case AmpNumber: name = "AmpNumber"; break;
            case Backslash: name = "\\"; break;
            case DollarDollar: name = "$$"; break;
            case DollarName: name = "DollarName"; break;
            case DollarNot: name = "$!"; break;
            case DollarQuestion: name = "$?"; break;
            case DollarNumber: name = "DollarNumber"; break;
            case DollarBraceName: name = "DollarBraceName"; break;
            case EndOfInput: name = "EndOfInput"; break;
            case Equal: name = "="; break;
            case Greater: name = ">"; break;
            case GreaterEqual: name = ">="; break;
            case GreaterGreater: name = ">>"; break;
            case GreaterAmp: name = ">&"; break;
            case Identifier: name = "Identifier"; break;
            case Invalid: name = "Invalid"; break;
            case Less: name = "<"; break;
            case LessEqual: name = "<="; break;
            case LessLess: name = "<<"; break;
            case LessLessDash: name = "<<-"; break;
            case LessLessLess: name = "<<<"; break;
            case LessRndOpen: name = "<("; break;
            case LineFeed: name = "LineFeed"; break;
            case Not: name = "!"; break;
            case Number: name = "Number"; break;
            case Pipe: name = "|"; break;
            case RndClose: name = ")"; break;
            case RndOpen: name = "("; break;
            case Semicolon: name = ";"; break;
            case String: name = "String"; break;
            case AmpAmp: name = "&&"; break;
            case PipePipe: name = "||"; break;
            case DollarRndOpen: name = "$("; break;
            case DollarDblRndOpen: name = "$(("; break;
            case DblRndClose: name = "))"; break;
            case DblRndOpen: name = "(("; break;
            case DblSemicolon: name = ";;"; break;
            case GreaterRndOpen: name = ">("; break;
            case Backtick: name = "`"; break;
            case Tilde: name = "~"; break;
            case DollarBraceParam: name = "DollarBraceParam"; break;
            case Ampersand: name = "&"; break;
            case DblQuoteStart: name = "DblQuoteStart"; break;
            case DblQuoteEnd: name = "DblQuoteEnd"; break;
            case StringFragment: name = "StringFragment"; break;
            // F# style keywords
            case Let: name = "let"; break;
            case Mut: name = "mut"; break;
            case Fun: name = "fun"; break;
            case Match: name = "match"; break;
            case With: name = "with"; break;
            case When: name = "when"; break;
            case Type: name = "type"; break;
            case Of: name = "of"; break;
            case Rec: name = "rec"; break;
            case And: name = "and"; break;
            case As: name = "as"; break;
            // F# style constructors
            case OptionSome: name = "Some"; break;
            case OptionNone: name = "None"; break;
            case ResultOk: name = "Ok"; break;
            // F# style operators
            case Arrow: name = "->"; break;
            case LeftArrow: name = "<-"; break;
            case ForwardPipe:
                name = "|>";
                break;
                // Note: >> and << use GreaterGreater/LessLess tokens
                // Note: :: : .. are lexed as part of identifiers
        }
        return formatter<std::string_view>::format(name, ctx);
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
        return std::format_to(ctx.out(), "({}, {}, {})", info.token, info.literal, info.location);
    }
};
