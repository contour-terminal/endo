// SPDX-License-Identifier: Apache-2.0
#include <endo-language/lexer/Lexer.hpp>

#include <algorithm>

#include <libunicode/utf8.h>

using namespace std::string_view_literals;

namespace endo
{

namespace
{
    // Maps F# keywords to their token types
    // Returns Token::Identifier if not a keyword
    Token keywordToToken(std::string_view literal)
    {
        // F# keywords
        if (literal == "let")
            return Token::Let;
        if (literal == "mut")
            return Token::Mut;
        if (literal == "fun")
            return Token::Fun;
        if (literal == "match")
            return Token::Match;
        if (literal == "with")
            return Token::With;
        if (literal == "when")
            return Token::When;
        if (literal == "type")
            return Token::Type;
        if (literal == "of")
            return Token::Of;
        if (literal == "rec")
            return Token::Rec;
        if (literal == "and")
            return Token::And;
        if (literal == "as")
            return Token::As;
        // Note: 'in' is NOT recognized as a keyword here because it's used in bash
        // for-loops (for x in list) and case statements (case x in). The parser
        // will detect F# style 'in' (let x = 1 in expr) by context.

        // F# constructors
        if (literal == "Some")
            return Token::OptionSome;
        if (literal == "None")
            return Token::OptionNone;
        if (literal == "Ok")
            return Token::ResultOk;
        if (literal == "Error")
            return Token::ResultError;

        // F# error handling keywords
        if (literal == "try")
            return Token::Try;
        if (literal == "finally")
            return Token::Finally;

        // Lazy evaluation
        if (literal == "lazy")
            return Token::Lazy;

        // Lazy sequences
        if (literal == "seq")
            return Token::Seq;
        if (literal == "yield")
            return Token::Yield;

        // Resource management
        if (literal == "use")
            return Token::Use;
        if (literal == "manual")
            return Token::Manual;

        // Boolean literals
        if (literal == "true")
            return Token::True;
        if (literal == "false")
            return Token::False;

        return Token::Identifier;
    }
} // namespace

Token Lexer::nextToken()
{
    // If tokens were pushed back, return the most recently pushed one
    if (!_pushedBackTokens.empty())
    {
        _currentToken = std::move(_pushedBackTokens.back());
        _pushedBackTokens.pop_back();
        _atStatementStart =
            (_currentToken.token == Token::Semicolon || _currentToken.token == Token::LineFeed);
        return _currentToken.token;
    }

    // If we're inside a double-quoted string (and not inside a substitution), use the special tokenizer
    if (_inDoubleQuote && _dquoteSubstDepth == 0)
        return consumeDoubleQuotedContent();

    // If we're inside an F#-style interpolated string and not inside an expression hole, scan string content
    if (_inFString && _fstringBraceDepth == 0)
        return consumeFStringContent();

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
        case '=':
            nextChar();
            if (_currentChar == '=')
                return consumeCharAndConfirmToken(Token::EqualEqual);
            return confirmToken(Token::Equal);
        case '|':
            nextChar();
            if (_currentChar == '|')
                return consumeCharAndConfirmToken(Token::PipePipe);
            if (_currentChar == '>')
                return consumeCharAndConfirmToken(Token::ForwardPipe);
            return confirmToken(Token::Pipe);
        case '&':
            nextChar();
            if (_currentChar == '&')
                return consumeCharAndConfirmToken(Token::AmpAmp);
            return confirmToken(Token::Ampersand);
        case '>':
            nextChar();
            if (_currentChar == '>')
                return consumeCharAndConfirmToken(Token::GreaterGreater); // >> (redirect append or compose)
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
                    return confirmToken(Token::LessLess); // << (here-doc or back-compose)
            }
            else if (_currentChar == '-')
                return consumeCharAndConfirmToken(Token::LeftArrow); // <- (mutation)
            else if (_currentChar == '=')
                return consumeCharAndConfirmToken(Token::LessEqual);
            else if (_currentChar == '>' && _fsharpDepth > 0)
                return consumeCharAndConfirmToken(Token::NotEqual); // F# <> operator
            else if (_currentChar == '(')
                return consumeCharAndConfirmToken(Token::LessRndOpen);
            else
                return confirmToken(Token::Less);
        case '(':
            nextChar();
            // Only merge '((' into DblRndOpen for shell arithmetic contexts, not F# mode.
            if (_currentChar == '(' && _fsharpDepth == 0)
            {
                ++_arithDepth; // Entering (( arithmetic context
                return consumeCharAndConfirmToken(Token::DblRndOpen);
            }
            return confirmToken(Token::RndOpen);
        case ')':
            nextChar();
            // In F# mode without arithmetic context, each ')' is a separate RndClose token.
            // Only merge '))' into DblRndClose for shell arithmetic or $((…)) contexts.
            if (_currentChar == ')' && (_arithDepth > 0 || (_inDoubleQuote && _dquoteSubstDepth > 0)))
            {
                if (_inDoubleQuote && _dquoteSubstDepth > 0)
                    --_dquoteSubstDepth; // Closing $((
                if (_arithDepth > 0)
                    --_arithDepth; // Leaving arithmetic context
                return consumeCharAndConfirmToken(Token::DblRndClose);
            }
            if (_inDoubleQuote && _dquoteSubstDepth > 0)
                --_dquoteSubstDepth; // Closing $(
            return confirmToken(Token::RndClose);
        case '\\': return consumeCharAndConfirmToken(Token::Backslash);
        case '!':
            nextChar();
            if (_currentChar == '=')
                return consumeCharAndConfirmToken(Token::NotEqual);
            return confirmToken(Token::Not);
        case '`':
            if (_inDoubleQuote && _dquoteSubstDepth > 0)
                --_dquoteSubstDepth; // Closing backtick
            return consumeCharAndConfirmToken(Token::Backtick);
        case '~': return consumeTilde();
        // Note: ? , : are NOT tokenized here to preserve shell compatibility.
        // - ? is used in glob patterns like file?.txt
        // - , is used in brace expansion like {a,b,c}
        // - : is used in PATH-like values like /usr/bin:/usr/local/bin
        // The parser handles these in F# expression context.
        case '.':
            nextChar();
            if (_currentChar == '.')
            {
                nextChar();
                if (_currentChar == '.')
                    return consumeCharAndConfirmToken(Token::Ellipsis); // ... (variadic/splat)
                // .. (range operator) — only in F# mode
                if (_fsharpDepth > 0)
                    return confirmToken(Token::DotDot);
                // Shell mode: treat as identifier (e.g., cd .., ls ../foo)
                _nextToken.literal = "..";
                return consumeIdentifier();
            }
            // In F# mode, single dot is a field access operator
            if (_fsharpDepth > 0)
                return confirmToken(Token::Dot);
            // Single dot - treat as identifier (e.g., ./script, file.txt)
            _nextToken.literal = ".";
            return consumeIdentifier();
        case '-':
            nextChar();
            if (_currentChar == '>')
                return consumeCharAndConfirmToken(Token::Arrow); // -> (lambda arrow, match arm)
            // Check for negative number: - followed by digit
            if (_currentChar >= U'0' && _currentChar <= U'9')
            {
                _nextToken.literal = "-";
                return consumeNumber(); // Will append digits to the "-" prefix
            }
            // In F# mode, - not followed by digit is Minus token
            if (_fsharpDepth > 0)
                return confirmToken(Token::Minus);
            // Not an arrow or number, treat - and what follows as identifier (e.g., -l, --help, file-name)
            _nextToken.literal = "-";
            return consumeIdentifier();
        case '$':
            nextChar();
            if (_currentChar == '"' && _fsharpDepth > 0)
            {
                // F#-style interpolated string: $"..."
                nextChar(); // consume opening "
                _inFString = true;
                _fstringBraceDepth = 0;
                _fragmentBuffer.clear();
                return confirmToken(Token::FStringStart);
            }
            if (_currentChar == '(')
            {
                nextChar();
                if (_currentChar == '(')
                {
                    ++_arithDepth; // Entering $(( arithmetic context
                    return consumeCharAndConfirmToken(Token::DollarDblRndOpen);
                }
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
            // Start of double-quoted string with interpolation support
            _inDoubleQuote = true;
            _fragmentBuffer.clear();
            return consumeCharAndConfirmToken(Token::DblQuoteStart);
        case '\'': return consumeSingleQuotedString();
        // F# mode operators - only tokenized separately when in F# expression context
        case '+':
            if (_fsharpDepth > 0)
                return consumeCharAndConfirmToken(Token::Plus);
            return consumeIdentifier();
        case '*':
            if (_fsharpDepth > 0)
            {
                nextChar();
                if (_currentChar == '*')
                    return consumeCharAndConfirmToken(Token::StarStar);
                return confirmToken(Token::Star);
            }
            return consumeIdentifier();
        case '/':
            if (_fsharpDepth > 0)
                return consumeCharAndConfirmToken(Token::Slash);
            return consumeIdentifier();
        case '%':
            if (_fsharpDepth > 0)
                return consumeCharAndConfirmToken(Token::Percent);
            return consumeIdentifier();
        case '^':
            if (_fsharpDepth > 0)
                return consumeCharAndConfirmToken(Token::Caret);
            return consumeIdentifier();
        case ':':
            if (_fsharpDepth > 0)
            {
                nextChar();
                if (_currentChar == ':')
                    return consumeCharAndConfirmToken(Token::ColonColon);
                return confirmToken(Token::Colon);
            }
            return consumeIdentifier();
        case '@':
            if (_fsharpDepth > 0)
                return consumeCharAndConfirmToken(Token::At);
            return consumeIdentifier();
        case ',':
            if (_fsharpDepth > 0)
                return consumeCharAndConfirmToken(Token::Comma);
            return consumeIdentifier();
        case '[':
            if (_fsharpDepth > 0 || _atStatementStart)
                return consumeCharAndConfirmToken(Token::BracketOpen);
            return consumeIdentifier();
        case ']':
            if (_fsharpDepth > 0)
                return consumeCharAndConfirmToken(Token::BracketClose);
            return consumeIdentifier();
        case '{':
            if (_inFString && _fstringBraceDepth > 0)
            {
                ++_fstringBraceDepth;
                return consumeCharAndConfirmToken(Token::BraceOpen);
            }
            if (_fsharpDepth > 0)
                return consumeCharAndConfirmToken(Token::BraceOpen);
            return consumeIdentifier();
        case '}':
            if (_inFString && _fstringBraceDepth > 0)
            {
                --_fstringBraceDepth;
                if (_fstringBraceDepth == 0)
                    return consumeCharAndConfirmToken(Token::FStringExprEnd);
                return consumeCharAndConfirmToken(Token::BraceClose);
            }
            if (_fsharpDepth > 0)
                return consumeCharAndConfirmToken(Token::BraceClose);
            return consumeIdentifier();
        case '?':
            if (_fsharpDepth > 0)
            {
                nextChar();
                if (_currentChar == '.')
                    return consumeCharAndConfirmToken(Token::QuestionDot);
                if (_currentChar == '|')
                    return consumeCharAndConfirmToken(Token::QuestionPipe);
                return confirmToken(Token::Question);
            }
            return consumeIdentifier();
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
    _precedingSpace = false;

    for (;;)
    {
        // Consume spaces and tabs
        if (_currentChar == U' ' || _currentChar == U'\t')
        {
            _precedingSpace = true;
            while (_currentChar == U' ' || _currentChar == U'\t')
                nextChar();
        }

        // # line comment (shell style)
        if (_currentChar == U'#')
        {
            _precedingSpace = true;
            if (_collectComments)
            {
                auto const startLoc = _source->currentSourceLocation();
                std::string text;
                text += '#';
                nextChar();
                while (!eof() && _currentChar != U'\n' && _currentChar != U'\r')
                {
                    text += unicode::to_utf8(_currentChar);
                    nextChar();
                }
                auto const endLoc = _source->currentSourceLocation();
                _comments.push_back(CommentTrivia {
                    .style = CommentStyle::Shell,
                    .location = { .begin = { startLoc.line, std::max(0, startLoc.column - 1) },
                                  .end = { endLoc.line, std::max(0, endLoc.column - 1) },
                                  .name = startLoc.name },
                    .text = std::move(text),
                    .isTrailing = (_lastTokenEndLine == startLoc.line),
                });
            }
            else
            {
                while (!eof() && _currentChar != U'\n' && _currentChar != U'\r')
                    nextChar();
            }
            continue;
        }

        // // line comment (C style)
        if (_currentChar == U'/' && _source->peekChar() == U'/')
        {
            _precedingSpace = true;
            if (_collectComments)
            {
                auto const startLoc = _source->currentSourceLocation();
                std::string text;
                text += '/';
                nextChar();
                text += '/';
                nextChar();
                while (!eof() && _currentChar != U'\n' && _currentChar != U'\r')
                {
                    text += unicode::to_utf8(_currentChar);
                    nextChar();
                }
                auto const endLoc = _source->currentSourceLocation();
                _comments.push_back(CommentTrivia {
                    .style = CommentStyle::CStyle,
                    .location = { .begin = { startLoc.line, std::max(0, startLoc.column - 1) },
                                  .end = { endLoc.line, std::max(0, endLoc.column - 1) },
                                  .name = startLoc.name },
                    .text = std::move(text),
                    .isTrailing = (_lastTokenEndLine == startLoc.line),
                });
            }
            else
            {
                while (!eof() && _currentChar != U'\n' && _currentChar != U'\r')
                    nextChar();
            }
            continue;
        }

        // (* ... *) block comment (F# style, nestable)
        if (_currentChar == U'(' && _source->peekChar() == U'*')
        {
            _precedingSpace = true;
            if (_collectComments)
            {
                auto const startLoc = _source->currentSourceLocation();
                std::string text;
                text += '(';
                nextChar();
                text += '*';
                nextChar();
                auto depth = 1;
                while (!eof() && depth > 0)
                {
                    if (_currentChar == U'(' && _source->peekChar() == U'*')
                    {
                        text += '(';
                        nextChar();
                        text += '*';
                        nextChar();
                        ++depth;
                    }
                    else if (_currentChar == U'*' && _source->peekChar() == U')')
                    {
                        text += '*';
                        nextChar();
                        text += ')';
                        nextChar();
                        --depth;
                    }
                    else
                    {
                        text += unicode::to_utf8(_currentChar);
                        nextChar();
                    }
                }
                auto const endLoc = _source->currentSourceLocation();
                _comments.push_back(CommentTrivia {
                    .style = CommentStyle::FSharp,
                    .location = { .begin = { startLoc.line, std::max(0, startLoc.column - 1) },
                                  .end = { endLoc.line, std::max(0, endLoc.column - 1) },
                                  .name = startLoc.name },
                    .text = std::move(text),
                    .isTrailing = (_lastTokenEndLine == startLoc.line),
                });
            }
            else
            {
                nextChar(); // consume '('
                nextChar(); // consume '*'
                auto depth = 1;
                while (!eof() && depth > 0)
                {
                    if (_currentChar == U'(' && _source->peekChar() == U'*')
                    {
                        nextChar();
                        nextChar();
                        ++depth;
                    }
                    else if (_currentChar == U'*' && _source->peekChar() == U')')
                    {
                        nextChar();
                        nextChar();
                        --depth;
                    }
                    else
                    {
                        nextChar();
                    }
                }
            }
            continue;
        }

        break;
    }

    auto const [line, column, name] = _source->currentSourceLocation();
    _nextToken.location.name = name;
    _nextToken.location.begin = { .line = line, .column = std::max(0, column - 1) };
    _nextToken.location.end = _nextToken.location.begin;
}

Token Lexer::consumeNumber()
{
    // Check for base prefix when first digit is '0'
    if (_currentChar == U'0')
    {
        _nextToken.literal += '0';
        nextChar();

        if (_currentChar == U'x' || _currentChar == U'X')
        {
            _nextToken.literal += static_cast<char>(_currentChar);
            nextChar();
            while ((_currentChar >= U'0' && _currentChar <= U'9')
                   || (_currentChar >= U'a' && _currentChar <= U'f')
                   || (_currentChar >= U'A' && _currentChar <= U'F'))
            {
                _nextToken.literal += unicode::to_utf8(_currentChar);
                nextChar();
            }
            return finalizeNumberOrShellWord();
        }
        if (_currentChar == U'o' || _currentChar == U'O')
        {
            _nextToken.literal += static_cast<char>(_currentChar);
            nextChar();
            while (_currentChar >= U'0' && _currentChar <= U'7')
            {
                _nextToken.literal += unicode::to_utf8(_currentChar);
                nextChar();
            }
            return finalizeNumberOrShellWord();
        }
        if (_currentChar == U'b' || _currentChar == U'B')
        {
            _nextToken.literal += static_cast<char>(_currentChar);
            nextChar();
            while (_currentChar == U'0' || _currentChar == U'1')
            {
                _nextToken.literal += unicode::to_utf8(_currentChar);
                nextChar();
            }
            return finalizeNumberOrShellWord();
        }
        // Fall through: plain '0' followed by more decimal digits, '.', 'e', etc.
    }

    // Consume integer part
    while (_currentChar >= U'0' && _currentChar <= U'9')
    {
        _nextToken.literal += unicode::to_utf8(_currentChar);
        nextChar();
    }

    // Check for fractional part: '.' followed by digit
    // Peek ahead to distinguish floats (2.5) from ranges (2..10) or filenames (2.txt)
    if (_currentChar == U'.')
    {
        char32_t const afterDot = _source->peekChar();
        if (afterDot >= U'0' && afterDot <= U'9')
        {
            // It's a float - consume the dot and fractional digits
            _nextToken.literal += '.';
            nextChar(); // consume '.'

            while (_currentChar >= U'0' && _currentChar <= U'9')
            {
                _nextToken.literal += unicode::to_utf8(_currentChar);
                nextChar();
            }

            // Check for exponent part: 'e' or 'E'
            if (_currentChar == U'e' || _currentChar == U'E')
            {
                char32_t const afterE = _source->peekChar();
                bool hasExponent = (afterE >= U'0' && afterE <= U'9') || afterE == U'+' || afterE == U'-';

                if (hasExponent)
                {
                    _nextToken.literal += unicode::to_utf8(_currentChar);
                    nextChar(); // consume 'e' or 'E'

                    // Optional sign
                    if (_currentChar == U'+' || _currentChar == U'-')
                    {
                        _nextToken.literal += unicode::to_utf8(_currentChar);
                        nextChar();
                    }

                    // Exponent digits
                    while (_currentChar >= U'0' && _currentChar <= U'9')
                    {
                        _nextToken.literal += unicode::to_utf8(_currentChar);
                        nextChar();
                    }
                }
            }
        }
        // else: it's not a float (e.g., "2..10" range or "2.txt" filename)
    }
    // Also check for exponent without fractional part (e.g., 2e10)
    else if (_currentChar == U'e' || _currentChar == U'E')
    {
        char32_t const afterE = _source->peekChar();
        bool hasExponent = (afterE >= U'0' && afterE <= U'9') || afterE == U'+' || afterE == U'-';

        if (hasExponent)
        {
            _nextToken.literal += unicode::to_utf8(_currentChar);
            nextChar(); // consume 'e' or 'E'

            // Optional sign
            if (_currentChar == U'+' || _currentChar == U'-')
            {
                _nextToken.literal += unicode::to_utf8(_currentChar);
                nextChar();
            }

            // Exponent digits
            while (_currentChar >= U'0' && _currentChar <= U'9')
            {
                _nextToken.literal += unicode::to_utf8(_currentChar);
                nextChar();
            }
        }
    }

    return finalizeNumberOrShellWord();
}

Token Lexer::finalizeNumberOrShellWord()
{
    // In F# or arithmetic mode, always return Number
    if (_fsharpDepth > 0 || _arithDepth > 0)
        return confirmToken(Token::Number);

    // In shell mode, check if the character after the number continues a word
    // (e.g., git SHA "3a4b5c6" or filename "3.txt")
    using namespace std::string_view_literals;
    auto constexpr ReservedSymbols = U"|<>()!$'\"\t\r\n ;`"sv;
    if (!eof() && ReservedSymbols.find(_currentChar) == std::u32string_view::npos)
    {
        while (!eof() && ReservedSymbols.find(_currentChar) == std::u32string_view::npos)
        {
            _nextToken.literal += unicode::to_utf8(_currentChar);
            nextChar();
        }
        return confirmToken(Token::Identifier);
    }

    return confirmToken(Token::Number);
}

Token Lexer::consumeIdentifier()
{
    return consumeIdentifier(Token::Identifier);
}

Token Lexer::consumeIdentifier(Token token)
{
    // For DollarName tokens, only consume valid variable name characters [a-zA-Z0-9_]
    if (token == Token::DollarName)
    {
        while (!eof())
        {
            if ((_currentChar >= U'a' && _currentChar <= U'z')
                || (_currentChar >= U'A' && _currentChar <= U'Z')
                || (_currentChar >= U'0' && _currentChar <= U'9') || _currentChar == U'_')
            {
                _nextToken.literal += unicode::to_utf8(_currentChar);
                nextChar();
            }
            else
                break;
        }
        return confirmToken(token);
    }

    // Note: {} are NOT reserved to allow brace expansion patterns like {a,b,c} to be lexed as single tokens.
    // Note: [] are NOT reserved to allow glob bracket expressions like [abc] to be lexed as single tokens.
    // Note: - is NOT reserved to allow command-line flags like -l and filenames like file-name.txt.
    // Note: , is NOT reserved to allow brace expansion {a,b,c} to be lexed as single tokens.
    // Note: ? is NOT reserved to allow glob patterns like file?.txt to be lexed as single tokens.
    // Note: : is NOT reserved to allow values like one:two:three (common in PATH, etc.)
    //       The parser handles :: and : contextually for F# cons and type annotations.
    // Note: ~ is NOT reserved to allow mid-word tilde (e.g., HEAD~2 in git).
    //       Tilde expansion (~, ~/foo, ~user) still works because nextToken()'s
    //       `case '~': return consumeTilde()` fires when ~ starts a new token.
    auto constexpr ReservedSymbols = U"|<>()!$'\"\t\r\n ;`"sv;
    // In arithmetic context, operators are also reserved to allow expressions like 1+2
    auto constexpr ArithReservedSymbols = U"|<>()!$'\"\t\r\n ;`~+-*/%^&,?:"sv;
    // In F# expression context, brackets and operators are reserved
    auto constexpr FSharpReservedSymbols = U"|<>()!$'\"\t\r\n ;`~+-*/%^&,?:[]{}#."sv;
    // Arithmetic operators that should be lexed as single-char tokens
    auto constexpr ArithOperators = U"+-*/%^:?,"sv;

    auto const& reserved = _fsharpDepth > 0  ? FSharpReservedSymbols
                           : _arithDepth > 0 ? ArithReservedSymbols
                                             : ReservedSymbols;

    // In arithmetic mode, if current char is an operator, consume it as a single-char token
    if (_arithDepth > 0 && ArithOperators.find(_currentChar) != std::u32string_view::npos)
    {
        _nextToken.literal += unicode::to_utf8(_currentChar);
        nextChar();
        return confirmToken(token);
    }

    while (!eof() && reserved.find(_currentChar) == std::string_view::npos)
    {
        _nextToken.literal += unicode::to_utf8(_currentChar);
        nextChar();
    }

    // In F# mode, allow hyphen-letter continuation to form compound identifiers
    // like "open-json", "from-csv", "is-even". Hyphen followed by digit/space/operator/EOF
    // remains the minus operator. In shell mode, hyphen is already non-reserved.
    if (_fsharpDepth > 0 && token == Token::Identifier && !_nextToken.literal.empty())
    {
        while (_currentChar == U'-')
        {
            auto const afterHyphen = _source->peekChar();
            if ((afterHyphen >= U'a' && afterHyphen <= U'z') || (afterHyphen >= U'A' && afterHyphen <= U'Z'))
            {
                _nextToken.literal += '-';
                nextChar(); // consume '-'
                while (!eof() && reserved.find(_currentChar) == std::string_view::npos)
                {
                    _nextToken.literal += unicode::to_utf8(_currentChar);
                    nextChar();
                }
            }
            else
                break;
        }
    }

    // Check for F# keywords only for regular identifiers (not DollarName)
    if (token == Token::Identifier)
    {
        Token const keywordToken = keywordToToken(_nextToken.literal);
        if (keywordToken != Token::Identifier)
            return confirmToken(keywordToken);
    }

    return confirmToken(token);
}

Token Lexer::consumeSingleQuotedString()
{
    // Single-quoted strings: no interpolation, backslash is literal
    nextChar(); // consume opening '
    while (_currentChar != '\'' && !eof())
    {
        _nextToken.literal += unicode::to_utf8(_currentChar);
        nextChar();
    }
    if (eof())
        return confirmToken(Token::Invalid); // Unterminated string
    nextChar();                              // consume closing '
    return confirmToken(Token::String);
}

Token Lexer::consumeDoubleQuotedContent()
{
    // We're inside a double-quoted string, tokenize content with interpolation support
    // This is called repeatedly until we hit the closing quote

    // Set up location tracking
    auto const [line, column, name] = _source->currentSourceLocation();
    _nextToken.location.name = name;
    _nextToken.location.begin = { .line = line, .column = std::max(0, column - 1) };
    _nextToken.location.end = _nextToken.location.begin;
    _nextToken.literal.clear();

    while (!eof())
    {
        switch (_currentChar)
        {
            case '"':
                // End of double-quoted string
                if (!_fragmentBuffer.empty())
                {
                    // Emit pending fragment first
                    _nextToken.literal = std::move(_fragmentBuffer);
                    _fragmentBuffer.clear();
                    return confirmToken(Token::StringFragment);
                }
                // Emit the closing quote
                _inDoubleQuote = false;
                return consumeCharAndConfirmToken(Token::DblQuoteEnd);

            case '\\':
                // Escape sequences in double quotes
                nextChar();
                if (eof())
                {
                    // Unterminated string with trailing backslash
                    _inDoubleQuote = false;
                    return confirmToken(Token::Invalid);
                }
                switch (_currentChar)
                {
                    case '"':
                    case '\\':
                    case '$':
                    case '`':
                        // These escapes are recognized in double quotes
                        _fragmentBuffer += static_cast<char>(_currentChar);
                        nextChar();
                        break;
                    case 'n':
                        _fragmentBuffer += '\n';
                        nextChar();
                        break;
                    case 't':
                        _fragmentBuffer += '\t';
                        nextChar();
                        break;
                    case 'r':
                        _fragmentBuffer += '\r';
                        nextChar();
                        break;
                    default:
                        // Unknown escape: keep both backslash and character
                        _fragmentBuffer += '\\';
                        _fragmentBuffer += unicode::to_utf8(_currentChar);
                        nextChar();
                        break;
                }
                break;

            case '$':
                // Variable or command substitution - emit pending fragment first
                if (!_fragmentBuffer.empty())
                {
                    _nextToken.literal = std::move(_fragmentBuffer);
                    _fragmentBuffer.clear();
                    return confirmToken(Token::StringFragment);
                }
                // Now handle the $ sequence
                nextChar();
                if (_currentChar == '(')
                {
                    nextChar();
                    if (_currentChar == '(')
                    {
                        ++_dquoteSubstDepth; // Entering $((
                        ++_arithDepth;       // Enter arithmetic context
                        return consumeCharAndConfirmToken(Token::DollarDblRndOpen);
                    }
                    ++_dquoteSubstDepth; // Entering $(
                    return confirmToken(Token::DollarRndOpen);
                }
                else if (_currentChar == '{')
                    return consumeBracedVariable();
                else if (_currentChar == '$')
                    return consumeCharAndConfirmToken(Token::DollarDollar);
                else if (_currentChar == '!')
                    return consumeCharAndConfirmToken(Token::DollarNot);
                else if (_currentChar == '?')
                    return consumeCharAndConfirmToken(Token::DollarQuestion);
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
                {
                    // Bare $ followed by something else - treat as literal
                    _fragmentBuffer += '$';
                }
                break;

            case '`':
                // Backtick command substitution - emit pending fragment first
                if (!_fragmentBuffer.empty())
                {
                    _nextToken.literal = std::move(_fragmentBuffer);
                    _fragmentBuffer.clear();
                    return confirmToken(Token::StringFragment);
                }
                ++_dquoteSubstDepth; // Entering backtick substitution
                return consumeCharAndConfirmToken(Token::Backtick);

            default:
                // Regular character - add to fragment buffer
                _fragmentBuffer += unicode::to_utf8(_currentChar);
                nextChar();
                break;
        }
    }

    // EOF while inside double-quoted string
    // First emit any pending fragment
    if (!_fragmentBuffer.empty())
    {
        _nextToken.literal = std::move(_fragmentBuffer);
        _fragmentBuffer.clear();
        return confirmToken(Token::StringFragment);
    }
    _inDoubleQuote = false;
    return confirmToken(Token::Invalid);
}

Token Lexer::consumeFStringContent()
{
    // Set up location tracking
    auto const [line, column, name] = _source->currentSourceLocation();
    _nextToken.location.name = name;
    _nextToken.location.begin = { .line = line, .column = std::max(0, column - 1) };
    _nextToken.location.end = _nextToken.location.begin;
    _nextToken.literal.clear();

    while (!eof())
    {
        switch (_currentChar)
        {
            case '"':
                // End of F# interpolated string
                if (!_fragmentBuffer.empty())
                {
                    _nextToken.literal = std::move(_fragmentBuffer);
                    _fragmentBuffer.clear();
                    return confirmToken(Token::StringFragment);
                }
                _inFString = false;
                return consumeCharAndConfirmToken(Token::FStringEnd);

            case '{':
                // Check for escaped brace: {{ → literal {
                if (_source->peekChar() == U'{')
                {
                    nextChar(); // consume first {
                    nextChar(); // consume second {
                    _fragmentBuffer += '{';
                    break;
                }
                // Expression hole start
                if (!_fragmentBuffer.empty())
                {
                    _nextToken.literal = std::move(_fragmentBuffer);
                    _fragmentBuffer.clear();
                    return confirmToken(Token::StringFragment);
                }
                _fstringBraceDepth = 1;
                return consumeCharAndConfirmToken(Token::FStringExprStart);

            case '}':
                // Check for escaped brace: }} → literal }
                if (_source->peekChar() == U'}')
                {
                    nextChar(); // consume first }
                    nextChar(); // consume second }
                    _fragmentBuffer += '}';
                    break;
                }
                // Stray } outside expression hole - treat as literal
                _fragmentBuffer += '}';
                nextChar();
                break;

            case '\\':
                // Escape sequences (same as double-quoted strings)
                nextChar();
                if (eof())
                {
                    _inFString = false;
                    return confirmToken(Token::Invalid);
                }
                switch (_currentChar)
                {
                    case '"':
                    case '\\':
                    case '{':
                    case '}':
                        _fragmentBuffer += static_cast<char>(_currentChar);
                        nextChar();
                        break;
                    case 'n':
                        _fragmentBuffer += '\n';
                        nextChar();
                        break;
                    case 't':
                        _fragmentBuffer += '\t';
                        nextChar();
                        break;
                    case 'r':
                        _fragmentBuffer += '\r';
                        nextChar();
                        break;
                    default:
                        _fragmentBuffer += '\\';
                        _fragmentBuffer += unicode::to_utf8(_currentChar);
                        nextChar();
                        break;
                }
                break;

            default:
                _fragmentBuffer += unicode::to_utf8(_currentChar);
                nextChar();
                break;
        }
    }

    // EOF while inside F# interpolated string
    if (!_fragmentBuffer.empty())
    {
        _nextToken.literal = std::move(_fragmentBuffer);
        _fragmentBuffer.clear();
        return confirmToken(Token::StringFragment);
    }
    _inFString = false;
    return confirmToken(Token::Invalid);
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
    _nextToken.location.end = { .line = a, .column = std::max(0, b - 1) };
    _currentToken = _nextToken;

    // Track the line of the last meaningful token for trailing comment detection.
    // Uses begin.line (set in consumeWhitespace before token content) rather than
    // end.line, because readChar() advances the line counter when reading '\n',
    // making end.line point to the next line for tokens at line boundaries.
    // Skip LineFeed/Semicolon since they are not "meaningful" tokens.
    if (token != Token::LineFeed && token != Token::Semicolon)
        _lastTokenEndLine = _currentToken.location.begin.line;

    _nextToken.literal = {};
    _nextToken.location.name = _source->currentSourceLocation().name;
    _nextToken.location.begin = _nextToken.location.end;

    _atStatementStart = (token == Token::Semicolon || token == Token::LineFeed);

    return token;
}

std::string Lexer::currentTokenText() const
{
    if (!_currentToken.literal.empty())
        return _currentToken.literal;
    switch (_currentToken.token)
    {
        case Token::EndOfInput: return "end of input";
        case Token::LineFeed: return "end of line";
        default: return std::string(tos(_currentToken.token));
    }
}

} // namespace endo
