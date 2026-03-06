// SPDX-License-Identifier: Apache-2.0
#include <endo-language/ide/CompletionContext.hpp>
#include <endo-language/lexer/Lexer.hpp>

#include <algorithm>

namespace endo
{

namespace
{

    // Tokens that indicate the next token is in command position
    bool isCommandBoundary(Token token)
    {
        switch (token)
        {
            case Token::Pipe:
            case Token::Semicolon:
            case Token::AmpAmp:
            case Token::PipePipe:
            case Token::RndOpen:
            case Token::LineFeed:
            case Token::Ampersand: return true;
            default: return false;
        }
    }

    // Tokens that indicate a redirect context (file target expected)
    bool isRedirectToken(Token token)
    {
        switch (token)
        {
            case Token::Less:
            case Token::Greater:
            case Token::GreaterGreater:
            case Token::GreaterAmp:
            case Token::LessLess:
            case Token::LessLessLess: return true;
            default: return false;
        }
    }

} // namespace

CompletionContext CompletionContextAnalyzer::analyze(std::string_view input, size_t cursorPosition)
{
    CompletionContext ctx;
    ctx.fullInput = std::string(input);
    ctx.cursorPosition = cursorPosition;

    // Handle empty input
    if (input.empty() || cursorPosition == 0)
    {
        ctx.type = CompletionContextType::Command;
        ctx.prefix = "";
        ctx.prefixStart = 0;
        return ctx;
    }

    // Find word boundaries
    auto [wordStart, wordEnd] = findWordBoundaries(input, cursorPosition);
    ctx.prefix = std::string(input.substr(wordStart, cursorPosition - wordStart));
    ctx.prefixStart = wordStart;

    // Check for variable context first (highest priority)
    if (isVariableStart(input, wordStart))
    {
        // Check if we're in a brace expansion
        if (wordStart + 1 < input.size() && input[wordStart + 1] == '{')
        {
            ctx.type = CompletionContextType::VariableBrace;
            // Prefix is just the variable name part (after ${)
            size_t varStart = wordStart + 2;
            ctx.prefix = std::string(input.substr(varStart, cursorPosition - varStart));
            ctx.prefixStart = varStart;
        }
        else
        {
            ctx.type = CompletionContextType::Variable;
            // Prefix is just the variable name part (after $)
            size_t varStart = wordStart + 1;
            ctx.prefix = std::string(input.substr(varStart, cursorPosition - varStart));
            ctx.prefixStart = varStart;
        }
        return ctx;
    }

    // Check for option context
    if (looksLikeOption(ctx.prefix))
    {
        ctx.type = CompletionContextType::Option;
        // Try to find the command for this option
        // Tokenize and find the first identifier that's not a keyword
        try
        {
            auto tokens =
                Lexer::tokenize(std::make_unique<StringSource>(std::string(input.substr(0, wordStart))));
            for (auto const& tok: tokens)
            {
                if (tok.token == Token::Identifier)
                {
                    ctx.command = tok.literal;
                    break;
                }
            }
        }
        catch (...) // NOLINT(bugprone-empty-catch)
        {
            // Tokenization failed, ignore
        }
        return ctx;
    }

    // Tokenize to determine if we're in command or argument position
    try
    {
        auto inputUpToCursor = std::string(input.substr(0, wordStart));

        // Strip trailing unclosed quote (cursor may be inside a quoted argument)
        if (!inputUpToCursor.empty() && (inputUpToCursor.back() == '"' || inputUpToCursor.back() == '\''))
            inputUpToCursor.pop_back();

        auto tokens = Lexer::tokenize(std::make_unique<StringSource>(inputUpToCursor));

        if (tokens.empty())
        {
            // In command position, file-path-like prefixes trigger file completion
            ctx.type = looksLikeFilePath(ctx.prefix) ? CompletionContextType::FilePath
                                                     : CompletionContextType::Command;
            return ctx;
        }

        // Check if the last non-whitespace token indicates command position
        Token lastToken = Token::EndOfInput;
        std::string lastIdentifier;

        for (auto const& tok: tokens)
        {
            if (tok.token != Token::EndOfInput)
            {
                if (tok.token == Token::Identifier)
                    lastIdentifier = tok.literal;
                lastToken = tok.token;
            }
        }

        // If last token was a left-arrow assignment (<-), the preceding identifier is the property
        if (lastToken == Token::LeftArrow)
        {
            ctx.type = CompletionContextType::Argument;
            if (!lastIdentifier.empty())
                ctx.command = lastIdentifier;
            return ctx;
        }

        // If last token was a redirect, we need a file
        if (isRedirectToken(lastToken))
        {
            ctx.type = CompletionContextType::Redirect;
            return ctx;
        }

        // If last token was a command boundary, next is a command
        if (isCommandBoundary(lastToken))
        {
            ctx.type = looksLikeFilePath(ctx.prefix) ? CompletionContextType::FilePath
                                                     : CompletionContextType::Command;
            return ctx;
        }

        // Check if this is the first token (command position)
        bool hasCommand = false;
        for (auto const& tok: tokens)
        {
            if (tok.token == Token::Identifier || tok.token == Token::String)
            {
                hasCommand = true;
                ctx.command = tok.literal;
                break;
            }
        }

        if (!hasCommand)
        {
            // In command position, file-path-like prefixes (./script, ~/bin, path/to/cmd)
            // should trigger file completion rather than command completion
            if (looksLikeFilePath(ctx.prefix))
                ctx.type = CompletionContextType::FilePath;
            else
                ctx.type = CompletionContextType::Command;
        }
        else
        {
            ctx.type = CompletionContextType::Argument;
        }
    }
    catch (...)
    {
        // Tokenization failed (incomplete input), default to argument
        ctx.type = CompletionContextType::Argument;
    }

    return ctx;
}

std::pair<size_t, size_t> CompletionContextAnalyzer::findWordBoundaries(std::string_view input,
                                                                        size_t cursorPosition)
{
    size_t wordStart = cursorPosition;
    size_t wordEnd = cursorPosition;

    // Find start of word (go backwards)
    while (wordStart > 0)
    {
        char ch = input[wordStart - 1];
        if (isWordBreak(ch))
            break;
        --wordStart;
    }

    // Find end of word (go forwards)
    while (wordEnd < input.size())
    {
        char ch = input[wordEnd];
        if (isWordBreak(ch))
            break;
        ++wordEnd;
    }

    return { wordStart, wordEnd };
}

bool CompletionContextAnalyzer::isWordBreak(char ch)
{
    // These characters break words in shell context
    switch (ch)
    {
        case ' ':
        case '\t':
        case '\n':
        case '\r':
        case '|':
        case ';':
        case '&':
        case '<':
        case '>':
        case '(':
        case ')':
        case '`':
        case '"':
        case '\'': return true;
        default: return false;
    }
}

bool CompletionContextAnalyzer::isVariableStart(std::string_view input, size_t pos)
{
    // Check if the word starts with $
    if (pos < input.size() && input[pos] == '$')
        return true;

    // Also check if we're inside a ${...} that started earlier
    // Look backwards for an unmatched ${
    int braceDepth = 0;
    for (size_t i = pos; i > 0; --i)
    {
        if (i + 1 < input.size() && input[i - 1] == '$' && input[i] == '{')
        {
            // Found ${
            if (braceDepth == 0)
            {
                // Check if there's a matching }
                bool foundClose = false;
                for (size_t j = i + 1; j < input.size(); ++j)
                {
                    if (input[j] == '}')
                    {
                        if (j > pos)
                        {
                            // Closing brace is after cursor, we're inside
                            return true;
                        }
                        foundClose = true;
                        break;
                    }
                }
                if (!foundClose)
                    return true; // No closing brace, we're inside
            }
            --braceDepth;
        }
        else if (input[i - 1] == '}')
        {
            ++braceDepth;
        }
    }

    return false;
}

bool CompletionContextAnalyzer::looksLikeFilePath(std::string_view prefix)
{
    if (prefix.empty())
        return false;

    // Starts with path-like characters
    if (prefix[0] == '/' || prefix[0] == '.' || prefix[0] == '~')
        return true;

    // Contains a path separator
    if (prefix.find('/') != std::string_view::npos)
        return true;

    return false;
}

bool CompletionContextAnalyzer::looksLikeOption(std::string_view prefix)
{
    if (prefix.empty())
        return false;

    return prefix[0] == '-';
}

} // namespace endo
