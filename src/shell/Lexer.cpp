// SPDX-License-Identifier: Apache-2.0
#include "Lexer.hpp"

#include <libunicode/utf8.h>

using namespace std::string_view_literals;

namespace endo
{

Token Lexer::nextToken()
{
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
        case ';':
            nextChar();
            if (_currentChar == ';')
                return consumeCharAndConfirmToken(Token::DblSemicolon);
            return confirmToken(Token::Semicolon);
        case '=': return consumeCharAndConfirmToken(Token::Equal);
        case '|':
            nextChar();
            if (_currentChar == '|')
                return consumeCharAndConfirmToken(Token::PipePipe);
            return confirmToken(Token::Pipe);
        case '&':
            nextChar();
            if (_currentChar == '&')
                return consumeCharAndConfirmToken(Token::AmpAmp);
            return confirmToken(Token::Ampersand);
        case '>':
            nextChar();
            if (_currentChar == '>')
                return consumeCharAndConfirmToken(Token::GreaterGreater);
            else if (_currentChar == '=')
                return consumeCharAndConfirmToken(Token::GreaterEqual);
            else if (_currentChar == '&')
                return consumeCharAndConfirmToken(Token::GreaterAmp);
            else if (_currentChar == '(')
                return consumeCharAndConfirmToken(Token::GreaterRndOpen);
            else
                return confirmToken(Token::Greater);
        case '<':
            nextChar();
            if (_currentChar == '<')
            {
                nextChar();
                if (_currentChar == '<')
                    return consumeCharAndConfirmToken(Token::LessLessLess);
                else if (_currentChar == '-')
                    return consumeCharAndConfirmToken(Token::LessLessDash);
                else
                    return confirmToken(Token::LessLess);
            }
            else if (_currentChar == '=')
                return consumeCharAndConfirmToken(Token::LessEqual);
            else if (_currentChar == '(')
                return consumeCharAndConfirmToken(Token::LessRndOpen);
            else
                return confirmToken(Token::Less);
        case '(':
            nextChar();
            if (_currentChar == '(')
                return consumeCharAndConfirmToken(Token::DblRndOpen);
            return confirmToken(Token::RndOpen);
        case ')':
            nextChar();
            if (_currentChar == ')')
                return consumeCharAndConfirmToken(Token::DblRndClose);
            return confirmToken(Token::RndClose);
        case '\\': return consumeCharAndConfirmToken(Token::Backslash);
        case '!': return consumeCharAndConfirmToken(Token::Not);
        case '`': return consumeCharAndConfirmToken(Token::Backtick);
        case '~': return consumeTilde();
        case '$':
            nextChar();
            if (_currentChar == '(')
            {
                nextChar();
                if (_currentChar == '(')
                    return consumeCharAndConfirmToken(Token::DollarDblRndOpen);
                return confirmToken(Token::DollarRndOpen);
            }
            else if (_currentChar == '$')
                return consumeCharAndConfirmToken(Token::DollarDollar);
            else if (_currentChar == '!')
                return consumeCharAndConfirmToken(Token::DollarNot);
            else if (_currentChar == '?')
                return consumeCharAndConfirmToken(Token::DollarQuestion);
            else if (_currentChar == '{')
                return consumeBracedVariable();
            else if (_currentChar < 0x80 && std::isalpha(static_cast<char>(_currentChar)))
                return consumeIdentifier(Token::DollarName);
            else if (_currentChar == '_')
                return consumeIdentifier(Token::DollarName);
            else if (_currentChar < 0x80 && std::isdigit(static_cast<char>(_currentChar)))
            {
                _nextToken.literal += static_cast<char>(_currentChar);
                return consumeCharAndConfirmToken(Token::DollarNumber);
            }
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

void Lexer::consumeWhitespace()
{
    _nextToken.literal = {};

    while (_currentChar == U' ' || _currentChar == U'\t')
        nextChar();

    auto const [line, column, name] = _source->currentSourceLocation();
    _nextToken.location.name = name;
    _nextToken.location.begin = { .line = line, .column = column };
    _nextToken.location.end = _nextToken.location.begin;
}

Token Lexer::consumeNumber()
{
    while (_currentChar >= U'0' && _currentChar <= U'9')
    {
        _nextToken.literal += unicode::to_utf8(_currentChar);
        nextChar();
    }

    return confirmToken(Token::Number);
}

Token Lexer::consumeIdentifier()
{
    return consumeIdentifier(Token::Identifier);
}

Token Lexer::consumeIdentifier(Token token)
{
    // Note: {} are NOT reserved to allow brace expansion patterns like {a,b,c} to be lexed as single
    // tokens Note: [] are NOT reserved to allow glob bracket expressions like [abc] to be lexed as single
    // tokens
    auto constexpr ReservedSymbols = U"|<>()!$'\"\t\r\n ;`~"sv;

    while (!eof() && ReservedSymbols.find(_currentChar) == std::string_view::npos)
    {
        _nextToken.literal += unicode::to_utf8(_currentChar);
        nextChar();
    }
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
        _nextToken.literal += unicode::to_utf8(_currentChar);
        nextChar();
    }
    nextChar();
    return confirmToken(Token::String);
}

Token Lexer::consumeBracedVariable()
{
    nextChar(); // consume '{'

    // Handle ${#VAR} (length) - starts with #
    if (_currentChar == '#')
    {
        _nextToken.literal += '#';
        nextChar();
        // Fall through to consume the variable name and any trailing content
    }

    // Handle special variables within braces: ${?}, ${!}, etc.
    if (_currentChar == '?')
    {
        nextChar();
        if (_currentChar == '}')
        {
            nextChar();
            if (!_nextToken.literal.empty() && _nextToken.literal[0] == '#')
            {
                // ${#?} - length of $? (unusual but valid)
                return confirmToken(Token::DollarBraceParam);
            }
            return confirmToken(Token::DollarQuestion);
        }
        return confirmToken(Token::Invalid);
    }
    else if (_currentChar == '!')
    {
        nextChar();
        if (_currentChar == '}')
        {
            nextChar();
            if (!_nextToken.literal.empty() && _nextToken.literal[0] == '#')
            {
                // ${#!} - length of $!
                return confirmToken(Token::DollarBraceParam);
            }
            return confirmToken(Token::DollarNot);
        }
        return confirmToken(Token::Invalid);
    }
    else if (_currentChar == '$')
    {
        nextChar();
        if (_currentChar == '}')
        {
            nextChar();
            if (!_nextToken.literal.empty() && _nextToken.literal[0] == '#')
            {
                // ${#$} - length of $$
                return confirmToken(Token::DollarBraceParam);
            }
            return confirmToken(Token::DollarDollar);
        }
        return confirmToken(Token::Invalid);
    }
    else if (_currentChar < 0x80 && std::isdigit(static_cast<char>(_currentChar)))
    {
        _nextToken.literal += static_cast<char>(_currentChar);
        nextChar();
        if (_currentChar == '}')
        {
            nextChar();
            if (_nextToken.literal.size() > 1 && _nextToken.literal[0] == '#')
            {
                // ${#0} - length of $0
                return confirmToken(Token::DollarBraceParam);
            }
            return confirmToken(Token::DollarNumber);
        }
        // Fall through - might have more content like ${1:-default}
    }

    // Consume the variable name if we haven't started yet
    if (_nextToken.literal.empty() || (_nextToken.literal.size() == 1 && _nextToken.literal[0] == '#'))
    {
        // Regular variable name: must start with alpha or underscore
        if (!(_currentChar < 0x80 && (std::isalpha(static_cast<char>(_currentChar)) || _currentChar == '_')))
        {
            if (_nextToken.literal.empty())
                return confirmToken(Token::Invalid);
            // Had # but no valid variable name - still invalid
            return confirmToken(Token::Invalid);
        }

        // Consume the variable name
        while (_currentChar < 0x80 && (std::isalnum(static_cast<char>(_currentChar)) || _currentChar == '_'))
        {
            _nextToken.literal += unicode::to_utf8(_currentChar);
            nextChar();
        }
    }

    // Check for closing brace or parameter expansion operator
    if (_currentChar == '}')
    {
        nextChar(); // consume '}'
        // Check if we have a # prefix (length operator)
        if (!_nextToken.literal.empty() && _nextToken.literal[0] == '#')
            return confirmToken(Token::DollarBraceParam);
        return confirmToken(Token::DollarBraceName);
    }

    // Parameter expansion operator detected - consume everything until closing brace
    // Operators: :-, :+, :=, :?, #, ##, %, %%, /, //
    // We store the full content in literal for the parser to handle
    int braceDepth = 1;
    while (!eof() && braceDepth > 0)
    {
        if (_currentChar == '{')
            ++braceDepth;
        else if (_currentChar == '}')
            --braceDepth;

        if (braceDepth > 0)
        {
            _nextToken.literal += unicode::to_utf8(_currentChar);
            nextChar();
        }
    }

    if (braceDepth != 0)
        return confirmToken(Token::Invalid);

    nextChar(); // consume final '}'
    return confirmToken(Token::DollarBraceParam);
}

Token Lexer::consumeTilde()
{
    nextChar(); // consume '~'

    // Check if followed by a path separator or whitespace (standalone ~)
    // or followed by alphanumeric/underscore (username)
    if (_currentChar == ' ' || _currentChar == '\t' || _currentChar == '\n' || _currentChar == '\r' || eof()
        || _currentChar == ';' || _currentChar == '|' || _currentChar == '&' || _currentChar == '>'
        || _currentChar == '<' || _currentChar == ')' || _currentChar == '"' || _currentChar == '\'')
    {
        // Standalone ~ - no username
        return confirmToken(Token::Tilde);
    }

    // If it starts with /, this is ~/path - return as identifier to be handled specially
    // The tilde is recorded but the rest becomes part of the identifier
    if (_currentChar == '/')
    {
        // Put a marker in the literal to indicate this was a tilde expansion
        // The entire ~/path will be treated as one token
        _nextToken.literal = "~";
        while (!eof() && _currentChar != ' ' && _currentChar != '\t' && _currentChar != '\n'
               && _currentChar != '\r' && _currentChar != ';' && _currentChar != '|' && _currentChar != '&'
               && _currentChar != '>' && _currentChar != '<' && _currentChar != ')' && _currentChar != '"'
               && _currentChar != '\'')
        {
            _nextToken.literal += unicode::to_utf8(_currentChar);
            nextChar();
        }
        return confirmToken(Token::Tilde);
    }

    // Consume username: alphanumeric, underscore, dash
    while (_currentChar < 0x80
           && (std::isalnum(static_cast<char>(_currentChar)) || _currentChar == '_' || _currentChar == '-'))
    {
        _nextToken.literal += unicode::to_utf8(_currentChar);
        nextChar();
    }

    // If followed by /, consume the path too
    if (_currentChar == '/')
    {
        std::string const username = _nextToken.literal;
        _nextToken.literal.clear();
        _nextToken.literal = username;
        while (!eof() && _currentChar != ' ' && _currentChar != '\t' && _currentChar != '\n'
               && _currentChar != '\r' && _currentChar != ';' && _currentChar != '|' && _currentChar != '&'
               && _currentChar != '>' && _currentChar != '<' && _currentChar != ')' && _currentChar != '"'
               && _currentChar != '\'')
        {
            _nextToken.literal += unicode::to_utf8(_currentChar);
            nextChar();
        }
    }

    return confirmToken(Token::Tilde);
}

char32_t Lexer::nextChar()
{
    _currentChar = _source->readChar();
    return _currentChar;
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

} // namespace endo
