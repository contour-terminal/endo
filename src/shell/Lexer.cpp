// SPDX-License-Identifier: Apache-2.0
#include <shell/Lexer.h>

#include <crispy/assert.h>

#include <cstring>
#include <string_view>

// clang-format off
#if 0 // defined(TRACE_LEXER)
    #define TRACE(message, ...) do { ::fmt::print("Lexer: " message, __VA_ARGS__); } while (0)
#else
    #define TRACE(message, ...) do {} while (0)
#endif
// clang-format on

using namespace std::string_view_literals;

namespace crush
{

// {{{ StringSource
StringSource::StringSource(std::string source): _source { std::move(source) }
{
}

void StringSource::rewind()
{
    _location.line = 0;
    _location.column = 0;
}

[[nodiscard]] char32_t StringSource::readChar()
{
    if (_offset >= _source.size())
        return -1;

    auto const ch = _source[_offset];

    ++_offset;
    ++_location.column;
    if (ch == '\n')
    {
        ++_location.line;
        _location.column = 0;
    }

    return ch;
}

[[nodiscard]] std::string_view StringSource::readGraphemeCluster()
{
    if (_offset >= _source.size())
        return {};

    return std::string_view(_source.data() + _offset, 1);
}

[[nodiscard]] SourceLocation StringSource::currentSourceLocation() const noexcept
{
    return _location;
}
// }}}

// {{{ Lexer
Lexer::Lexer(std::unique_ptr<Source> source): _source { std::move(source) }
{
    nextChar();
    nextToken();
}

char32_t Lexer::nextChar()
{
    _currentChar = _source->readChar();
    // TRACE("Lexer.nextChar: '{}'\n", (char) _currentChar);
    return _currentChar;
}

Token Lexer::nextToken()
{
    // auto const postLogger = crispy::finally { [this]() mutable {
    //     TRACE("Lexer.nextToken ~> {}\n", _currentToken);
    // } };

    consumeWhitespace();
    switch (_currentChar)
    {
        case (char32_t) -1: return confirmToken(Token::EndOfInput);
        case '\r':
            nextChar();
            if (_currentChar == '\n')
                return consumeCharAndConfirmToken(Token::LineFeed);
            return confirmToken(Token::Invalid);
        case '\n': return consumeCharAndConfirmToken(Token::LineFeed);
        case ';': return consumeCharAndConfirmToken(Token::Semicolon);
        case '=': return consumeCharAndConfirmToken(Token::Equal);
        case '|': return consumeCharAndConfirmToken(Token::Pipe);
        case '>':
            nextChar();
            if (_currentChar == '>')
                return consumeCharAndConfirmToken(Token::GreaterGreater);
            else if (_currentChar == '=')
                return consumeCharAndConfirmToken(Token::GreaterEqual);
            else
                return consumeCharAndConfirmToken(Token::Greater);
        case '<':
            nextChar();
            if (_currentChar == '<')
                return consumeCharAndConfirmToken(Token::LessLess);
            else if (_currentChar == '=')
                return consumeCharAndConfirmToken(Token::LessEqual);
            else if (_currentChar == '(')
                return consumeCharAndConfirmToken(Token::LessRndOpen);
            else
                return confirmToken(Token::Less);
        case '(': return consumeCharAndConfirmToken(Token::RndOpen);
        case ')': return consumeCharAndConfirmToken(Token::RndClose);
        case '\\': return consumeCharAndConfirmToken(Token::Backslash);
        case '!': return consumeCharAndConfirmToken(Token::Not);
        case '$':
            nextChar();
            if (_currentChar == '$')
                return consumeCharAndConfirmToken(Token::DollarDollar);
            else if (_currentChar == '!')
                return consumeCharAndConfirmToken(Token::DollarNot);
            else if (_currentChar == '?')
                return consumeCharAndConfirmToken(Token::DollarQuestion);
            else if (_currentChar < 0x80 && std::isalpha(static_cast<char>(_currentChar)))
                return consumeIdentifier(Token::DollarName);
            else if (_currentChar < 0x80 && std::isdigit(static_cast<char>(_currentChar)))
                return consumeCharAndConfirmToken(Token::DollarNumber);
            else
                return confirmToken(Token::Invalid);
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9': return consumeNumber();
        case '"':
        case '\'': return consumeString();
        default: return consumeIdentifier();
    }
    return confirmToken(Token::Invalid);
    // crispy::unreachable();
}

Token Lexer::consumeIdentifier()
{
    return consumeIdentifier(Token::Identifier);
}

Token Lexer::consumeIdentifier(Token token)
{
    auto constexpr ReservedSymbols = U"|<>()[]{}!$'\"\t\r\n ;"sv;

    while (!eof() && ReservedSymbols.find(_currentChar) == std::string_view::npos)
    {
        _nextToken.literal += static_cast<char>(_currentChar); // TODO: UTF-8
        nextChar();
        // TRACE("consumeIdentifier: '{}'\n", (char) _currentChar);
    }
    TRACE("consumeIdentifier: result: '{}'\n", _nextToken.literal);
    return confirmToken(token);
}

Token Lexer::consumeString()
{
    auto const quote = _currentChar;
    nextChar();
    while (_currentChar != quote)
    {
        if (_currentChar == '\\')
            nextChar();
        _nextToken.literal += static_cast<char>(_currentChar); // TODO: UTF-8
        nextChar();
    }
    nextChar();
    return confirmToken(Token::String);
}

Token Lexer::consumeNumber()
{
    while (strchr("0123456789", (int) _currentChar))
    {
        _nextToken.literal += static_cast<char>(_currentChar); // TODO: UTF-8
        nextChar();
    }

    return confirmToken(Token::Number);
}

void Lexer::consumeWhitespace()
{
    _nextToken.literal = {};

    while (strchr(" \t", (int) _currentChar))
        nextChar();

    auto const [line, column, name] = _source->currentSourceLocation();
    _nextToken.location.name = name;
    _nextToken.location.begin = { .line = line, .column = column };
    _nextToken.location.end = _nextToken.location.begin;
}

Token Lexer::consumeCharAndConfirmToken(Token token)
{
    nextChar();
    return confirmToken(token);
}

Token Lexer::confirmToken(Token token)
{
    _nextToken.token = token;
    _nextToken.literal = _nextToken.literal;
    auto const [a, b, _] = _source->currentSourceLocation();
    _nextToken.location.end = { .line = a, .column = b };
    _currentToken = _nextToken;

    _nextToken.literal = {};
    _nextToken.location.name = _source->currentSourceLocation().name;
    _nextToken.location.begin = _nextToken.location.end;

    return token;
}

std::vector<TokenInfo> Lexer::tokenize(std::unique_ptr<Source> source)
{
    auto tokens = std::vector<TokenInfo> {};
    auto lexer = Lexer { std::move(source) };

    while (lexer.currentToken() != Token::EndOfInput)
    {
        tokens.emplace_back(TokenInfo { lexer.currentToken(), lexer.currentLiteral(), lexer.currentRange() });
        lexer.nextToken();
    }

    return tokens;
}
// }}}

} // namespace crush
