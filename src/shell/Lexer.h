// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <fmt/format.h>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace crush
{

enum class Token
{
    Invalid,

    AmpNumber,      // '&' DIGIT+
    Backslash,      // '\'
    DollarDollar,   // $$
    DollarName,     // $NAME
    DollarNot,      // $!
    DollarNumber,   // '$' DIGIT+
    EndOfInput,     // EOF
    EndOfLine,      // LF
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
    LineFeed,       // LF
    String,         // "..." and '...'
    Identifier,     // space delimited text not containing any of the unescaped symbols above
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
    [[nodiscard]] virtual std::string_view readGraphemeCluster() = 0;
    [[nodiscard]] virtual SourceLocation currentSourceLocation() const noexcept = 0;
};

class StringSource final: public Source
{
  public:
    explicit StringSource(std::string source);
    void rewind() override;
    [[nodiscard]] char32_t readChar() override;
    [[nodiscard]] std::string_view readGraphemeCluster() override;
    [[nodiscard]] SourceLocation currentSourceLocation() const noexcept override;

  private:
    SourceLocation _location;
    std::string _source;
    size_t _offset = 0;
};

class Lexer
{
  public:
    explicit Lexer(std::unique_ptr<Source> source);
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
    Token consumeString();
    char32_t nextChar();
    Token consumeCharAndConfirmToken(Token token);
    Token confirmToken(Token token);

    std::unique_ptr<Source> _source;
    char32_t _currentChar = 0;
    TokenInfo _currentToken = TokenInfo {};
    TokenInfo _nextToken = TokenInfo {};
    SourceLocationRange _currentRange {};
};

} // namespace crush

template <>
struct fmt::formatter<crush::LineColumn>: fmt::formatter<std::string>
{
    auto format(const crush::LineColumn lineColumn, format_context& ctx) -> format_context::iterator
    {
        return formatter<std::string>::format(fmt::format("{}:{}", lineColumn.line, lineColumn.column), ctx);
    }
};

template <>
struct fmt::formatter<crush::SourceLocation>: fmt::formatter<std::string>
{
    auto format(const crush::SourceLocation location, format_context& ctx) -> format_context::iterator
    {
        return formatter<std::string>::format(
            fmt::format("{}:{}:{}", location.name, location.line, location.column), ctx);
    }
};

template <>
struct fmt::formatter<crush::SourceLocationRange>: fmt::formatter<std::string>
{
    auto format(const crush::SourceLocationRange range, format_context& ctx) -> format_context::iterator
    {
        return formatter<std::string>::format(fmt::format("{}({} - {})", range.name, range.begin, range.end),
                                              ctx);
    }
};

template <>
struct fmt::formatter<crush::Token>: fmt::formatter<std::string_view>
{
    auto format(const crush::Token token, format_context& ctx) -> format_context::iterator
    {
        string_view name;
        using enum crush::Token;
        switch (token)
        {
            case AmpNumber: name = "AmpNumber"; break;
            case Backslash: name = "\\"; break;
            case DollarDollar: name = "$$"; break;
            case DollarName: name = "DollarName"; break;
            case DollarNot: name = "$!"; break;
            case DollarNumber: name = "DollarNumber"; break;
            case EndOfInput: name = "EndOfInput"; break;
            case EndOfLine: name = "EndOfLine"; break;
            case Equal: name = "="; break;
            case Greater: name = ">"; break;
            case GreaterEqual: name = ">="; break;
            case GreaterGreater: name = ">>"; break;
            case Identifier: name = "Identifier"; break;
            case Invalid: name = "Invalid"; break;
            case Less: name = "<"; break;
            case LessEqual: name = "<="; break;
            case LessLess: name = "<<"; break;
            case LessRndOpen: name = "<("; break;
            case LineFeed: name = "LineFeed"; break;
            case Not: name = "!"; break;
            case Number: name = "Number"; break;
            case Pipe: name = "|"; break;
            case RndClose: name = ")"; break;
            case RndOpen: name = "("; break;
            case Semicolon: name = ";"; break;
            case String: name = "String"; break;
        }
        return formatter<string_view>::format(name, ctx);
    }
};

template <>
struct fmt::formatter<crush::TokenInfo>
{
    static auto parse(format_parse_context& ctx) -> format_parse_context::iterator { return ctx.begin(); }
    static auto format(crush::TokenInfo const& info, format_context& ctx) -> format_context::iterator
    {
        return fmt::format_to(ctx.out(), "({}, {}, {})", info.token, info.literal, info.location);
    }
};
