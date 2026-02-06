// SPDX-License-Identifier: Apache-2.0
#include "Parser.hpp"
#include <shell/AST.hpp>
#include <shell/DiagnosticsAdapter.hpp>
#include <shell/ScopedLogger.hpp>

#include <CoreVM/CoreVM.hpp>

#include <crispy/utils.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <memory>
#include <optional>
#include <ranges>

#include "ASTPrinter.hpp"
#include "Lexer.hpp"
#include "LogConfig.hpp"

// Use function-local static to avoid C++20 module static initialization issues
inline auto& parserLog()
{
    static auto instance = logstore::category("parser", "Parser logger", endo::log::categoryState("parser"));
    return instance;
}

#define TRACE_SCOPE(message) ScopedLogger _logger { message, parserLog() };
#define TRACE_FMT(message, ...)                                                        \
    do                                                                                 \
    {                                                                                  \
        parserLog()()("{}", ScopedLogger::write(::std::format(message, __VA_ARGS__))); \
    } while (0)
#define TRACE(message)                                                    \
    do                                                                    \
    {                                                                     \
        parserLog()()("{}", ScopedLogger::write(::std::format(message))); \
    } while (0)

namespace endo
{

Parser::Parser(CoreVM::Runtime& runtime, CoreVM::diagnostics::Report& report, std::unique_ptr<Source> source):
    _runtime { runtime }, _report { report }, _lexer { std::move(source) }
{
}

std::unique_ptr<ast::Statement> Parser::parse()
{
    return parseBlock("global");
}

void Parser::setSourceText(std::string_view source)
{
    _sourceText = source;
}

CoreVM::SourceLocation Parser::currentLocation() const
{
    return toCoreLoc(_lexer.currentRange());
}

std::optional<std::string> Parser::currentContextSnippet() const
{
    if (_sourceText.empty())
        return std::nullopt;
    auto const line = extractSourceLine(_sourceText, _lexer.currentRange().begin.line);
    return line.empty() ? std::nullopt : std::make_optional(line);
}

bool Parser::isEndOfBlock() const noexcept
{
    // clang-format off
    return _lexer.currentToken() == Token::EndOfInput
        || _lexer.isDirective("else")
        || _lexer.isDirective("elif")
        || _lexer.isDirective("fi")
        || _lexer.isDirective("done")
        || _lexer.isDirective("esac")
        || _lexer.currentLiteral() == "}"
        || _lexer.currentToken() == Token::DblSemicolon;
    // clang-format on
}

bool Parser::isEndOfStmt() const noexcept
{
    // clang-format off
    if (_lexer.currentToken() == Token::EndOfInput
        || _lexer.currentToken() == Token::LineFeed
        || _lexer.currentToken() == Token::Pipe
        || _lexer.currentToken() == Token::Semicolon
        || _lexer.currentToken() == Token::DblSemicolon
        || _lexer.currentToken() == Token::AmpAmp
        || _lexer.currentToken() == Token::PipePipe
        || _lexer.currentToken() == Token::RndClose
        || _lexer.currentToken() == Token::Ampersand)
        return true;

    // Backtick is end-of-statement only when we're inside a backtick substitution
    if (_lexer.currentToken() == Token::Backtick && _backtickNestingLevel > 0)
        return true;

    return false;
    // clang-format on
}

bool Parser::isParameterToken() const noexcept
{
    switch (_lexer.currentToken())
    {
        case Token::String:
        case Token::Number:
        case Token::Identifier:
        case Token::DollarName:
        case Token::DollarBraceName:
        case Token::DollarQuestion:
        case Token::DollarDollar:
        case Token::DollarNot:
        case Token::DollarNumber:
        case Token::DollarRndOpen:
        case Token::DollarBraceParam:
        case Token::LessRndOpen:
        case Token::GreaterRndOpen:
        case Token::Tilde: return true;
        case Token::Backtick:
            // Backtick is a parameter token only at nesting level 0
            return _backtickNestingLevel == 0;
        default: return false;
    }
}

std::unique_ptr<ast::Statement> Parser::parseBlock(std::string_view traceMessage)
{
    TRACE_SCOPE(std::format("parseBlock{}", traceMessage.empty() ? "" : std::format(" ({})", traceMessage)));
    auto scope = std::make_unique<ast::CompoundStmt>();
    while (!isEndOfBlock())
    {
        if (consumeUntilNotOneOf(Token::Semicolon, Token::LineFeed))
            continue;
        auto stmt = parseStmt();
        if (!stmt)
        {
            TRACE_FMT("Parsed scope.1: {}", ast::ASTPrinter::print(*scope));
            return scope;
        }

        TRACE_FMT("Parsed statement: {}", ast::ASTPrinter::print(*stmt));
        scope->statements.emplace_back(std::move(stmt));
    }
    TRACE_FMT(
        "Parsed scope.3 (current token: {}): {}", _lexer.currentLiteral(), ast::ASTPrinter::print(*scope));
    return scope;
}

std::unique_ptr<ast::Statement> Parser::parseStmt()
{
    TRACE_SCOPE("parseStmt");
    switch (_lexer.currentToken())
    {
        case Token::String:
        case Token::Identifier:
            if (_lexer.isDirective("if"))
                return parseIf();
            else if (_lexer.isDirective("while"))
                return parseWhile();
            else if (_lexer.isDirective("for"))
                return parseFor();
            else if (_lexer.isDirective("case"))
                return parseCase();
            else if (_lexer.isDirective("function"))
                return parseFunctionDef();
            else if (_lexer.isDirective("return"))
                return parseReturn();
            else if (_lexer.isDirective("break"))
                return parseBreak();
            else if (_lexer.isDirective("continue"))
                return parseContinue();
            else if (isFunctionDefinition())
                return parseFunctionDef();
            else
            {
                // All other statements (builtins and commands) can participate
                // in logical expressions (&&, ||)
                return parseLogicalExpr();
            }
        case Token::EndOfInput:
            _report.syntaxErrorWithSuggestions(
                currentLocation(),
                { "Check for unclosed quotes, parentheses, or control structures" },
                currentContextSnippet(),
                "Unexpected end of input");
            return nullptr;
        default:
            _report.syntaxErrorWithSuggestions(currentLocation(),
                                               {},
                                               currentContextSnippet(),
                                               "Unexpected token '{}'",
                                               _lexer.currentLiteral());
            return nullptr;
    }

    return nullptr;
}

std::string Parser::consumeLiteral()
{
    auto literal = _lexer.currentLiteral();
    _lexer.nextToken();
    return literal;
}

std::unique_ptr<ast::IfStmt> Parser::parseIf()
{
    TRACE_SCOPE("parseIf");
    // 'if' statement (LF | ';') statement ('else' statement)?
    _lexer.nextToken();
    auto condition = parseStmt();
    assert(condition != nullptr);
    if (!condition)
        return nullptr;

    if (!consumeOneOf(Token::Semicolon, Token::LineFeed))
    {
        TRACE_FMT("Expected ';' or LF after if condition but got '{}'", _lexer.currentLiteral());
        return nullptr;
    }

    TRACE_FMT("Parsed if condition: {}", ast::ASTPrinter::print(*condition));

    consumeDirective("then");

    auto thenBranch = parseBlock("trueBranch");
    if (!thenBranch)
        return nullptr;

    TRACE_FMT("Parsed if then branch: {}", ast::ASTPrinter::print(*thenBranch));

    std::unique_ptr<ast::Statement> elseBranch;
    if (_lexer.isDirective("elif"))
    {
        elseBranch = parseIf();
        TRACE_FMT("Parsed elif branch: {}", ast::ASTPrinter::print(*elseBranch));
    }
    else if (_lexer.isDirective("else"))
    {
        TRACE_FMT(
            "Parsing else branch (current token: {}, '{}')", _lexer.currentToken(), _lexer.currentLiteral());
        _lexer.nextToken();
        elseBranch = parseBlock("elseBranch");
        ;
        if (!elseBranch)
        {
            TRACE_FMT("Parsed elif branch: returned NULL (cur token: {})", _lexer.currentToken());
            return nullptr;
        }
        TRACE_FMT("Parsed elif branch: {}", ast::ASTPrinter::print(*elseBranch));
    }

    TRACE_FMT("Parsed if statement finished. Current token: {}", _lexer.currentLiteral());
    consumeDirective("fi");

    return std::make_unique<ast::IfStmt>(std::move(condition), std::move(thenBranch), std::move(elseBranch));
}

std::unique_ptr<ast::WhileStmt> Parser::parseWhile()
{
    TRACE_SCOPE("parseStmt");
    // 'while' statement (LF | ';') statement 'done'
    _lexer.nextToken(); // consume 'while'
    auto condition = parseStmt();
    if (!consumeOneOf(Token::Semicolon, Token::LineFeed))
    {
        TRACE_FMT("Expected ';' or LF after if condition but got '{}'", _lexer.currentLiteral());
        return nullptr;
    }

    consumeDirective("do");
    auto body = parseBlock("whileBody");
    consumeDirective("done");
    return std::make_unique<ast::WhileStmt>(std::move(condition), std::move(body));
}

std::unique_ptr<ast::Statement> Parser::parseFor()
{
    TRACE_SCOPE("parseFor");
    _lexer.nextToken(); // consume 'for'

    // Check for C-style: for ((init; cond; step))
    if (_lexer.currentToken() == Token::DblRndOpen)
    {
        return parseForCStyle();
    }

    // List-based: for var in list; do ...; done
    return parseForList();
}

std::unique_ptr<ast::ForListStmt> Parser::parseForList()
{
    TRACE_SCOPE("parseForList");

    // Expect variable name
    if (_lexer.currentToken() != Token::Identifier)
    {
        _report.syntaxErrorWithSuggestions(currentLocation(),
                                           { "Provide a variable name for the for loop" },
                                           currentContextSnippet(),
                                           "Expected variable name after 'for', got '{}'",
                                           _lexer.currentLiteral());
        return nullptr;
    }

    std::string variable = consumeLiteral();

    // Expect 'in' keyword
    if (!_lexer.isDirective("in"))
    {
        _report.syntaxErrorWithSuggestions(currentLocation(),
                                           { "Use 'in' to specify the list to iterate over" },
                                           currentContextSnippet(),
                                           "Expected 'in' after variable name, got '{}'",
                                           _lexer.currentLiteral());
        return nullptr;
    }
    _lexer.nextToken(); // consume 'in'

    // Parse items until ';' or newline followed by 'do'
    std::vector<std::unique_ptr<ast::Expr>> items;
    while (!isEndOfStmt() && !_lexer.isDirective("do"))
    {
        auto item = parseParameter();
        if (!item)
            break;
        items.emplace_back(std::move(item));
    }

    consumeOneOf(Token::Semicolon, Token::LineFeed);
    consumeDirective("do");

    auto body = parseBlock("forListBody");
    consumeDirective("done");

    return std::make_unique<ast::ForListStmt>(std::move(variable), std::move(items), std::move(body));
}

std::unique_ptr<ast::ForCStyleStmt> Parser::parseForCStyle()
{
    TRACE_SCOPE("parseForCStyle");
    _lexer.nextToken(); // consume '(('

    // Parse init expression (may be empty)
    std::unique_ptr<ast::ArithExpr> init;
    if (_lexer.currentToken() != Token::Semicolon)
    {
        init = parseArithOr();
    }

    if (_lexer.currentToken() != Token::Semicolon)
    {
        _report.syntaxErrorWithSuggestions(currentLocation(),
                                           {},
                                           currentContextSnippet(),
                                           "Expected ';' in for loop, got '{}'",
                                           _lexer.currentLiteral());
        return nullptr;
    }
    _lexer.nextToken(); // consume ';'

    // Parse condition expression (may be empty for infinite loop)
    std::unique_ptr<ast::ArithExpr> condition;
    if (_lexer.currentToken() != Token::Semicolon)
    {
        condition = parseArithOr();
    }

    if (_lexer.currentToken() != Token::Semicolon)
    {
        _report.syntaxErrorWithSuggestions(currentLocation(),
                                           {},
                                           currentContextSnippet(),
                                           "Expected ';' in for loop, got '{}'",
                                           _lexer.currentLiteral());
        return nullptr;
    }
    _lexer.nextToken(); // consume ';'

    // Parse step expression (may be empty)
    std::unique_ptr<ast::ArithExpr> step;
    if (_lexer.currentToken() != Token::DblRndClose)
    {
        step = parseArithOr();
    }

    if (_lexer.currentToken() != Token::DblRndClose)
    {
        _report.syntaxErrorWithSuggestions(currentLocation(),
                                           {},
                                           currentContextSnippet(),
                                           "Expected '))' in for loop, got '{}'",
                                           _lexer.currentLiteral());
        return nullptr;
    }
    _lexer.nextToken(); // consume '))'

    consumeOneOf(Token::Semicolon, Token::LineFeed);
    consumeDirective("do");

    auto body = parseBlock("forCStyleBody");
    consumeDirective("done");

    return std::make_unique<ast::ForCStyleStmt>(
        std::move(init), std::move(condition), std::move(step), std::move(body));
}

std::unique_ptr<ast::CaseStmt> Parser::parseCase()
{
    TRACE_SCOPE("parseCase");
    _lexer.nextToken(); // consume 'case'

    // Parse word to match
    auto word = parseParameter();
    if (!word)
    {
        _report.syntaxErrorWithSuggestions(
            currentLocation(), {}, currentContextSnippet(), "Expected word after 'case'");
        return nullptr;
    }

    // Expect 'in' keyword
    consumeOneOf(Token::Semicolon, Token::LineFeed);
    if (!_lexer.isDirective("in"))
    {
        _report.syntaxErrorWithSuggestions(currentLocation(),
                                           {},
                                           currentContextSnippet(),
                                           "Expected 'in' after case word, got '{}'",
                                           _lexer.currentLiteral());
        return nullptr;
    }
    _lexer.nextToken(); // consume 'in'
    consumeUntilNotOneOf(Token::Semicolon, Token::LineFeed);

    // Parse clauses
    std::vector<ast::CaseClause> clauses;
    while (!_lexer.isDirective("esac") && _lexer.currentToken() != Token::EndOfInput)
    {
        ast::CaseClause clause;

        // Parse patterns (pipe-separated)
        while (true)
        {
            // Skip leading '(' if present (optional in patterns)
            if (_lexer.currentToken() == Token::RndOpen)
                _lexer.nextToken();

            if (_lexer.currentToken() != Token::Identifier && _lexer.currentToken() != Token::String
                && _lexer.currentToken() != Token::Number)
            {
                break;
            }

            clause.patterns.push_back(consumeLiteral());

            if (_lexer.currentToken() == Token::Pipe)
            {
                _lexer.nextToken(); // consume '|'
                continue;
            }
            break;
        }

        if (clause.patterns.empty())
        {
            _report.syntaxErrorWithSuggestions(
                currentLocation(), {}, currentContextSnippet(), "Expected pattern in case clause");
            return nullptr;
        }

        // Expect ')' after patterns
        if (_lexer.currentToken() != Token::RndClose)
        {
            _report.syntaxErrorWithSuggestions(currentLocation(),
                                               {},
                                               currentContextSnippet(),
                                               "Expected ')' after pattern, got '{}'",
                                               _lexer.currentLiteral());
            return nullptr;
        }
        _lexer.nextToken(); // consume ')'

        // Parse commands until ';;' or 'esac'
        clause.body = parseBlock("caseClauseBody");

        // Expect ';;' or 'esac'
        if (_lexer.currentToken() == Token::DblSemicolon)
        {
            _lexer.nextToken(); // consume ';;'
            consumeUntilNotOneOf(Token::Semicolon, Token::LineFeed);
        }

        clauses.push_back(std::move(clause));
    }

    consumeDirective("esac");

    return std::make_unique<ast::CaseStmt>(std::move(word), std::move(clauses));
}

bool Parser::isFunctionDefinition() const noexcept
{
    // This is a simple heuristic - we check if we have "name()" pattern
    // We'd need proper lookahead for full correctness
    return false; // For now, require explicit 'function' keyword
}

std::unique_ptr<ast::FunctionDefStmt> Parser::parseFunctionDef()
{
    TRACE_SCOPE("parseFunctionDef");

    // Handle 'function' keyword if present
    if (_lexer.isDirective("function"))
        _lexer.nextToken(); // consume 'function'

    // Expect function name
    if (_lexer.currentToken() != Token::Identifier)
    {
        _report.syntaxErrorWithSuggestions(currentLocation(),
                                           {},
                                           currentContextSnippet(),
                                           "Expected function name, got '{}'",
                                           _lexer.currentLiteral());
        return nullptr;
    }

    std::string name = consumeLiteral();

    // Optional '()' after name
    if (_lexer.currentToken() == Token::RndOpen)
    {
        _lexer.nextToken(); // consume '('
        if (_lexer.currentToken() != Token::RndClose)
        {
            _report.syntaxErrorWithSuggestions(currentLocation(),
                                               {},
                                               currentContextSnippet(),
                                               "Expected ')' after '(' in function definition");
            return nullptr;
        }
        _lexer.nextToken(); // consume ')'
    }

    consumeUntilNotOneOf(Token::Semicolon, Token::LineFeed);

    // Expect '{' or body starts directly
    std::unique_ptr<ast::Statement> body;
    if (_lexer.currentLiteral() == "{")
    {
        _lexer.nextToken(); // consume '{'
        consumeUntilNotOneOf(Token::Semicolon, Token::LineFeed);
        body = parseBlock("functionBody");
        if (_lexer.currentLiteral() != "}")
        {
            _report.syntaxErrorWithSuggestions(currentLocation(),
                                               {},
                                               currentContextSnippet(),
                                               "Expected '}}' to close function body, got '{}'",
                                               _lexer.currentLiteral());
            return nullptr;
        }
        _lexer.nextToken(); // consume '}'
    }
    else
    {
        // Allow function body to be a single compound statement (not braced)
        body = parseStmt();
    }

    return std::make_unique<ast::FunctionDefStmt>(std::move(name), std::move(body));
}

std::unique_ptr<ast::ReturnStmt> Parser::parseReturn()
{
    TRACE_SCOPE("parseReturn");
    _lexer.nextToken(); // consume 'return'

    std::unique_ptr<ast::Expr> value;
    if (!isEndOfStmt())
    {
        value = parseParameter();
    }

    return std::make_unique<ast::ReturnStmt>(std::move(value));
}

std::unique_ptr<ast::BreakStmt> Parser::parseBreak()
{
    TRACE_SCOPE("parseBreak");
    _lexer.nextToken(); // consume 'break'

    int levels = 1;
    if (_lexer.currentToken() == Token::Number)
    {
        levels = std::stoi(_lexer.currentLiteral());
        _lexer.nextToken();
    }

    return std::make_unique<ast::BreakStmt>(levels);
}

std::unique_ptr<ast::ContinueStmt> Parser::parseContinue()
{
    TRACE_SCOPE("parseContinue");
    _lexer.nextToken(); // consume 'continue'

    int levels = 1;
    if (_lexer.currentToken() == Token::Number)
    {
        levels = std::stoi(_lexer.currentLiteral());
        _lexer.nextToken();
    }

    return std::make_unique<ast::ContinueStmt>(levels);
}

bool Parser::isRedirectToken() const noexcept
{
    switch (_lexer.currentToken())
    {
        case Token::Greater:        // >
        case Token::GreaterGreater: // >>
        case Token::GreaterAmp:     // >&
        case Token::Less:           // <
        case Token::LessLess:       // <<
        case Token::LessLessDash:   // <<-
        case Token::LessLessLess:   // <<<
            return true;
        default: return false;
    }
}

bool Parser::isNumberBeforeRedirect() const noexcept
{
    if (_lexer.currentToken() != Token::Number)
        return false;

    // We need to look ahead to see if a redirect operator follows
    // This is tricky without peeking, so for now we assume numbers followed by
    // redirect tokens are fd prefixes. The lexer doesn't consume whitespace between them.
    return true; // Will be validated in parseRedirects
}

bool Parser::parseRedirect(std::vector<std::unique_ptr<ast::InputRedirect>>& inputRedirects,
                           std::vector<std::unique_ptr<ast::OutputRedirect>>& outputRedirects,
                           std::vector<std::unique_ptr<ast::HereDocument>>& hereDocuments,
                           std::vector<std::unique_ptr<ast::HereString>>& hereStrings)
{
    TRACE_SCOPE("parseRedirect");

    // Check for optional fd prefix (e.g., "2>" for stderr)
    int fdValue = -1;
    if (_lexer.currentToken() == Token::Number)
    {
        fdValue = std::stoi(_lexer.currentLiteral());
        _lexer.nextToken();
    }

    switch (_lexer.currentToken())
    {
        case Token::Greater: {
            // > FILE (stdout redirect)
            _lexer.nextToken();
            auto target = parseParameter();
            if (!target)
                return false;
            int const sourceFd = fdValue >= 0 ? fdValue : 1; // Default: stdout
            outputRedirects.emplace_back(std::make_unique<ast::OutputRedirect>(
                std::make_unique<ast::FileDescriptor>(sourceFd), std::move(target), false));
            return true;
        }
        case Token::GreaterGreater: {
            // >> FILE (append)
            _lexer.nextToken();
            auto target = parseParameter();
            if (!target)
                return false;
            int const sourceFd = fdValue >= 0 ? fdValue : 1; // Default: stdout
            outputRedirects.emplace_back(std::make_unique<ast::OutputRedirect>(
                std::make_unique<ast::FileDescriptor>(sourceFd), std::move(target), true));
            return true;
        }
        case Token::GreaterAmp: {
            // >& (fd duplication) - could be >&2 or >&FILE
            _lexer.nextToken();
            int const sourceFd = fdValue >= 0 ? fdValue : 1; // Default: stdout

            // Check if next token is a number (fd duplication) or identifier (file)
            if (_lexer.currentToken() == Token::Number)
            {
                int const targetFd = std::stoi(_lexer.currentLiteral());
                _lexer.nextToken();
                outputRedirects.emplace_back(
                    std::make_unique<ast::OutputRedirect>(std::make_unique<ast::FileDescriptor>(sourceFd),
                                                          std::make_unique<ast::FileDescriptor>(targetFd)));
            }
            else
            {
                // Redirect to file (e.g., >& file is equivalent to > file 2>&1 in some shells)
                auto target = parseParameter();
                if (!target)
                    return false;
                outputRedirects.emplace_back(std::make_unique<ast::OutputRedirect>(
                    std::make_unique<ast::FileDescriptor>(sourceFd), std::move(target), false));
            }
            return true;
        }
        case Token::Less: {
            // < FILE (input redirect)
            _lexer.nextToken();
            auto source = parseParameter();
            if (!source)
                return false;
            int const targetFd = fdValue >= 0 ? fdValue : 0; // Default: stdin
            inputRedirects.emplace_back(std::make_unique<ast::InputRedirect>(
                std::make_unique<ast::FileDescriptor>(targetFd), std::move(source)));
            return true;
        }
        case Token::LessLess:
        case Token::LessLessDash: {
            // << DELIMITER or <<- DELIMITER (here-document)
            bool const stripTabs = _lexer.currentToken() == Token::LessLessDash;
            _lexer.nextToken();

            // Get the delimiter
            if (_lexer.currentToken() != Token::Identifier && _lexer.currentToken() != Token::String)
            {
                _report.syntaxErrorWithSuggestions(currentLocation(),
                                                   { "Provide a delimiter for the here-document" },
                                                   currentContextSnippet(),
                                                   "Expected here-document delimiter, got '{}'",
                                                   _lexer.currentLiteral());
                return false;
            }
            std::string delimiter = consumeLiteral();
            int const targetFd = fdValue >= 0 ? fdValue : 0; // Default: stdin

            // Here-document content will be parsed after the command line is complete
            // For now, store with empty content - actual content parsing is deferred
            hereDocuments.emplace_back(std::make_unique<ast::HereDocument>(
                std::make_unique<ast::FileDescriptor>(targetFd), std::move(delimiter), "", stripTabs));
            return true;
        }
        case Token::LessLessLess: {
            // <<< "string" (here-string)
            _lexer.nextToken();
            auto content = parseParameter();
            if (!content)
                return false;
            int const targetFd = fdValue >= 0 ? fdValue : 0; // Default: stdin
            hereStrings.emplace_back(std::make_unique<ast::HereString>(
                std::make_unique<ast::FileDescriptor>(targetFd), std::move(content)));
            return true;
        }
        default:
            // Not a redirect - if we consumed an fd number, this is an error
            if (fdValue >= 0)
            {
                _report.syntaxErrorWithSuggestions(currentLocation(),
                                                   { "Use > for output or < for input redirect" },
                                                   currentContextSnippet(),
                                                   "Expected redirect operator after '{}', got '{}'",
                                                   fdValue,
                                                   _lexer.currentLiteral());
                return false;
            }
            return false;
    }
}

std::unique_ptr<ast::ProgramCall> Parser::parseCall(bool piped)
{
    TRACE_SCOPE("parseCall");
    std::string program = consumeLiteral();
    std::vector<std::unique_ptr<ast::Expr>> arguments;
    std::vector<std::unique_ptr<ast::InputRedirect>> inputRedirects;
    std::vector<std::unique_ptr<ast::OutputRedirect>> outputRedirects;
    std::vector<std::unique_ptr<ast::HereDocument>> hereDocuments;
    std::vector<std::unique_ptr<ast::HereString>> hereStrings;

    // Parse arguments and redirects interleaved
    // Continue while we have parameter tokens or redirect tokens, and we're not at end of statement
    while (!isEndOfStmt() || isParameterToken())
    {
        // Check for redirect tokens
        if (isRedirectToken())
        {
            if (!parseRedirect(inputRedirects, outputRedirects, hereDocuments, hereStrings))
                break;
            continue;
        }

        // Check for number that might be fd prefix for redirect
        if (_lexer.currentToken() == Token::Number)
        {
            // Save position to potentially backtrack
            std::string const numLiteral = _lexer.currentLiteral();
            _lexer.nextToken();

            if (isRedirectToken())
            {
                // It's an fd prefix - put back and parse as redirect
                // We need to handle this differently since we've already consumed the number
                int const fdValue = std::stoi(numLiteral);

                switch (_lexer.currentToken())
                {
                    case Token::Greater: {
                        _lexer.nextToken();
                        auto target = parseParameter();
                        if (!target)
                            break;
                        outputRedirects.emplace_back(std::make_unique<ast::OutputRedirect>(
                            std::make_unique<ast::FileDescriptor>(fdValue), std::move(target), false));
                        continue;
                    }
                    case Token::GreaterGreater: {
                        _lexer.nextToken();
                        auto target = parseParameter();
                        if (!target)
                            break;
                        outputRedirects.emplace_back(std::make_unique<ast::OutputRedirect>(
                            std::make_unique<ast::FileDescriptor>(fdValue), std::move(target), true));
                        continue;
                    }
                    case Token::GreaterAmp: {
                        _lexer.nextToken();
                        if (_lexer.currentToken() == Token::Number)
                        {
                            int const targetFd = std::stoi(_lexer.currentLiteral());
                            _lexer.nextToken();
                            outputRedirects.emplace_back(std::make_unique<ast::OutputRedirect>(
                                std::make_unique<ast::FileDescriptor>(fdValue),
                                std::make_unique<ast::FileDescriptor>(targetFd)));
                        }
                        else
                        {
                            auto target = parseParameter();
                            if (!target)
                                break;
                            outputRedirects.emplace_back(std::make_unique<ast::OutputRedirect>(
                                std::make_unique<ast::FileDescriptor>(fdValue), std::move(target), false));
                        }
                        continue;
                    }
                    case Token::Less: {
                        _lexer.nextToken();
                        auto source = parseParameter();
                        if (!source)
                            break;
                        inputRedirects.emplace_back(std::make_unique<ast::InputRedirect>(
                            std::make_unique<ast::FileDescriptor>(fdValue), std::move(source)));
                        continue;
                    }
                    case Token::LessLess:
                    case Token::LessLessDash: {
                        bool const stripTabs = _lexer.currentToken() == Token::LessLessDash;
                        _lexer.nextToken();
                        if (_lexer.currentToken() != Token::Identifier
                            && _lexer.currentToken() != Token::String)
                        {
                            _report.syntaxErrorWithSuggestions(
                                currentLocation(),
                                { "Provide a delimiter for the here-document" },
                                currentContextSnippet(),
                                "Expected here-document delimiter, got '{}'",
                                _lexer.currentLiteral());
                            break;
                        }
                        std::string delimiter = consumeLiteral();
                        hereDocuments.emplace_back(std::make_unique<ast::HereDocument>(
                            std::make_unique<ast::FileDescriptor>(fdValue),
                            std::move(delimiter),
                            "",
                            stripTabs));
                        continue;
                    }
                    case Token::LessLessLess: {
                        _lexer.nextToken();
                        auto content = parseParameter();
                        if (!content)
                            break;
                        hereStrings.emplace_back(std::make_unique<ast::HereString>(
                            std::make_unique<ast::FileDescriptor>(fdValue), std::move(content)));
                        continue;
                    }
                    default: break;
                }
            }

            // Not followed by redirect - treat the number as a regular argument
            arguments.emplace_back(std::make_unique<ast::LiteralExpr>(numLiteral));
            continue;
        }

        // Regular argument
        auto arg = parseParameter();
        if (arg)
        {
            // Check for brace expansion on literal arguments
            if (auto* literal = dynamic_cast<ast::LiteralExpr*>(arg.get()))
            {
                if (containsBracePattern(literal->value))
                {
                    auto const expanded = expandBraces(literal->value);
                    for (auto const& e: expanded)
                        arguments.emplace_back(std::make_unique<ast::LiteralExpr>(e));
                    continue;
                }
            }
            arguments.emplace_back(std::move(arg));
        }
        else
            break;
    }

    CoreVM::NativeCallback const* builtinCallProcess = _lexer.currentToken() == Token::Pipe || piped
                                                           ? _runtime.find("callproc(Bs)I")
                                                           : _runtime.find("callproc(s)I");
    assert(builtinCallProcess != nullptr);

    return std::make_unique<ast::ProgramCall>(*builtinCallProcess,
                                              std::move(program),
                                              std::move(arguments),
                                              std::move(inputRedirects),
                                              std::move(outputRedirects),
                                              std::move(hereDocuments),
                                              std::move(hereStrings));
}

std::vector<std::unique_ptr<ast::Expr>> Parser::parseParameterList()
{
    TRACE_SCOPE("parseParameterList");
    std::vector<std::unique_ptr<ast::Expr>> parameters;
    while (!isEndOfStmt() && !isRedirectToken())
    {
        // Don't consume numbers that might be fd prefixes for redirects
        if (_lexer.currentToken() == Token::Number)
        {
            // This is a simplified check - in a full implementation we'd need lookahead
            // For now, treat numbers as parameters in this context
        }

        auto arg = parseParameter();
        if (arg)
            parameters.emplace_back(std::move(arg));
        else
            break;
    }
    TRACE_FMT("parsed {} parameters, follow-up token: {}", parameters.size(), _lexer.currentToken());
    return parameters;
}

std::unique_ptr<ast::SubstitutionExpr> Parser::parseCommandSubstitution()
{
    TRACE_SCOPE("parseCommandSubstitution");
    _lexer.nextToken(); // consume $(
    auto command = parseLogicalExpr();
    if (!command)
        return nullptr;
    if (_lexer.currentToken() != Token::RndClose)
    {
        _report.syntaxErrorWithSuggestions(currentLocation(),
                                           { "Add a closing ')' after the command" },
                                           currentContextSnippet(),
                                           "Expected ')' after command substitution, got '{}'",
                                           _lexer.currentLiteral());
        return nullptr;
    }
    _lexer.nextToken(); // consume )
    return std::make_unique<ast::SubstitutionExpr>(std::move(command), false);
}

std::unique_ptr<ast::SubstitutionExpr> Parser::parseBacktickSubstitution()
{
    TRACE_SCOPE("parseBacktickSubstitution");
    _lexer.nextToken(); // consume opening `
    ++_backtickNestingLevel;
    auto command = parseLogicalExpr();
    --_backtickNestingLevel;
    if (!command)
        return nullptr;
    if (_lexer.currentToken() != Token::Backtick)
    {
        _report.syntaxErrorWithSuggestions(currentLocation(),
                                           { "Add a closing backtick" },
                                           currentContextSnippet(),
                                           "Expected closing backtick, got '{}'",
                                           _lexer.currentLiteral());
        return nullptr;
    }
    _lexer.nextToken(); // consume closing `
    return std::make_unique<ast::SubstitutionExpr>(std::move(command), true);
}

std::unique_ptr<ast::CommandFileSubst> Parser::parseProcessSubstitution(ast::ProcessSubstMode mode)
{
    TRACE_SCOPE("parseProcessSubstitution");
    _lexer.nextToken(); // consume <( or >(
    auto command = parseLogicalExpr();
    if (!command)
        return nullptr;
    if (_lexer.currentToken() != Token::RndClose)
    {
        _report.syntaxErrorWithSuggestions(currentLocation(),
                                           { "Add a closing ')' after the command" },
                                           currentContextSnippet(),
                                           "Expected ')' after process substitution, got '{}'",
                                           _lexer.currentLiteral());
        return nullptr;
    }
    _lexer.nextToken(); // consume )
    return std::make_unique<ast::CommandFileSubst>(std::move(command), mode);
}

std::unique_ptr<ast::ParamExpansionExpr> Parser::parseParamExpansion()
{
    TRACE_SCOPE("parseParamExpansion");
    std::string const content = _lexer.currentLiteral();
    _lexer.nextToken();

    // Parse the content to determine operation type
    // Content format:
    //   #VAR           -> Length
    //   VAR:-default   -> DefaultValue
    //   VAR:+alt       -> AlternateValue
    //   VAR:=default   -> AssignDefault
    //   VAR:?error     -> ErrorIfUnset
    //   VAR#pattern    -> RemovePrefixShort
    //   VAR##pattern   -> RemovePrefixLong
    //   VAR%pattern    -> RemoveSuffixShort
    //   VAR%%pattern   -> RemoveSuffixLong
    //   VAR/pattern/replacement  -> ReplaceFirst
    //   VAR//pattern/replacement -> ReplaceAll

    if (content.empty())
        return nullptr;

    // Check for length operator: #VAR
    if (content[0] == '#')
    {
        std::string const variable = content.substr(1);
        return std::make_unique<ast::ParamExpansionExpr>(variable, ast::ParamExpansionOp::Length);
    }

    // Find the variable name (alphanumeric and underscore)
    size_t varEnd = 0;
    while (varEnd < content.size()
           && (std::isalnum(static_cast<unsigned char>(content[varEnd])) || content[varEnd] == '_'))
    {
        ++varEnd;
    }

    if (varEnd == 0)
        return nullptr;

    std::string const variable = content.substr(0, varEnd);
    std::string const rest = content.substr(varEnd);

    if (rest.empty())
    {
        // Just ${VAR} - but this should have been DollarBraceName
        return std::make_unique<ast::ParamExpansionExpr>(variable, ast::ParamExpansionOp::DefaultValue, "");
    }

    // Parse the operator
    if (rest.starts_with(":-"))
    {
        return std::make_unique<ast::ParamExpansionExpr>(
            variable, ast::ParamExpansionOp::DefaultValue, rest.substr(2));
    }
    else if (rest.starts_with(":+"))
    {
        return std::make_unique<ast::ParamExpansionExpr>(
            variable, ast::ParamExpansionOp::AlternateValue, rest.substr(2));
    }
    else if (rest.starts_with(":="))
    {
        return std::make_unique<ast::ParamExpansionExpr>(
            variable, ast::ParamExpansionOp::AssignDefault, rest.substr(2));
    }
    else if (rest.starts_with(":?"))
    {
        return std::make_unique<ast::ParamExpansionExpr>(
            variable, ast::ParamExpansionOp::ErrorIfUnset, rest.substr(2));
    }
    else if (rest.starts_with("##"))
    {
        return std::make_unique<ast::ParamExpansionExpr>(
            variable, ast::ParamExpansionOp::RemovePrefixLong, rest.substr(2));
    }
    else if (rest.starts_with("#"))
    {
        return std::make_unique<ast::ParamExpansionExpr>(
            variable, ast::ParamExpansionOp::RemovePrefixShort, rest.substr(1));
    }
    else if (rest.starts_with("%%"))
    {
        return std::make_unique<ast::ParamExpansionExpr>(
            variable, ast::ParamExpansionOp::RemoveSuffixLong, rest.substr(2));
    }
    else if (rest.starts_with("%"))
    {
        return std::make_unique<ast::ParamExpansionExpr>(
            variable, ast::ParamExpansionOp::RemoveSuffixShort, rest.substr(1));
    }
    else if (rest.starts_with("//"))
    {
        // ${VAR//pattern/replacement}
        auto const patternStart = 2;
        auto const slashPos = rest.find('/', patternStart);
        if (slashPos != std::string::npos)
        {
            return std::make_unique<ast::ParamExpansionExpr>(
                variable,
                ast::ParamExpansionOp::ReplaceAll,
                rest.substr(patternStart, slashPos - patternStart),
                rest.substr(slashPos + 1));
        }
        return std::make_unique<ast::ParamExpansionExpr>(
            variable, ast::ParamExpansionOp::ReplaceAll, rest.substr(patternStart), "");
    }
    else if (rest.starts_with("/"))
    {
        // ${VAR/pattern/replacement}
        auto const patternStart = 1;
        auto const slashPos = rest.find('/', patternStart);
        if (slashPos != std::string::npos)
        {
            return std::make_unique<ast::ParamExpansionExpr>(
                variable,
                ast::ParamExpansionOp::ReplaceFirst,
                rest.substr(patternStart, slashPos - patternStart),
                rest.substr(slashPos + 1));
        }
        return std::make_unique<ast::ParamExpansionExpr>(
            variable, ast::ParamExpansionOp::ReplaceFirst, rest.substr(patternStart), "");
    }

    // Unknown operator - treat as default value with empty string
    return std::make_unique<ast::ParamExpansionExpr>(variable, ast::ParamExpansionOp::DefaultValue, "");
}

std::unique_ptr<ast::TildeExpr> Parser::parseTildeExpansion()
{
    TRACE_SCOPE("parseTildeExpansion");
    std::string const literal = _lexer.currentLiteral(); // May contain user and/or path suffix
    _lexer.nextToken();                                  // consume ~ token

    // Parse the literal to extract user and suffix
    // Formats: "" (just ~), "user", "/path" (~/path), "user/path"
    std::string user;
    std::string suffix;

    if (literal.empty())
    {
        // Just ~
    }
    else if (literal[0] == '~')
    {
        // Literal starts with ~ - check for ~/path
        auto const slashPos = literal.find('/');
        if (slashPos == 1)
        {
            // ~/path format
            suffix = literal.substr(1); // Include the /
        }
        else if (slashPos != std::string::npos)
        {
            // ~user/path format (where literal is "~user/path")
            user = literal.substr(1, slashPos - 1);
            suffix = literal.substr(slashPos);
        }
        else
        {
            // ~user format (where literal is "~user")
            user = literal.substr(1);
        }
    }
    else
    {
        // Literal doesn't start with ~ - it's "user" or "user/path"
        auto const slashPos = literal.find('/');
        if (slashPos != std::string::npos)
        {
            user = literal.substr(0, slashPos);
            suffix = literal.substr(slashPos);
        }
        else
        {
            user = literal;
        }
    }

    return std::make_unique<ast::TildeExpr>(std::move(user), std::move(suffix));
}

std::unique_ptr<ast::ArithExpansionExpr> Parser::parseArithmeticExpansion()
{
    TRACE_SCOPE("parseArithmeticExpansion");
    _lexer.nextToken(); // consume $((

    auto expr = parseArithOr();
    if (!expr)
        return nullptr;

    if (_lexer.currentToken() != Token::DblRndClose)
    {
        _report.syntaxErrorWithSuggestions(
            currentLocation(), {}, currentContextSnippet(), "Expected ')) to close arithmetic expansion");
        return nullptr;
    }
    _lexer.nextToken(); // consume ))

    return std::make_unique<ast::ArithExpansionExpr>(std::move(expr));
}

std::unique_ptr<ast::ArithExpr> Parser::parseArithOr()
{
    auto left = parseArithAnd();
    if (!left)
        return nullptr;

    while (_lexer.currentToken() == Token::PipePipe)
    {
        _lexer.nextToken();
        auto right = parseArithAnd();
        if (!right)
            return nullptr;
        left = std::make_unique<ast::ArithBinaryExpr>(ast::ArithOp::Or, std::move(left), std::move(right));
    }
    return left;
}

std::unique_ptr<ast::ArithExpr> Parser::parseArithAnd()
{
    auto left = parseArithBitOr();
    if (!left)
        return nullptr;

    while (_lexer.currentToken() == Token::AmpAmp)
    {
        _lexer.nextToken();
        auto right = parseArithBitOr();
        if (!right)
            return nullptr;
        left = std::make_unique<ast::ArithBinaryExpr>(ast::ArithOp::And, std::move(left), std::move(right));
    }
    return left;
}

std::unique_ptr<ast::ArithExpr> Parser::parseArithBitOr()
{
    auto left = parseArithBitXor();
    if (!left)
        return nullptr;

    while (_lexer.currentToken() == Token::Pipe)
    {
        _lexer.nextToken();
        auto right = parseArithBitXor();
        if (!right)
            return nullptr;
        left = std::make_unique<ast::ArithBinaryExpr>(ast::ArithOp::BitOr, std::move(left), std::move(right));
    }
    return left;
}

std::unique_ptr<ast::ArithExpr> Parser::parseArithBitXor()
{
    auto left = parseArithBitAnd();
    if (!left)
        return nullptr;

    // ^ is not yet tokenized specially, we check as identifier
    while (_lexer.currentToken() == Token::Identifier && _lexer.currentLiteral() == "^")
    {
        _lexer.nextToken();
        auto right = parseArithBitAnd();
        if (!right)
            return nullptr;
        left =
            std::make_unique<ast::ArithBinaryExpr>(ast::ArithOp::BitXor, std::move(left), std::move(right));
    }
    return left;
}

std::unique_ptr<ast::ArithExpr> Parser::parseArithBitAnd()
{
    auto left = parseArithEquality();
    if (!left)
        return nullptr;

    // Single & is not a distinct token yet, so we handle it within identifiers or skip for now
    // In shell arithmetic, & is bitwise AND but our lexer doesn't separate it
    return left;
}

std::unique_ptr<ast::ArithExpr> Parser::parseArithEquality()
{
    auto left = parseArithComparison();
    if (!left)
        return nullptr;

    while (true)
    {
        // Check for == or != as identifiers (since lexer doesn't tokenize these specially)
        if (_lexer.currentToken() == Token::Identifier)
        {
            auto const& lit = _lexer.currentLiteral();
            if (lit == "==")
            {
                _lexer.nextToken();
                auto right = parseArithComparison();
                if (!right)
                    return nullptr;
                left = std::make_unique<ast::ArithBinaryExpr>(
                    ast::ArithOp::Eq, std::move(left), std::move(right));
                continue;
            }
            else if (lit == "!=")
            {
                _lexer.nextToken();
                auto right = parseArithComparison();
                if (!right)
                    return nullptr;
                left = std::make_unique<ast::ArithBinaryExpr>(
                    ast::ArithOp::Ne, std::move(left), std::move(right));
                continue;
            }
        }
        break;
    }
    return left;
}

std::unique_ptr<ast::ArithExpr> Parser::parseArithComparison()
{
    auto left = parseArithShift();
    if (!left)
        return nullptr;

    while (true)
    {
        ast::ArithOp op;
        bool found = false;

        switch (_lexer.currentToken())
        {
            case Token::Less:
                op = ast::ArithOp::Lt;
                found = true;
                break;
            case Token::Greater:
                op = ast::ArithOp::Gt;
                found = true;
                break;
            case Token::LessEqual:
                op = ast::ArithOp::Le;
                found = true;
                break;
            case Token::GreaterEqual:
                op = ast::ArithOp::Ge;
                found = true;
                break;
            default:
                // Check for <= and >= as identifiers
                if (_lexer.currentToken() == Token::Identifier)
                {
                    auto const& lit = _lexer.currentLiteral();
                    if (lit == "<=")
                    {
                        op = ast::ArithOp::Le;
                        found = true;
                    }
                    else if (lit == ">=")
                    {
                        op = ast::ArithOp::Ge;
                        found = true;
                    }
                }
                break;
        }

        if (!found)
            break;

        _lexer.nextToken();
        auto right = parseArithShift();
        if (!right)
            return nullptr;
        left = std::make_unique<ast::ArithBinaryExpr>(op, std::move(left), std::move(right));
    }
    return left;
}

std::unique_ptr<ast::ArithExpr> Parser::parseArithShift()
{
    auto left = parseArithAddSub();
    if (!left)
        return nullptr;

    while (true)
    {
        ast::ArithOp op;
        bool found = false;

        // << is LessLess token, >> would be GreaterGreater
        switch (_lexer.currentToken())
        {
            case Token::LessLess:
                op = ast::ArithOp::Shl;
                found = true;
                break;
            case Token::GreaterGreater:
                op = ast::ArithOp::Shr;
                found = true;
                break;
            default: break;
        }

        if (!found)
            break;

        _lexer.nextToken();
        auto right = parseArithAddSub();
        if (!right)
            return nullptr;
        left = std::make_unique<ast::ArithBinaryExpr>(op, std::move(left), std::move(right));
    }
    return left;
}

std::unique_ptr<ast::ArithExpr> Parser::parseArithAddSub()
{
    auto left = parseArithMulDiv();
    if (!left)
        return nullptr;

    while (true)
    {
        ast::ArithOp op;
        bool found = false;

        if (_lexer.currentToken() == Token::Identifier)
        {
            auto const& lit = _lexer.currentLiteral();
            if (lit == "+")
            {
                op = ast::ArithOp::Add;
                found = true;
            }
            else if (lit == "-")
            {
                op = ast::ArithOp::Sub;
                found = true;
            }
        }

        if (!found)
            break;

        _lexer.nextToken();
        auto right = parseArithMulDiv();
        if (!right)
            return nullptr;
        left = std::make_unique<ast::ArithBinaryExpr>(op, std::move(left), std::move(right));
    }
    return left;
}

std::unique_ptr<ast::ArithExpr> Parser::parseArithMulDiv()
{
    auto left = parseArithPow();
    if (!left)
        return nullptr;

    while (true)
    {
        ast::ArithOp op;
        bool found = false;

        if (_lexer.currentToken() == Token::Identifier)
        {
            auto const& lit = _lexer.currentLiteral();
            if (lit == "*")
            {
                op = ast::ArithOp::Mul;
                found = true;
            }
            else if (lit == "/")
            {
                op = ast::ArithOp::Div;
                found = true;
            }
            else if (lit == "%")
            {
                op = ast::ArithOp::Mod;
                found = true;
            }
        }

        if (!found)
            break;

        _lexer.nextToken();
        auto right = parseArithPow();
        if (!right)
            return nullptr;
        left = std::make_unique<ast::ArithBinaryExpr>(op, std::move(left), std::move(right));
    }
    return left;
}

std::unique_ptr<ast::ArithExpr> Parser::parseArithPow()
{
    auto left = parseArithUnary();
    if (!left)
        return nullptr;

    if (_lexer.currentToken() == Token::Identifier && _lexer.currentLiteral() == "**")
    {
        _lexer.nextToken();
        auto right = parseArithPow(); // Right associative: 2**3**4 = 2**(3**4)
        if (!right)
            return nullptr;
        return std::make_unique<ast::ArithBinaryExpr>(ast::ArithOp::Pow, std::move(left), std::move(right));
    }
    return left;
}

std::unique_ptr<ast::ArithExpr> Parser::parseArithUnary()
{
    if (_lexer.currentToken() == Token::Not)
    {
        _lexer.nextToken();
        auto operand = parseArithUnary();
        if (!operand)
            return nullptr;
        return std::make_unique<ast::ArithUnaryExpr>(ast::ArithOp::Not, std::move(operand));
    }

    if (_lexer.currentToken() == Token::Identifier)
    {
        auto const& lit = _lexer.currentLiteral();
        if (lit == "-")
        {
            _lexer.nextToken();
            auto operand = parseArithUnary();
            if (!operand)
                return nullptr;
            return std::make_unique<ast::ArithUnaryExpr>(ast::ArithOp::Neg, std::move(operand));
        }
        else if (lit.size() > 1 && lit[0] == '-' && std::isdigit(static_cast<unsigned char>(lit[1])))
        {
            // Handle tokens like "-5" as a negative number literal
            int64_t value = 0;
            // Parse the whole number including the minus sign
            auto [ptr, ec] = std::from_chars(lit.data(), lit.data() + lit.size(), value);
            if (ec == std::errc() && ptr == lit.data() + lit.size())
            {
                _lexer.nextToken();
                return std::make_unique<ast::ArithLiteralExpr>(value);
            }
        }
        else if (lit == "~")
        {
            _lexer.nextToken();
            auto operand = parseArithUnary();
            if (!operand)
                return nullptr;
            return std::make_unique<ast::ArithUnaryExpr>(ast::ArithOp::BitNot, std::move(operand));
        }
    }

    return parseArithPrimary();
}

std::unique_ptr<ast::ArithExpr> Parser::parseArithPrimary()
{
    // Number literal
    if (_lexer.currentToken() == Token::Number)
    {
        auto const& lit = _lexer.currentLiteral();
        int64_t value = 0;
        auto [ptr, ec] = std::from_chars(lit.data(), lit.data() + lit.size(), value);
        if (ec != std::errc())
            value = 0;
        _lexer.nextToken();
        return std::make_unique<ast::ArithLiteralExpr>(value);
    }

    // Identifier-style number (operators like + are tokenized as identifiers with the number)
    if (_lexer.currentToken() == Token::Identifier)
    {
        auto const& lit = _lexer.currentLiteral();
        // Check if it's a number
        int64_t value = 0;
        auto [ptr, ec] = std::from_chars(lit.data(), lit.data() + lit.size(), value);
        if (ec == std::errc() && ptr == lit.data() + lit.size())
        {
            _lexer.nextToken();
            return std::make_unique<ast::ArithLiteralExpr>(value);
        }

        // Otherwise it's a variable reference
        std::string name = lit;
        _lexer.nextToken();
        return std::make_unique<ast::ArithVarExpr>(std::move(name));
    }

    // Variable reference $VAR or ${VAR}
    if (_lexer.currentToken() == Token::DollarName || _lexer.currentToken() == Token::DollarBraceName)
    {
        std::string name = _lexer.currentLiteral();
        _lexer.nextToken();
        return std::make_unique<ast::ArithVarExpr>(std::move(name));
    }

    // Parenthesized expression
    if (_lexer.currentToken() == Token::RndOpen)
    {
        _lexer.nextToken();
        auto expr = parseArithOr();
        if (!expr)
            return nullptr;
        if (_lexer.currentToken() != Token::RndClose)
        {
            _report.syntaxErrorWithSuggestions(
                currentLocation(), {}, currentContextSnippet(), "Expected ')' in arithmetic expression");
            return nullptr;
        }
        _lexer.nextToken();
        return expr;
    }

    _report.syntaxErrorWithSuggestions(currentLocation(),
                                       {},
                                       currentContextSnippet(),
                                       "Expected number or variable in arithmetic expression");
    return nullptr;
}

std::unique_ptr<ast::Expr> Parser::parseParameter()
{
    TRACE_FMT("parseParameter: {} \"{}\"", _lexer.currentToken(), _lexer.currentLiteral());
    switch (_lexer.currentToken())
    {
        case Token::String:
        case Token::Number: return std::make_unique<ast::LiteralExpr>(consumeLiteral());
        case Token::Identifier: {
            auto const& literal = _lexer.currentLiteral();
            if (containsGlobChars(literal))
                return std::make_unique<ast::GlobExpr>(consumeLiteral());
            return std::make_unique<ast::LiteralExpr>(consumeLiteral());
        }
        case Token::DollarName:
            return std::make_unique<ast::VariableExpr>(consumeLiteral(), ast::VariableType::Named, false);
        case Token::DollarBraceName:
            return std::make_unique<ast::VariableExpr>(consumeLiteral(), ast::VariableType::Named, true);
        case Token::DollarBraceParam: return parseParamExpansion();
        case Token::DollarQuestion:
            _lexer.nextToken();
            return std::make_unique<ast::VariableExpr>("?", ast::VariableType::ExitStatus, false);
        case Token::DollarDollar:
            _lexer.nextToken();
            return std::make_unique<ast::VariableExpr>("$", ast::VariableType::ProcessId, false);
        case Token::DollarNot:
            _lexer.nextToken();
            return std::make_unique<ast::VariableExpr>("!", ast::VariableType::BackgroundId, false);
        case Token::DollarNumber:
            return std::make_unique<ast::VariableExpr>(
                consumeLiteral(), ast::VariableType::Positional, false);
        case Token::DollarRndOpen: return parseCommandSubstitution();
        case Token::DollarDblRndOpen: return parseArithmeticExpansion();
        case Token::Backtick: return parseBacktickSubstitution();
        case Token::LessRndOpen: return parseProcessSubstitution(ast::ProcessSubstMode::Read);
        case Token::GreaterRndOpen: return parseProcessSubstitution(ast::ProcessSubstMode::Write);
        case Token::Tilde: return parseTildeExpansion();
        default:
            _report.syntaxErrorWithSuggestions(currentLocation(),
                                               {},
                                               currentContextSnippet(),
                                               "Expected parameter, got '{}'",
                                               _lexer.currentLiteral());
            return nullptr;
    }
}

std::unique_ptr<ast::Statement> Parser::parsePrimaryStmt()
{
    TRACE_SCOPE("parsePrimaryStmt");

    // Handle builtin commands that don't participate in pipelines
    if (_lexer.isDirective("exit"))
    {
        _lexer.nextToken();
        std::unique_ptr<ast::Expr> code;
        if (!isEndOfStmt())
            code = parseParameter();
        assert(_runtime.find("exit(I)V") != nullptr);
        return std::make_unique<ast::BuiltinExitStmt>(*_runtime.find("exit(I)V"), std::move(code));
    }
    else if (_lexer.isDirective("true"))
    {
        _lexer.nextToken();
        return std::make_unique<ast::BuiltinTrueStmt>(*_runtime.find("true()B"));
    }
    else if (_lexer.isDirective("false"))
    {
        _lexer.nextToken();
        return std::make_unique<ast::BuiltinFalseStmt>(*_runtime.find("false()B"));
    }
    else if (_lexer.isDirective("read"))
    {
        _lexer.nextToken();
        std::vector<std::unique_ptr<ast::Expr>> parameters = parseParameterList();
        CoreVM::NativeCallback const& callback = *_runtime.find(parameters.empty() ? "read()S" : "read(s)S");
        return std::make_unique<ast::BuiltinReadStmt>(callback, std::move(parameters));
    }
    else if (_lexer.isDirective("export"))
    {
        _lexer.nextToken();
        auto name = consumeLiteral();
        return std::make_unique<ast::BuiltinExportStmt>(*_runtime.find("export(S)V"), name);
    }
    else if (_lexer.isDirective("set"))
    {
        _lexer.nextToken();
        auto name = parseParameter();
        auto value = parseParameter();
        return std::make_unique<ast::BuiltinSetStmt>(
            *_runtime.find("set(SS)B"), std::move(name), std::move(value));
    }
    else if (_lexer.isDirective("cd"))
    {
        _lexer.nextToken();
        if (isEndOfStmt())
            return std::make_unique<ast::BuiltinChDirStmt>(*_runtime.find("cd()B"), nullptr);
        else
        {
            auto param = parseParameter();
            return std::make_unique<ast::BuiltinChDirStmt>(*_runtime.find("cd(S)B"), std::move(param));
        }
    }
    else if (_lexer.isDirective("unset"))
    {
        _lexer.nextToken();
        auto name = consumeLiteral();
        return std::make_unique<ast::BuiltinUnsetStmt>(*_runtime.find("unset(S)B"), name);
    }
    else if (_lexer.isDirective("jobs"))
    {
        _lexer.nextToken();
        return std::make_unique<ast::BuiltinJobsStmt>(*_runtime.find("jobs()I"));
    }
    else if (_lexer.isDirective("fg"))
    {
        _lexer.nextToken();
        if (isEndOfStmt())
            return std::make_unique<ast::BuiltinFgStmt>(*_runtime.find("fg()I"));
        else
        {
            auto jobId = parseParameter();
            return std::make_unique<ast::BuiltinFgStmt>(*_runtime.find("fg(I)I"), std::move(jobId));
        }
    }
    else if (_lexer.isDirective("bg"))
    {
        _lexer.nextToken();
        if (isEndOfStmt())
            return std::make_unique<ast::BuiltinBgStmt>(*_runtime.find("bg()I"));
        else
        {
            auto jobId = parseParameter();
            return std::make_unique<ast::BuiltinBgStmt>(*_runtime.find("bg(I)I"), std::move(jobId));
        }
    }
    else if (_lexer.isDirective("wait"))
    {
        _lexer.nextToken();
        if (isEndOfStmt())
            return std::make_unique<ast::BuiltinWaitStmt>(*_runtime.find("wait()I"));
        else
        {
            auto jobId = parseParameter();
            return std::make_unique<ast::BuiltinWaitStmt>(*_runtime.find("wait(I)I"), std::move(jobId));
        }
    }
    else if (_lexer.isDirective("bind"))
    {
        _lexer.nextToken();
        if (isEndOfStmt())
        {
            // bind with no arguments - list all bindings
            return std::make_unique<ast::BuiltinBindStmt>(*_runtime.find("bind()I"));
        }
        else
        {
            // bind with arguments
            std::vector<std::unique_ptr<ast::Expr>> args;
            while (!isEndOfStmt())
            {
                auto arg = parseParameter();
                if (!arg)
                    break;
                args.push_back(std::move(arg));
            }
            return std::make_unique<ast::BuiltinBindStmt>(*_runtime.find("bind(S+)I"), std::move(args));
        }
    }
    else if (_lexer.isDirective("which"))
    {
        _lexer.nextToken();
        if (isEndOfStmt())
        {
            // which with no arguments - show help
            assert(_runtime.find("which()I") != nullptr);
            return std::make_unique<ast::BuiltinWhichStmt>(*_runtime.find("which()I"));
        }
        else
        {
            // which with arguments (program names and flags)
            std::vector<std::unique_ptr<ast::Expr>> args;
            while (!isEndOfStmt())
            {
                auto arg = parseParameter();
                if (!arg)
                    break;
                args.push_back(std::move(arg));
            }
            assert(_runtime.find("which(s)I") != nullptr);
            return std::make_unique<ast::BuiltinWhichStmt>(*_runtime.find("which(s)I"), std::move(args));
        }
    }
    else
    {
        // External command or pipeline
        return parseCallPipeline();
    }
}

std::unique_ptr<ast::Statement> Parser::parseLogicalExpr()
{
    TRACE_SCOPE("parseLogicalExpr");

    auto left = parsePrimaryStmt();
    if (!left)
        return nullptr;

    while (_lexer.currentToken() == Token::AmpAmp || _lexer.currentToken() == Token::PipePipe)
    {
        auto const op = _lexer.currentToken();
        _lexer.nextToken();

        auto right = parsePrimaryStmt();
        if (!right)
        {
            _report.syntaxErrorWithSuggestions(currentLocation(),
                                               { "Add a command after the operator" },
                                               currentContextSnippet(),
                                               "Expected command after '{}'",
                                               op == Token::AmpAmp ? "&&" : "||");
            return nullptr;
        }

        if (op == Token::AmpAmp)
            left = std::make_unique<ast::LogicalAndStmt>(std::move(left), std::move(right));
        else
            left = std::make_unique<ast::LogicalOrStmt>(std::move(left), std::move(right));
    }

    return left;
}

std::unique_ptr<ast::Statement> Parser::parseCallPipeline()
{
    TRACE_SCOPE("parseCallPipeline");

    auto call = parseCall();
    if (!call)
        return nullptr;

    if (_lexer.currentToken() != Token::Pipe)
    {
        // Check for trailing & (background execution)
        bool const background = _lexer.currentToken() == Token::Ampersand;
        if (background)
            _lexer.nextToken(); // consume &

        // Single command, optionally backgrounded
        if (background)
        {
            std::vector<std::unique_ptr<ast::ProgramCall>> calls;
            calls.emplace_back(std::move(call));
            return std::make_unique<ast::CallPipeline>(std::move(calls), true);
        }
        return call;
    }

    std::vector<std::unique_ptr<ast::ProgramCall>> calls;
    calls.emplace_back(std::move(call));
    while (_lexer.currentToken() == Token::Pipe)
    {
        _lexer.nextToken();
        TRACE_FMT("Parsing call pipeline item (NT: {})", _lexer.currentLiteral());
        if (auto nextCall = parseCall(true); nextCall)
        {
            calls.emplace_back(std::move(nextCall));
            TRACE_FMT("Parsed call pipeline item: {} (NT: {})",
                      ast::ASTPrinter::print(*calls.back()),
                      _lexer.currentLiteral());
        }
    }

    // Check for trailing & (background execution for pipeline)
    bool const background = _lexer.currentToken() == Token::Ampersand;
    if (background)
        _lexer.nextToken(); // consume &

    return std::make_unique<ast::CallPipeline>(std::move(calls), background);
}

bool Parser::tryConsumeToken(Token token)
{
    if (_lexer.currentToken() != token)
        return false;
    _lexer.nextToken();
    return true;
}

bool Parser::consumeOneOf(Token token)
{
    if (_lexer.currentToken() != token)
        return false;
    _lexer.nextToken();
    return true;
}

void Parser::consumeDirective(const std::string& directive)
{
    if (_lexer.isDirective(directive))
        _lexer.nextToken();
    else
        _report.syntaxErrorWithSuggestions(currentLocation(),
                                           {},
                                           currentContextSnippet(),
                                           "Expected '{}' but got '{}'",
                                           directive,
                                           _lexer.currentLiteral());
}

bool Parser::containsGlobChars(std::string_view s)
{
    for (size_t i = 0; i < s.size(); ++i)
    {
        switch (s[i])
        {
            case '*':
            case '?': return true;
            case '[': {
                // Check for valid bracket expression [...]
                auto const close = s.find(']', i + 1);
                if (close != std::string_view::npos && close > i + 1)
                    return true;
                break;
            }
            default: break;
        }
    }
    return false;
}

bool Parser::containsBracePattern(std::string_view s)
{
    size_t braceDepth = 0;
    bool foundComma = false;
    bool foundDotDot = false;

    for (size_t i = 0; i < s.size(); ++i)
    {
        char const c = s[i];
        if (c == '{')
        {
            ++braceDepth;
        }
        else if (c == '}' && braceDepth > 0)
        {
            --braceDepth;
            if (braceDepth == 0 && (foundComma || foundDotDot))
                return true;
        }
        else if (braceDepth == 1)
        {
            if (c == ',')
                foundComma = true;
            else if (c == '.' && i + 1 < s.size() && s[i + 1] == '.')
                foundDotDot = true;
        }
    }
    return false;
}

size_t Parser::findMatchingBrace(std::string_view s, size_t start)
{
    int depth = 1;
    for (size_t i = start + 1; i < s.size(); ++i)
    {
        if (s[i] == '{')
            ++depth;
        else if (s[i] == '}')
        {
            --depth;
            if (depth == 0)
                return i;
        }
    }
    return std::string_view::npos;
}

std::vector<std::string> Parser::splitBraceItems(std::string_view content)
{
    std::vector<std::string> items;
    std::string current;
    int depth = 0;

    for (char const c: content)
    {
        if (c == '{')
        {
            ++depth;
            current += c;
        }
        else if (c == '}')
        {
            --depth;
            current += c;
        }
        else if (c == ',' && depth == 0)
        {
            items.push_back(std::move(current));
            current.clear();
        }
        else
        {
            current += c;
        }
    }

    if (!current.empty() || !items.empty())
        items.push_back(std::move(current));

    return items;
}

std::vector<std::string> Parser::expandRange(std::string_view range)
{
    // Find ".." separator
    auto const dotPos = range.find("..");
    if (dotPos == std::string_view::npos)
        return { std::string(range) };

    auto const startStr = range.substr(0, dotPos);
    auto const endStr = range.substr(dotPos + 2);

    if (startStr.empty() || endStr.empty())
        return { std::string(range) };

    std::vector<std::string> result;

    // Check for numeric range
    int startNum = 0;
    int endNum = 0;
    auto const startResult = std::from_chars(startStr.data(), startStr.data() + startStr.size(), startNum);
    auto const endResult = std::from_chars(endStr.data(), endStr.data() + endStr.size(), endNum);

    if (startResult.ec == std::errc {} && endResult.ec == std::errc {}
        && startResult.ptr == startStr.data() + startStr.size()
        && endResult.ptr == endStr.data() + endStr.size())
    {
        // Numeric range
        if (startNum <= endNum)
        {
            for (int i = startNum; i <= endNum; ++i)
                result.push_back(std::to_string(i));
        }
        else
        {
            for (int i = startNum; i >= endNum; --i)
                result.push_back(std::to_string(i));
        }
        return result;
    }

    // Check for single character alphabetic range
    if (startStr.size() == 1 && endStr.size() == 1 && std::isalpha(static_cast<unsigned char>(startStr[0]))
        && std::isalpha(static_cast<unsigned char>(endStr[0])))
    {
        char const startChar = startStr[0];
        char const endChar = endStr[0];

        if (startChar <= endChar)
        {
            for (char c = startChar; c <= endChar; ++c)
                result.push_back(std::string(1, c));
        }
        else
        {
            for (char c = startChar; c >= endChar; --c)
                result.push_back(std::string(1, c));
        }
        return result;
    }

    // Not a valid range, return as-is
    return { std::string(range) };
}

std::vector<std::string> Parser::expandBraces(std::string const& input)
{
    // Find the first brace pattern
    auto const braceStart = input.find('{');
    if (braceStart == std::string::npos)
        return { input };

    auto const braceEnd = findMatchingBrace(input, braceStart);
    if (braceEnd == std::string::npos)
        return { input };

    std::string const prefix = input.substr(0, braceStart);
    std::string const content = input.substr(braceStart + 1, braceEnd - braceStart - 1);
    std::string const suffix = input.substr(braceEnd + 1);

    std::vector<std::string> expansions;

    // Check if it's a range pattern (contains ".." but no comma at depth 0)
    if (content.find("..") != std::string::npos && content.find(',') == std::string::npos)
    {
        auto const rangeExpansion = expandRange(content);
        for (auto const& item: rangeExpansion)
            expansions.push_back(prefix + item + suffix);
    }
    else
    {
        // Comma-separated list
        auto const items = splitBraceItems(content);
        for (auto const& item: items)
            expansions.push_back(prefix + item + suffix);
    }

    // Recursively expand any remaining brace patterns
    std::vector<std::string> result;
    for (auto const& expanded: expansions)
    {
        if (containsBracePattern(expanded))
        {
            auto const nested = expandBraces(expanded);
            result.insert(result.end(), nested.begin(), nested.end());
        }
        else
        {
            result.push_back(expanded);
        }
    }

    return result;
}

} // namespace endo
