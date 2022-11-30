#include "Lexer.h"
#include <cstring>

namespace contrair
{

void StdinSource::rewind()
{
    _location.column = 0;
}

[[nodiscard]] char32_t StdinSource::readChar()
{
    _location.column++;
    return static_cast<char32_t>(std::getchar());
}

[[nodiscard]] std::u32string StdinSource::readGraphemeCluster()
{
    if (auto const ch = readChar())
        return std::u32string(1, ch);
    return {};
}

[[nodiscard]] SourceLocation StdinSource::currentSourceLocation() const noexcept
{
    return _location;
}

// {{{ Lexer
Lexer::Lexer(std::unique_ptr<Source> source): _source { std::move(source) }
{
}

Token Lexer::nextToken()
{
    consumeWhitespace();
    switch (_currentChar)
    {
        case ';': return confirmToken(Token::Semicolon);
        case '=': return confirmToken(Token::Equal);
        case '|': return confirmToken(Token::Pipe);
        case '>':
            nextChar();
            if (_currentChar == '>')
                return confirmToken(Token::GreaterGreater);
            else if (_currentChar == '=')
                return confirmToken(Token::GreaterEqual);
            else
                return confirmToken(Token::Greater);
        case '<':
            nextChar();
            if (_currentChar == '<')
                return confirmToken(Token::LessLess);
            else if (_currentChar == '=')
                return confirmToken(Token::LessEqual);
            else if (_currentChar == '(')
                return confirmToken(Token::LessRndOpen);
            else
                return confirmToken(Token::Less);
        case '(':
            return confirmToken(Token::RndOpen);
        case ')':
            return confirmToken(Token::RndClose);
        case '\\':
            return confirmToken(Token::Backslash);
        case '!':
            return confirmToken(Token::Not);
        case '$':
            nextChar();
            if (_currentChar == '$')
                return confirmToken(Token::DollarDollar);
            else if (_currentChar == '!')
                return confirmToken(Token::DollarNot);
            else
                return confirmToken(Token::Invalid);
        case '0' ... '9': // TODO
        case '"': // TODO
        case '\'': // TODO
        default:
            consumeWord();
    }
    return Token::Invalid;
}

void Lexer::consumeWord()
{
    while (strchr(";=|<>$'\"! \t\r\n", (int)_currentChar))
        nextChar();

    confirmToken(Token::Word);
}

void Lexer::consumeWhitespace()
{
    while (strchr(" \t\r\n", (int)_currentChar))
        nextChar();
}

char32_t Lexer::nextChar()
{
    _currentChar = _source->readChar();
    if (_currentChar)
        _nextToken.literal.push_back(_currentChar);
    return _currentChar;
}

Token Lexer::confirmToken(Token token)
{
    _currentToken.token = token;
    _currentToken.literal = _nextToken.literal;
    _nextToken.literal.clear();
    nextChar();
    return token;
}

// }}}

}
