// SPDX-License-Identifier: Apache-2.0
#include <endo-language/lexer/ContextAwareTokenizer.hpp>

#include <ranges>

namespace endo
{

std::vector<ClassifiedToken> tokenizeWithContext(std::string_view source)
{
    auto result = std::vector<ClassifiedToken> {};
    if (source.empty())
        return result;

    auto lexer = Lexer { std::make_unique<StringSource>(std::string(source)) };

    // Start in shell mode (no enterFSharpExpr call).
    // Track whether we're at the start of a new statement.
    auto atStatementStart = true;
    auto inFSharpStatement = false;

    while (lexer.currentToken() != Token::EndOfInput)
    {
        auto const token = lexer.currentToken();
        auto const& literal = lexer.currentLiteral();

        // Statement boundary: newline or semicolon resets to statement start.
        if (token == Token::LineFeed || token == Token::Semicolon)
        {
            atStatementStart = true;
            // Leave F# mode at statement boundary so the next statement starts in shell mode.
            if (inFSharpStatement && lexer.inFSharpMode())
                lexer.leaveFSharpExpr();
            inFSharpStatement = false;

            result.push_back(ClassifiedToken {
                .token = token,
                .literal = std::string(literal),
                .location = lexer.currentRange(),
                .category = TokenCategory::Default,
            });
            lexer.nextToken();
            continue;
        }

        // At statement start, decide whether to enter F# mode.
        if (atStatementStart)
        {
            atStatementStart = false;

            if (isFSharpStartToken(token))
            {
                // F# keyword/constructor at statement start → enter F# mode.
                if (!lexer.inFSharpMode())
                    lexer.enterFSharpExpr();
                inFSharpStatement = true;
            }
            else if (token == Token::Pipe)
            {
                // Match arm continuation (| pattern → ...).
                if (!lexer.inFSharpMode())
                    lexer.enterFSharpExpr();
                inFSharpStatement = true;
            }
            else if (token == Token::Number)
            {
                // Bare number at statement start → F# expression (e.g., "42 |> print").
                if (!lexer.inFSharpMode())
                    lexer.enterFSharpExpr();
                inFSharpStatement = true;
            }
            else if (token == Token::RndOpen)
            {
                // Parenthesized expression at statement start → F# mode.
                if (!lexer.inFSharpMode())
                    lexer.enterFSharpExpr();
                inFSharpStatement = true;
            }
            else if (token == Token::Identifier)
            {
                if (isKnownFSharpFunction(literal))
                {
                    // Known F# function → enter F# mode.
                    if (!lexer.inFSharpMode())
                        lexer.enterFSharpExpr();
                    inFSharpStatement = true;
                }
                else
                {
                    // Shell command → stay in shell mode.
                    if (lexer.inFSharpMode())
                        lexer.leaveFSharpExpr();
                    inFSharpStatement = false;
                }
            }
            else if (token == Token::DblQuoteStart || token == Token::FStringStart || token == Token::String)
            {
                // String at statement start → stay in shell mode (e.g., "'hello'" or shell command).
                if (lexer.inFSharpMode())
                    lexer.leaveFSharpExpr();
                inFSharpStatement = false;
            }
            else
            {
                // Other tokens at statement start → try F# mode for safety.
                if (!lexer.inFSharpMode())
                    lexer.enterFSharpExpr();
                inFSharpStatement = true;
            }
        }

        // Classify the token with context awareness.
        auto category = classifyTokenCategory(token);

        // Override: shell builtins at statement start position get Function category.
        // We detect this by checking: we just decided this is a shell statement (not F#),
        // and the token is an Identifier that is a known shell builtin.
        if (!inFSharpStatement && token == Token::Identifier && result.empty() == false)
        {
            // Check if the previous meaningful token was a statement boundary (or this is the first token).
            auto isFirstOnLine = true;
            for (auto& it: std::ranges::reverse_view(result))
            {
                if (it.token == Token::LineFeed || it.token == Token::Semicolon)
                    break;
                if (it.category != TokenCategory::Default)
                {
                    isFirstOnLine = false;
                    break;
                }
            }
            if (isFirstOnLine && isShellBuiltin(literal))
                category = TokenCategory::Function;
        }
        else if (!inFSharpStatement && token == Token::Identifier && result.empty())
        {
            // Very first token in source.
            if (isShellBuiltin(literal))
                category = TokenCategory::Function;
        }

        result.push_back(ClassifiedToken {
            .token = token,
            .literal = std::string(literal),
            .location = lexer.currentRange(),
            .category = category,
        });

        lexer.nextToken();
    }

    return result;
}

} // namespace endo
