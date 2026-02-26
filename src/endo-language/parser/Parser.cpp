// SPDX-License-Identifier: Apache-2.0
#include <endo-language/LogCategories.hpp>
#include <endo-language/LogConfig.hpp>
#include <endo-language/ScopedLogger.hpp>
#include <endo-language/ast/AST.hpp>
#include <endo-language/ast/ASTPrinter.hpp>
#include <endo-language/lexer/Lexer.hpp>
#include <endo-language/parser/DiagnosticsAdapter.hpp>
#include <endo-language/parser/Parser.hpp>

#include <CoreVM/CoreVM.hpp>

#include <crispy/utils.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <memory>
#include <optional>
#include <ranges>
#include <unordered_set>

// Use centralized log category from LogCategories.hpp
inline auto& parserLog()
{
    return endo::log::parser();
}

#define TRACE_SCOPE(message) ScopedLogger _logger { message, parserLog() };
#ifdef __EMSCRIPTEN__
    #define TRACE_FMT(message, ...) ((void) 0)
    #define TRACE(message)          ((void) 0)
#else
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
#endif

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

void Parser::setKnownFSharpFunctions(std::unordered_set<std::string> names)
{
    _knownFSharpFunctions = std::move(names);
}

void Parser::setKnownVariadicFunctions(std::unordered_set<std::string> names)
{
    _knownVariadicFunctions = std::move(names);
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
        || _lexer.currentToken() == Token::ForwardPipe
        || _lexer.currentToken() == Token::Semicolon
        || _lexer.currentToken() == Token::DblSemicolon
        || _lexer.currentToken() == Token::AmpAmp
        || _lexer.currentToken() == Token::PipePipe
        || _lexer.currentToken() == Token::RndClose
        || _lexer.currentToken() == Token::Ampersand
        || _lexer.currentToken() == Token::As)
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
        case Token::Tilde:
        case Token::DblQuoteStart:
        case Token::FStringStart:
        case Token::Ellipsis: return true;
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

std::unique_ptr<ast::Statement> Parser::parseIndentedBlock(size_t referenceColumn)
{
    TRACE_SCOPE("parseIndentedBlock");
    auto scope = std::make_unique<ast::CompoundStmt>();

    // Consume leading separators (newline after 'do')
    consumeUntilNotOneOf(Token::Semicolon, Token::LineFeed);

    while (_lexer.currentToken() != Token::EndOfInput)
    {
        // Offside rule: if current token is at or before the reference column, stop
        if (currentTokenColumn() <= referenceColumn)
        {
            _lexer.pushBackToken(Token::LineFeed, "\n");
            break;
        }

        auto stmt = parseStmt();
        if (!stmt)
            break;

        TRACE_FMT("parseIndentedBlock: parsed statement: {}", ast::ASTPrinter::print(*stmt));
        scope->statements.emplace_back(std::move(stmt));

        // Consume separators between statements
        bool sawLineFeed = false;
        while (_lexer.currentToken() == Token::LineFeed || _lexer.currentToken() == Token::Semicolon)
        {
            if (_lexer.currentToken() == Token::LineFeed)
                sawLineFeed = true;
            _lexer.nextToken();
        }

        if (_lexer.currentToken() == Token::EndOfInput)
            break;

        // If we crossed a line boundary, check indentation
        if (sawLineFeed && currentTokenColumn() <= referenceColumn)
        {
            _lexer.pushBackToken(Token::LineFeed, "\n");
            break;
        }
    }

    return scope;
}

std::unique_ptr<ast::Statement> Parser::parseStmt()
{
    TRACE_SCOPE("parseStmt");
    switch (_lexer.currentToken())
    {
        case Token::Let:
            // F# style let binding
            return parseLet();
        case Token::Ampersand: {
            // Shell-first execution: `& cmd args...` bypasses F# function bindings
            // Parse as a ShellCommandExpr wrapped in ExprStmt (statement-level = normal I/O)
            _lexer.enterFSharpExpr();
            auto expr = parseShellCommandExpr();
            _lexer.leaveFSharpExpr();
            if (!expr)
                return nullptr;
            return std::make_unique<ast::ExprStmt>(std::move(expr));
        }
        case Token::DollarRndOpen: {
            // Command substitution at statement level: $(whoami) |> string_length |> print
            _lexer.enterFSharpExpr();
            auto expr = parseFSharpExpr();
            _lexer.leaveFSharpExpr();
            if (!expr)
                return nullptr;
            return std::make_unique<ast::ExprStmt>(std::move(expr), /*displayResult=*/_autoDisplay);
        }
        case Token::Type:
            // Record type definition: type Person = { name: str; age: int }
            return parseTypeDefinition();
        case Token::Match: {
            // Standalone match expression as a statement
            _lexer.enterFSharpExpr();
            auto expr = parseMatch();
            _lexer.leaveFSharpExpr();
            if (!expr)
                return nullptr;
            return std::make_unique<ast::ExprStmt>(std::move(expr), /*displayResult=*/_autoDisplay);
        }
        case Token::String:
        case Token::Identifier:
            if (_lexer.isDirective("if"))
            {
                // F# if-then-else expression at statement level
                _lexer.enterFSharpExpr();
                auto expr = parseFSharpExpr();
                _lexer.leaveFSharpExpr();
                if (!expr)
                    return nullptr;
                return std::make_unique<ast::ExprStmt>(std::move(expr), /*displayResult=*/_autoDisplay);
            }
            else if (_lexer.isDirective("while"))
                return parseWhile();
            else if (_lexer.isDirective("for"))
                return parseFor();
            else if (_lexer.isDirective("break"))
                return parseBreak();
            else if (_lexer.isDirective("continue"))
                return parseContinue();
            else if (_lexer.currentLiteral() == "register_completer")
            {
                // register_completer "cmd" funcName — parse as F# application for compile-time verification
                _lexer.enterFSharpExpr();
                auto expr = parseFSharpApplication();
                _lexer.leaveFSharpExpr();
                if (!expr)
                    return nullptr;
                return std::make_unique<ast::ExprStmt>(std::move(expr));
            }
            else if (_lexer.currentLiteral() == "print" || _lexer.currentLiteral() == "println"
                     || _lexer.currentLiteral() == "each")
            {
                // F# style print/println/each functions - parse as F# expression wrapped in ExprStmt
                _lexer.enterFSharpExpr();
                auto expr = parseFSharpApplication();
                _lexer.leaveFSharpExpr();
                if (!expr)
                    return nullptr;
                return std::make_unique<ast::ExprStmt>(std::move(expr));
            }
            else if (_lexer.currentLiteral() == "rand")
            {
                // F# style rand builtin - parse as F# expression with optional |> pipeline
                _lexer.enterFSharpExpr();
                auto expr = parseFSharpApplication();
                _lexer.leaveFSharpExpr();
                if (!expr)
                    return nullptr;
                // Check for trailing |> pipeline (e.g., rand 1 10 |> fun n -> ...)
                if (_lexer.currentToken() == Token::ForwardPipe)
                {
                    _lexer.enterFSharpExpr();
                    _lexer.nextToken(); // consume first |>
                    auto step = parseFSharpComposition();
                    if (!step)
                    {
                        _lexer.leaveFSharpExpr();
                        return nullptr;
                    }
                    expr = std::make_unique<ast::PipelineExpr>(std::move(expr), std::move(step));
                    // Continue chaining |> left-associatively (allow newlines before |>)
                    while (true)
                    {
                        auto const skippedNewlines = consumeUntilNotOneOf(Token::LineFeed);
                        if (_lexer.currentToken() != Token::ForwardPipe)
                        {
                            if (skippedNewlines)
                                _lexer.pushBackToken(Token::LineFeed, "\n");
                            break;
                        }
                        _lexer.nextToken(); // consume |>
                        step = parseFSharpComposition();
                        if (!step)
                        {
                            _lexer.leaveFSharpExpr();
                            return nullptr;
                        }
                        expr = std::make_unique<ast::PipelineExpr>(std::move(expr), std::move(step));
                    }
                    _lexer.leaveFSharpExpr();
                }
                return std::make_unique<ast::ExprStmt>(std::move(expr), /*displayResult=*/_autoDisplay);
            }
            else if (_lexer.currentLiteral() == "time")
            {
                // F# style time builtin - parse as F# expression with optional |> pipeline
                _lexer.enterFSharpExpr();
                auto expr = parseFSharpApplication();
                _lexer.leaveFSharpExpr();
                if (!expr)
                    return nullptr;
                // Support trailing |> pipeline: time { body } |> formatTimeSpan
                if (_lexer.currentToken() == Token::ForwardPipe)
                {
                    _lexer.enterFSharpExpr();
                    _lexer.nextToken(); // consume first |>
                    auto step = parseFSharpComposition();
                    if (!step)
                    {
                        _lexer.leaveFSharpExpr();
                        return nullptr;
                    }
                    expr = std::make_unique<ast::PipelineExpr>(std::move(expr), std::move(step));
                    while (true)
                    {
                        auto const skippedNewlines = consumeUntilNotOneOf(Token::LineFeed);
                        if (_lexer.currentToken() != Token::ForwardPipe)
                        {
                            if (skippedNewlines)
                                _lexer.pushBackToken(Token::LineFeed, "\n");
                            break;
                        }
                        _lexer.nextToken(); // consume |>
                        step = parseFSharpComposition();
                        if (!step)
                        {
                            _lexer.leaveFSharpExpr();
                            return nullptr;
                        }
                        expr = std::make_unique<ast::PipelineExpr>(std::move(expr), std::move(step));
                    }
                    _lexer.leaveFSharpExpr();
                }
                return std::make_unique<ast::ExprStmt>(std::move(expr), /*displayResult=*/_autoDisplay);
            }
            else if (_lexer.currentLiteral() == "exec")
            {
                // F# style exec — dynamic command execution with optional | piping
                _lexer.enterFSharpExpr();
                auto expr = parseExecPipeline();
                _lexer.leaveFSharpExpr();
                if (!expr)
                    return nullptr;
                // Check for trailing |> pipeline (e.g., exec "/bin/echo" "hi" |> ...)
                if (_lexer.currentToken() == Token::ForwardPipe)
                {
                    _lexer.enterFSharpExpr();
                    _lexer.nextToken(); // consume first |>
                    auto step = parseFSharpComposition();
                    if (!step)
                    {
                        _lexer.leaveFSharpExpr();
                        return nullptr;
                    }
                    expr = std::make_unique<ast::PipelineExpr>(std::move(expr), std::move(step));
                    while (true)
                    {
                        auto const skippedNewlines = consumeUntilNotOneOf(Token::LineFeed);
                        if (_lexer.currentToken() != Token::ForwardPipe)
                        {
                            if (skippedNewlines)
                                _lexer.pushBackToken(Token::LineFeed, "\n");
                            break;
                        }
                        _lexer.nextToken(); // consume |>
                        step = parseFSharpComposition();
                        if (!step)
                        {
                            _lexer.leaveFSharpExpr();
                            return nullptr;
                        }
                        expr = std::make_unique<ast::PipelineExpr>(std::move(expr), std::move(step));
                    }
                    _lexer.leaveFSharpExpr();
                }
                return std::make_unique<ast::ExprStmt>(std::move(expr));
            }
            else if ((_lexer.currentLiteral() == "ps" || _lexer.currentLiteral() == "ls"
                      || _lexer.currentLiteral() == "jobs")
                     && [this]() {
                            // Peek at next token to decide F# vs shell routing.
                            // Only route to F# when followed by end-of-stmt, pipeline, string,
                            // paren, or unquoted path arg (identifier not starting with '-')
                            // that is NOT followed by a shell redirect (e.g. 2>&1).
                            auto const savedTok = _lexer.currentToken();
                            auto const savedLit = std::string(_lexer.currentLiteral());
                            auto const savedRange = _lexer.currentRange();
                            _lexer.nextToken();
                            auto const nextTok = _lexer.currentToken();
                            auto const nextLit = std::string(_lexer.currentLiteral());
                            auto const nextRange = _lexer.currentRange();
                            if (nextTok == Token::Identifier && !nextLit.starts_with("-"))
                            {
                                // Unquoted path arg — peek one more token to check for redirects
                                // (e.g. `ls /path 2>&1` should fall through to shell mode).
                                _lexer.nextToken();
                                auto const afterPathTok = _lexer.currentToken();
                                // Restore both tokens (path, then cmd) via pushback stack
                                _lexer.pushBackToken(nextTok, nextLit, nextRange);
                                _lexer.pushBackToken(savedTok, savedLit, savedRange);
                                // If followed by a redirect token or a number (fd prefix), fall through
                                // to shell mode so redirects are properly parsed.
                                if (afterPathTok == Token::Greater || afterPathTok == Token::GreaterGreater
                                    || afterPathTok == Token::GreaterAmp || afterPathTok == Token::Less
                                    || afterPathTok == Token::LessLess || afterPathTok == Token::LessLessDash
                                    || afterPathTok == Token::LessLessLess || afterPathTok == Token::Number)
                                    return false;
                                return true;
                            }
                            _lexer.pushBackToken(savedTok, savedLit, savedRange);
                            return nextTok == Token::LineFeed || nextTok == Token::Semicolon
                                   || nextTok == Token::EndOfInput || nextTok == Token::ForwardPipe
                                   || nextTok == Token::String || nextTok == Token::DblQuoteStart
                                   || nextTok == Token::RndOpen;
                        }())
            {
                // Structured commands: route as F# expressions with display for table rendering
                std::unique_ptr<ast::Expr> expr;

                // Consume the command name to inspect the next token
                auto const cmdLit = std::string(_lexer.currentLiteral());
                auto const cmdLoc = _lexer.currentRange();
                _lexer.nextToken(); // consume cmd -> restores peeked next token

                if (_lexer.currentToken() == Token::Identifier && !_lexer.currentLiteral().starts_with("-"))
                {
                    // Unquoted path argument: construct AST directly.
                    // F# mode can't be used because the F# lexer would re-lex
                    // paths like /tmp as Div + Identifier.
                    auto pathLit = std::string(_lexer.currentLiteral());
                    auto const pathLoc = _lexer.currentRange();
                    _lexer.nextToken(); // consume path

                    auto cmdIdent = std::make_unique<ast::IdentifierExpr>(cmdLit);
                    cmdIdent->location = cmdLoc;
                    auto pathExpr =
                        std::make_unique<ast::LiteralExpr>(std::move(pathLit), ast::LiteralQuoting::Quoted);
                    pathExpr->location = pathLoc;
                    expr = std::make_unique<ast::ApplicationExpr>(std::move(cmdIdent), std::move(pathExpr));
                }
                else
                {
                    // Quoted arg, parens, bare command, or pipe — restore cmd and use F# parsing
                    _lexer.pushBackToken(Token::Identifier, cmdLit, cmdLoc);
                    _lexer.enterFSharpExpr();
                    expr = parseFSharpApplication();
                    _lexer.leaveFSharpExpr();
                    if (!expr)
                        return nullptr;
                }

                // Common pipeline handling for both paths (allow newlines before |>)
                {
                    auto const skippedNL = consumeUntilNotOneOf(Token::LineFeed);
                    if (_lexer.currentToken() != Token::ForwardPipe && skippedNL)
                        _lexer.pushBackToken(Token::LineFeed, "\n");
                }
                if (_lexer.currentToken() == Token::ForwardPipe)
                {
                    _lexer.enterFSharpExpr();
                    _lexer.nextToken(); // consume first |>
                    auto step = parseFSharpComposition();
                    if (!step)
                    {
                        _lexer.leaveFSharpExpr();
                        return nullptr;
                    }
                    std::unique_ptr<ast::Expr> pipeline =
                        std::make_unique<ast::PipelineExpr>(std::move(expr), std::move(step));
                    while (true)
                    {
                        auto const skippedNewlines = consumeUntilNotOneOf(Token::LineFeed);
                        if (_lexer.currentToken() != Token::ForwardPipe)
                        {
                            if (skippedNewlines)
                                _lexer.pushBackToken(Token::LineFeed, "\n");
                            break;
                        }
                        _lexer.nextToken();
                        auto right = parseFSharpComposition();
                        if (!right)
                        {
                            _lexer.leaveFSharpExpr();
                            return nullptr;
                        }
                        pipeline = std::make_unique<ast::PipelineExpr>(std::move(pipeline), std::move(right));
                    }
                    _lexer.leaveFSharpExpr();
                    return std::make_unique<ast::ExprStmt>(std::move(pipeline), /*displayResult=*/true);
                }
                return std::make_unique<ast::ExprStmt>(std::move(expr), /*displayResult=*/true);
            }
            else if (_lexer.currentLiteral() == "which" && [this]() {
                         // Route to F# when followed by quoted or parenthesized args.
                         // Bare identifiers (which git) fall through to shell mode.
                         auto const savedTok = _lexer.currentToken();
                         auto const savedLit = std::string(_lexer.currentLiteral());
                         auto const savedRange = _lexer.currentRange();
                         _lexer.nextToken();
                         auto const nextTok = _lexer.currentToken();
                         _lexer.pushBackToken(savedTok, savedLit, savedRange);
                         return nextTok == Token::String || nextTok == Token::DblQuoteStart
                                || nextTok == Token::RndOpen;
                     }())
            {
                // F# which: returns Option<string> for program lookup
                _lexer.enterFSharpExpr();
                auto expr = parseFSharpApplication();
                _lexer.leaveFSharpExpr();
                if (!expr)
                    return nullptr;
                // Check for trailing |> pipeline
                if (_lexer.currentToken() == Token::ForwardPipe)
                {
                    _lexer.enterFSharpExpr();
                    _lexer.nextToken(); // consume first |>
                    auto step = parseFSharpComposition();
                    if (!step)
                    {
                        _lexer.leaveFSharpExpr();
                        return nullptr;
                    }
                    std::unique_ptr<ast::Expr> pipeline =
                        std::make_unique<ast::PipelineExpr>(std::move(expr), std::move(step));
                    while (true)
                    {
                        auto const skippedNewlines = consumeUntilNotOneOf(Token::LineFeed);
                        if (_lexer.currentToken() != Token::ForwardPipe)
                        {
                            if (skippedNewlines)
                                _lexer.pushBackToken(Token::LineFeed, "\n");
                            break;
                        }
                        _lexer.nextToken();
                        auto right = parseFSharpComposition();
                        if (!right)
                        {
                            _lexer.leaveFSharpExpr();
                            return nullptr;
                        }
                        pipeline = std::make_unique<ast::PipelineExpr>(std::move(pipeline), std::move(right));
                    }
                    _lexer.leaveFSharpExpr();
                    return std::make_unique<ast::ExprStmt>(std::move(pipeline),
                                                           /*displayResult=*/_autoDisplay);
                }
                return std::make_unique<ast::ExprStmt>(std::move(expr), /*displayResult=*/_autoDisplay);
            }
            else if (_knownVariadicFunctions.contains(_lexer.currentLiteral()))
            {
                // Variadic function at statement level: parse args as shell tokens
                auto funcName = _lexer.currentLiteral();
                _lexer.nextToken(); // consume function name

                std::unique_ptr<ast::Expr> result = std::make_unique<ast::IdentifierExpr>(funcName);

                // Collect arguments in shell tokenization mode (identifiers, flags, paths, etc.)
                while (isParameterToken())
                {
                    auto arg = parseParameter();
                    if (!arg)
                        break;
                    result = std::make_unique<ast::ApplicationExpr>(std::move(result), std::move(arg));
                }

                // Handle trailing |> pipeline
                if (_lexer.currentToken() == Token::ForwardPipe)
                {
                    _lexer.enterFSharpExpr();
                    _lexer.nextToken(); // consume first |>
                    auto step = parseFSharpComposition();
                    if (!step)
                    {
                        _lexer.leaveFSharpExpr();
                        return nullptr;
                    }
                    auto pipeline = std::make_unique<ast::PipelineExpr>(std::move(result), std::move(step));
                    while (true)
                    {
                        auto const skippedNewlines = consumeUntilNotOneOf(Token::LineFeed);
                        if (_lexer.currentToken() != Token::ForwardPipe)
                        {
                            if (skippedNewlines)
                                _lexer.pushBackToken(Token::LineFeed, "\n");
                            break;
                        }
                        _lexer.nextToken();
                        auto right = parseFSharpComposition();
                        if (!right)
                        {
                            _lexer.leaveFSharpExpr();
                            return nullptr;
                        }
                        pipeline = std::make_unique<ast::PipelineExpr>(std::move(pipeline), std::move(right));
                    }
                    _lexer.leaveFSharpExpr();
                    return std::make_unique<ast::ExprStmt>(std::move(pipeline),
                                                           /*displayResult=*/_autoDisplay);
                }

                return std::make_unique<ast::ExprStmt>(std::move(result), /*displayResult=*/_autoDisplay);
            }
            else if (_knownFSharpFunctions.contains(_lexer.currentLiteral()))
            {
                // Bare top-level call to a known F# function or variable
                _lexer.enterFSharpExpr();
                auto expr = parseFSharpApplication();
                _lexer.leaveFSharpExpr();
                if (!expr)
                    return nullptr;
                // Check for mutable assignment: x <- expr
                if (_lexer.currentToken() == Token::LeftArrow)
                {
                    if (auto* identExpr = dynamic_cast<ast::IdentifierExpr*>(expr.get()))
                    {
                        auto name = identExpr->name;
                        auto const nameRange = identExpr->location;
                        _lexer.nextToken(); // consume '<-'
                        _lexer.enterFSharpExpr();
                        auto value = parseFSharpExpr();
                        _lexer.leaveFSharpExpr();
                        if (!value)
                            return nullptr;
                        auto stmt = std::make_unique<ast::MutAssignStmt>(std::move(name), std::move(value));
                        stmt->location =
                            nameRange && stmt->value->location
                                ? SourceLocationRange { nameRange->begin, stmt->value->location->end }
                                : nameRange;
                        return stmt;
                    }
                }
                // Check for trailing |> pipeline
                if (_lexer.currentToken() == Token::ForwardPipe)
                {
                    _lexer.enterFSharpExpr();
                    _lexer.nextToken(); // consume first |>
                    auto step = parseFSharpComposition();
                    if (!step)
                    {
                        _lexer.leaveFSharpExpr();
                        return nullptr;
                    }
                    std::unique_ptr<ast::Expr> pipeline =
                        std::make_unique<ast::PipelineExpr>(std::move(expr), std::move(step));
                    while (true)
                    {
                        auto const skippedNewlines = consumeUntilNotOneOf(Token::LineFeed);
                        if (_lexer.currentToken() != Token::ForwardPipe)
                        {
                            if (skippedNewlines)
                                _lexer.pushBackToken(Token::LineFeed, "\n");
                            break;
                        }
                        _lexer.nextToken();
                        auto right = parseFSharpComposition();
                        if (!right)
                        {
                            _lexer.leaveFSharpExpr();
                            return nullptr;
                        }
                        pipeline = std::make_unique<ast::PipelineExpr>(std::move(pipeline), std::move(right));
                    }
                    _lexer.leaveFSharpExpr();
                    return std::make_unique<ast::ExprStmt>(std::move(pipeline),
                                                           /*displayResult=*/_autoDisplay);
                }
                return std::make_unique<ast::ExprStmt>(std::move(expr), /*displayResult=*/_autoDisplay);
            }
            else
            {
                // Check for F# mutable assignment: identifier <- expr
                // We need to peek ahead to see if the next token is '<-'
                // This is safe because in shell mode, '<-' is also tokenized as LeftArrow
                auto const& ident = _lexer.currentLiteral();
                if (!ident.empty() && (std::isalpha(static_cast<unsigned char>(ident[0])) || ident[0] == '_'))
                {
                    auto savedToken = _lexer.currentToken();
                    auto savedLiteral = ident;
                    auto savedRange = _lexer.currentRange();
                    _lexer.nextToken(); // consume identifier
                    if (_lexer.currentToken() == Token::LeftArrow)
                    {
                        _lexer.nextToken(); // consume '<-'
                        _lexer.enterFSharpExpr();
                        auto value = parseFSharpExpr();
                        _lexer.leaveFSharpExpr();
                        if (!value)
                            return nullptr;
                        auto stmt =
                            std::make_unique<ast::MutAssignStmt>(std::move(savedLiteral), std::move(value));
                        stmt->location =
                            stmt->value->location
                                ? SourceLocationRange { savedRange.begin, stmt->value->location->end }
                                : savedRange;
                        return stmt;
                    }
                    // Not a mutable assignment — we consumed the identifier, need to push it back.
                    // Instead, we inject it as the command name and continue parsing.
                    // The identifier was consumed; fall through to command parsing by
                    // reconstructing a ProgramCall manually.
                    _lexer.pushBackToken(savedToken, savedLiteral, savedRange);
                }

                // All other statements (builtins and commands) can participate
                // in logical expressions (&&, ||)
                auto stmt = parseLogicalExpr();
                if (!stmt)
                    return nullptr;

                // Check for data source 'as' type annotation:
                // open-json "file" as { ... } or curl | from-json as { ... }
                if (_lexer.currentToken() == Token::As)
                {
                    if (auto dataSource = tryParseDataSource(std::move(stmt)))
                    {
                        // Check for |> pipeline continuation
                        // DataSourceExpr already produces a typed list value — use it directly
                        // as the pipeline source (no StructuredPipelineSourceExpr wrapper needed).
                        {
                            auto const skippedNL = consumeUntilNotOneOf(Token::LineFeed);
                            if (_lexer.currentToken() != Token::ForwardPipe && skippedNL)
                                _lexer.pushBackToken(Token::LineFeed, "\n");
                        }
                        if (_lexer.currentToken() == Token::ForwardPipe)
                        {
                            _lexer.enterFSharpExpr();
                            _lexer.nextToken(); // consume first |>

                            auto step = parseFSharpComposition();
                            if (!step)
                            {
                                _lexer.leaveFSharpExpr();
                                return nullptr;
                            }

                            std::unique_ptr<ast::Expr> pipeline =
                                std::make_unique<ast::PipelineExpr>(std::move(dataSource), std::move(step));

                            while (true)
                            {
                                auto const skippedNewlines = consumeUntilNotOneOf(Token::LineFeed);
                                if (_lexer.currentToken() != Token::ForwardPipe)
                                {
                                    if (skippedNewlines)
                                        _lexer.pushBackToken(Token::LineFeed, "\n");
                                    break;
                                }
                                _lexer.nextToken();
                                auto right = parseFSharpComposition();
                                if (!right)
                                {
                                    _lexer.leaveFSharpExpr();
                                    return nullptr;
                                }
                                pipeline = std::make_unique<ast::PipelineExpr>(std::move(pipeline),
                                                                               std::move(right));
                            }

                            _lexer.leaveFSharpExpr();
                            return std::make_unique<ast::ExprStmt>(std::move(pipeline));
                        }

                        return std::make_unique<ast::ExprStmt>(std::move(dataSource));
                    }
                    // If tryParseDataSource returned nullptr, the 'as' was not
                    // after a data source command — fall through.
                }

                // Shell command followed by |> → structured F# pipeline
                // Build left-associative pipeline chain: source |> step1 |> step2 |> ...
                if (_lexer.currentToken() == Token::ForwardPipe)
                {
                    auto source = std::make_unique<ast::StructuredPipelineSourceExpr>(std::move(stmt));
                    _lexer.enterFSharpExpr();
                    _lexer.nextToken(); // consume first |>

                    // Parse first pipeline step (single composition, not full pipeline)
                    auto step = parseFSharpComposition();
                    if (!step)
                    {
                        _lexer.leaveFSharpExpr();
                        return nullptr;
                    }

                    // Build left-associative chain: PipelineExpr(source, step1)
                    std::unique_ptr<ast::Expr> pipeline =
                        std::make_unique<ast::PipelineExpr>(std::move(source), std::move(step));

                    // Parse remaining |> steps left-associatively (allow newlines before |>)
                    while (true)
                    {
                        auto const skippedNewlines = consumeUntilNotOneOf(Token::LineFeed);
                        if (_lexer.currentToken() != Token::ForwardPipe)
                        {
                            if (skippedNewlines)
                                _lexer.pushBackToken(Token::LineFeed, "\n");
                            break;
                        }
                        _lexer.nextToken(); // consume |>
                        auto right = parseFSharpComposition();
                        if (!right)
                        {
                            _lexer.leaveFSharpExpr();
                            return nullptr;
                        }
                        pipeline = std::make_unique<ast::PipelineExpr>(std::move(pipeline), std::move(right));
                    }

                    _lexer.leaveFSharpExpr();
                    return std::make_unique<ast::ExprStmt>(std::move(pipeline));
                }
                return stmt;
            }
        // Boolean literals: participate in shell logical chains (&&, ||)
        case Token::True:
        case Token::False: return parseLogicalExpr();
        // Bare F# expression evaluation: tokens that unambiguously start F# expressions
        case Token::Number:
        case Token::RndOpen:
        case Token::BracketOpen:
        case Token::Fun:
        case Token::OptionSome:
        case Token::OptionNone:
        case Token::ResultOk:
        case Token::ResultError:
        case Token::Try: {
            _lexer.enterFSharpExpr();
            auto expr = parseFSharpExpr();
            _lexer.leaveFSharpExpr();
            if (!expr)
                return nullptr;
            // Check for trailing |> pipeline
            if (_lexer.currentToken() == Token::ForwardPipe)
            {
                _lexer.enterFSharpExpr();
                _lexer.nextToken(); // consume first |>
                auto step = parseFSharpComposition();
                if (!step)
                {
                    _lexer.leaveFSharpExpr();
                    return nullptr;
                }
                std::unique_ptr<ast::Expr> pipeline =
                    std::make_unique<ast::PipelineExpr>(std::move(expr), std::move(step));
                while (true)
                {
                    auto const skippedNewlines = consumeUntilNotOneOf(Token::LineFeed);
                    if (_lexer.currentToken() != Token::ForwardPipe)
                    {
                        if (skippedNewlines)
                            _lexer.pushBackToken(Token::LineFeed, "\n");
                        break;
                    }
                    _lexer.nextToken();
                    auto right = parseFSharpComposition();
                    if (!right)
                    {
                        _lexer.leaveFSharpExpr();
                        return nullptr;
                    }
                    pipeline = std::make_unique<ast::PipelineExpr>(std::move(pipeline), std::move(right));
                }
                _lexer.leaveFSharpExpr();
                return std::make_unique<ast::ExprStmt>(std::move(pipeline), /*displayResult=*/true);
            }
            return std::make_unique<ast::ExprStmt>(std::move(expr), /*displayResult=*/true);
        }
        case Token::EndOfInput:
            _report.syntaxErrorWithSuggestions(
                currentLocation(),
                { "Check for unclosed quotes, parentheses, or control structures" },
                currentContextSnippet(),
                "Unexpected end of input");
            return nullptr;
        case Token::Tilde: {
            // Tilde-prefixed command: ~/bin/foo args...
            auto stmt = parseLogicalExpr();
            if (!stmt)
                return nullptr;

            // Shell command followed by |> → structured F# pipeline
            if (_lexer.currentToken() == Token::ForwardPipe)
            {
                auto source = std::make_unique<ast::StructuredPipelineSourceExpr>(std::move(stmt));
                _lexer.enterFSharpExpr();
                _lexer.nextToken(); // consume first |>

                auto step = parseFSharpComposition();
                if (!step)
                {
                    _lexer.leaveFSharpExpr();
                    return nullptr;
                }

                std::unique_ptr<ast::Expr> pipeline =
                    std::make_unique<ast::PipelineExpr>(std::move(source), std::move(step));

                while (true)
                {
                    auto const skippedNewlines = consumeUntilNotOneOf(Token::LineFeed);
                    if (_lexer.currentToken() != Token::ForwardPipe)
                    {
                        if (skippedNewlines)
                            _lexer.pushBackToken(Token::LineFeed, "\n");
                        break;
                    }
                    _lexer.nextToken(); // consume |>
                    auto right = parseFSharpComposition();
                    if (!right)
                    {
                        _lexer.leaveFSharpExpr();
                        return nullptr;
                    }
                    pipeline = std::make_unique<ast::PipelineExpr>(std::move(pipeline), std::move(right));
                }

                _lexer.leaveFSharpExpr();
                return std::make_unique<ast::ExprStmt>(std::move(pipeline));
            }
            return stmt;
        }
        default:
            _report.syntaxErrorWithSuggestions(currentLocation(),
                                               {},
                                               currentContextSnippet(),
                                               "Unexpected token '{}'",
                                               _lexer.currentTokenText());
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

std::unique_ptr<ast::WhileStmt> Parser::parseWhile()
{
    TRACE_SCOPE("parseWhile");

    auto const whileColumn = currentTokenColumn();

    // Enter F# mode before consuming 'while' so the condition's first token is read in F# mode
    _lexer.enterFSharpExpr();
    auto const whileLoc = _lexer.currentRange();
    _lexer.nextToken(); // consume 'while'
    auto condition = parseFSharpExpr();
    _lexer.leaveFSharpExpr();
    if (!condition)
        return nullptr;

    // Optional separator before 'do'
    if (_lexer.currentToken() == Token::Semicolon || _lexer.currentToken() == Token::LineFeed)
        _lexer.nextToken();
    consumeDirective("do");

    auto body = parseIndentedBlock(whileColumn);
    auto const endPos = (body && body->location) ? body->location->end : whileLoc.end;
    auto node = std::make_unique<ast::WhileStmt>(std::move(condition), std::move(body));
    node->location = SourceLocationRange { whileLoc.begin, endPos };
    return node;
}

std::unique_ptr<ast::Statement> Parser::parseFor()
{
    TRACE_SCOPE("parseFor");
    auto const forColumn = currentTokenColumn();
    auto const forLoc = _lexer.currentRange();
    _lexer.nextToken(); // consume 'for'

    // Always F# style: for pattern in expr do body
    return parseForIn(forLoc, forColumn);
}

std::unique_ptr<ast::ForInStmt> Parser::parseForIn(SourceLocationRange const& forLoc, size_t forColumn)
{
    TRACE_SCOPE("parseForIn");

    // Enter F# mode for pattern and source expression parsing
    _lexer.enterFSharpExpr();

    // Parse the destructuring pattern (tuple or record)
    auto pat = parsePrimaryPattern();
    if (!pat)
    {
        _lexer.leaveFSharpExpr();
        _report.syntaxErrorWithSuggestions(currentLocation(),
                                           { "Provide a pattern for the for-in loop" },
                                           currentContextSnippet(),
                                           "Expected pattern after 'for', got '{}'",
                                           _lexer.currentTokenText());
        return nullptr;
    }

    // Expect 'in' keyword
    if (!_lexer.isDirective("in"))
    {
        _lexer.leaveFSharpExpr();
        _report.syntaxErrorWithSuggestions(currentLocation(),
                                           { "Use 'in' to specify the list to iterate over" },
                                           currentContextSnippet(),
                                           "Expected 'in' after pattern, got '{}'",
                                           _lexer.currentTokenText());
        return nullptr;
    }
    _lexer.nextToken(); // consume 'in'

    // Parse source expression (already in F# mode)
    auto source = parseFSharpExpr();
    _lexer.leaveFSharpExpr();

    if (!source)
    {
        _report.syntaxErrorWithSuggestions(currentLocation(),
                                           { "Provide an expression to iterate over" },
                                           currentContextSnippet(),
                                           "Expected expression after 'in'");
        return nullptr;
    }

    // Optional separator before 'do' — F# style allows `for pat in expr do` without semicolon
    if (_lexer.currentToken() == Token::Semicolon || _lexer.currentToken() == Token::LineFeed)
        _lexer.nextToken();
    consumeDirective("do");

    auto body = parseIndentedBlock(forColumn);
    auto const endPos = (body && body->location) ? body->location->end : forLoc.end;

    auto node = std::make_unique<ast::ForInStmt>(std::move(pat), std::move(source), std::move(body));
    node->location = SourceLocationRange { forLoc.begin, endPos };
    return node;
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
                                                   _lexer.currentTokenText());
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
                                                   _lexer.currentTokenText());
                return false;
            }
            return false;
    }
}

std::unique_ptr<ast::ProgramCall> Parser::parseCall(bool piped)
{
    TRACE_SCOPE("parseCall");
    auto const programLocation = _lexer.currentRange();

    std::unique_ptr<ast::Expr> progExpr;
    std::string program;
    if (_lexer.currentToken() == Token::Tilde)
    {
        // Tilde-prefixed program name: ~/bin/foo, ~user/bin/bar
        auto const& lit = _lexer.currentLiteral();
        program = lit.empty() ? "~" : lit;
        if (!program.starts_with('~'))
            program = "~" + program;
        progExpr = parseTildeExpansion();
    }
    else
    {
        program = consumeLiteral();
    }
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
                                _lexer.currentTokenText());
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
            // Check if the next token is adjacent (no whitespace) — combine into a compound word
            if (!_lexer.hasPrecedingSpace() && isParameterToken())
            {
                std::vector<std::unique_ptr<ast::Expr>> parts;
                parts.push_back(std::make_unique<ast::LiteralExpr>(numLiteral));
                while (!_lexer.hasPrecedingSpace() && isParameterToken())
                    if (auto part = parseParameter())
                        parts.push_back(std::move(part));
                    else
                        break;
                if (parts.size() == 1)
                    arguments.emplace_back(std::move(parts.front()));
                else
                    arguments.emplace_back(std::make_unique<ast::ConcatExpr>(std::move(parts)));
            }
            else
            {
                arguments.emplace_back(std::make_unique<ast::LiteralExpr>(numLiteral));
            }
            continue;
        }

        // Regular argument — combine adjacent tokens without whitespace into a single word
        auto arg = parseCompoundParameter();
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

    auto result = std::make_unique<ast::ProgramCall>(*builtinCallProcess,
                                                     std::move(program),
                                                     std::move(arguments),
                                                     std::move(inputRedirects),
                                                     std::move(outputRedirects),
                                                     std::move(hereDocuments),
                                                     std::move(hereStrings));
    result->programExpr = std::move(progExpr);
    result->programLocation = programLocation;
    return result;
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

        auto arg = parseCompoundParameter();
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
                                           _lexer.currentTokenText());
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
                                           _lexer.currentTokenText());
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
                                           _lexer.currentTokenText());
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

std::unique_ptr<ast::Expr> Parser::parseInterpolatedString()
{
    TRACE_SCOPE("parseInterpolatedString");

    // We've already consumed DblQuoteStart
    _lexer.nextToken(); // consume DblQuoteStart

    std::vector<std::unique_ptr<ast::Expr>> parts;

    // Collect parts until we hit DblQuoteEnd
    while (_lexer.currentToken() != Token::DblQuoteEnd && _lexer.currentToken() != Token::EndOfInput
           && _lexer.currentToken() != Token::Invalid)
    {
        switch (_lexer.currentToken())
        {
            case Token::StringFragment:
                parts.push_back(std::make_unique<ast::LiteralExpr>(consumeLiteral()));
                break;

            case Token::DollarName:
                parts.push_back(
                    std::make_unique<ast::VariableExpr>(consumeLiteral(), ast::VariableType::Named, false));
                break;

            case Token::DollarBraceName:
                parts.push_back(
                    std::make_unique<ast::VariableExpr>(consumeLiteral(), ast::VariableType::Named, true));
                break;

            case Token::DollarBraceParam: parts.push_back(parseParamExpansion()); break;

            case Token::DollarQuestion:
                _lexer.nextToken();
                parts.push_back(
                    std::make_unique<ast::VariableExpr>("?", ast::VariableType::ExitStatus, false));
                break;

            case Token::DollarDollar:
                _lexer.nextToken();
                parts.push_back(
                    std::make_unique<ast::VariableExpr>("$", ast::VariableType::ProcessId, false));
                break;

            case Token::DollarNot:
                _lexer.nextToken();
                parts.push_back(
                    std::make_unique<ast::VariableExpr>("!", ast::VariableType::BackgroundId, false));
                break;

            case Token::DollarNumber:
                parts.push_back(std::make_unique<ast::VariableExpr>(
                    consumeLiteral(), ast::VariableType::Positional, false));
                break;

            case Token::DollarRndOpen: parts.push_back(parseCommandSubstitution()); break;

            case Token::DollarDblRndOpen: parts.push_back(parseArithmeticExpansion()); break;

            case Token::Backtick: parts.push_back(parseBacktickSubstitution()); break;

            default:
                // Unexpected token inside interpolated string
                _report.syntaxErrorWithSuggestions(currentLocation(),
                                                   {},
                                                   currentContextSnippet(),
                                                   "Unexpected token '{}' in interpolated string",
                                                   _lexer.currentTokenText());
                return nullptr;
        }
    }

    if (_lexer.currentToken() != Token::DblQuoteEnd)
    {
        _report.syntaxErrorWithSuggestions(currentLocation(),
                                           { "Add a closing double quote" },
                                           currentContextSnippet(),
                                           "Unterminated double-quoted string");
        return nullptr;
    }

    _lexer.nextToken(); // consume DblQuoteEnd

    // Optimize: if only one part and it's a literal, return it directly
    if (parts.size() == 1)
    {
        if (auto* lit = dynamic_cast<ast::LiteralExpr*>(parts[0].get()))
        {
            lit->quoting = ast::LiteralQuoting::Quoted;
            return std::move(parts[0]);
        }
    }

    // If empty string, return empty literal
    if (parts.empty())
        return std::make_unique<ast::LiteralExpr>("", ast::LiteralQuoting::Quoted);

    // Otherwise create a ConcatExpr
    return std::make_unique<ast::ConcatExpr>(std::move(parts));
}

std::unique_ptr<ast::Expr> Parser::parseFStringExpression()
{
    TRACE_SCOPE("parseFStringExpression");

    _lexer.nextToken(); // consume FStringStart

    std::vector<std::unique_ptr<ast::Expr>> parts;

    while (_lexer.currentToken() != Token::FStringEnd && _lexer.currentToken() != Token::EndOfInput
           && _lexer.currentToken() != Token::Invalid)
    {
        switch (_lexer.currentToken())
        {
            case Token::StringFragment:
                parts.push_back(std::make_unique<ast::LiteralExpr>(consumeLiteral()));
                break;

            case Token::FStringExprStart: {
                _lexer.nextToken(); // consume {
                auto expr = parseFSharpExpr();
                if (!expr)
                    return nullptr;
                if (_lexer.currentToken() != Token::FStringExprEnd)
                {
                    _report.syntaxErrorWithSuggestions(
                        currentLocation(),
                        { "Add a closing '}'" },
                        currentContextSnippet(),
                        "Expected '}}' after expression in interpolated string, got '{}'",
                        _lexer.currentTokenText());
                    return nullptr;
                }
                _lexer.nextToken(); // consume }
                parts.push_back(std::move(expr));
                break;
            }

            default:
                _report.syntaxErrorWithSuggestions(currentLocation(),
                                                   {},
                                                   currentContextSnippet(),
                                                   "Unexpected token '{}' in F# interpolated string",
                                                   _lexer.currentTokenText());
                return nullptr;
        }
    }

    if (_lexer.currentToken() != Token::FStringEnd)
    {
        _report.syntaxErrorWithSuggestions(currentLocation(),
                                           { "Add a closing double quote" },
                                           currentContextSnippet(),
                                           "Unterminated F# interpolated string");
        return nullptr;
    }

    _lexer.nextToken(); // consume FStringEnd

    // Empty string
    if (parts.empty())
        return std::make_unique<ast::FStringExpr>(std::move(parts));

    return std::make_unique<ast::FStringExpr>(std::move(parts));
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
        if (_lexer.currentToken() == Token::EqualEqual)
        {
            _lexer.nextToken();
            auto right = parseArithComparison();
            if (!right)
                return nullptr;
            left =
                std::make_unique<ast::ArithBinaryExpr>(ast::ArithOp::Eq, std::move(left), std::move(right));
            continue;
        }
        else if (_lexer.currentToken() == Token::NotEqual)
        {
            _lexer.nextToken();
            auto right = parseArithComparison();
            if (!right)
                return nullptr;
            left =
                std::make_unique<ast::ArithBinaryExpr>(ast::ArithOp::Ne, std::move(left), std::move(right));
            continue;
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
        case Token::DblQuoteStart: return parseInterpolatedString();
        case Token::FStringStart: return parseFStringExpression();
        case Token::Ellipsis: {
            // Splat expression: ...args (expands a list into individual arguments)
            _lexer.nextToken(); // consume '...'
            if (_lexer.currentToken() != Token::Identifier)
            {
                _report.syntaxErrorWithSuggestions(currentLocation(),
                                                   { "Add identifier after '...'" },
                                                   currentContextSnippet(),
                                                   "Expected identifier after '...' for splat expression");
                return nullptr;
            }
            auto name = consumeLiteral();
            return std::make_unique<ast::SplatExpr>(std::move(name));
        }
        default:
            _report.syntaxErrorWithSuggestions(currentLocation(),
                                               {},
                                               currentContextSnippet(),
                                               "Expected parameter, got '{}'",
                                               _lexer.currentTokenText());
            return nullptr;
    }
}

std::unique_ptr<ast::Expr> Parser::parseCompoundParameter()
{
    TRACE_SCOPE("parseCompoundParameter");
    auto first = parseParameter();
    if (!first)
        return nullptr;

    // Check if the next token is adjacent (no intervening whitespace) and is a parameter token
    if (_lexer.hasPrecedingSpace() || !isParameterToken())
        return first;

    // Multiple adjacent tokens — combine into ConcatExpr
    std::vector<std::unique_ptr<ast::Expr>> parts;
    parts.push_back(std::move(first));

    while (!_lexer.hasPrecedingSpace() && isParameterToken())
        if (auto part = parseParameter())
            parts.push_back(std::move(part));
        else
            break;

    if (parts.size() == 1)
        return std::move(parts.front());

    return std::make_unique<ast::ConcatExpr>(std::move(parts));
}

std::unique_ptr<ast::Statement> Parser::parsePrimaryStmt()
{
    TRACE_SCOPE("parsePrimaryStmt");

    // Handle boolean literals as simple statements
    if (_lexer.currentToken() == Token::True)
    {
        _lexer.nextToken();
        return std::make_unique<ast::ExprStmt>(std::make_unique<ast::BoolLiteralExpr>(true));
    }
    if (_lexer.currentToken() == Token::False)
    {
        _lexer.nextToken();
        return std::make_unique<ast::ExprStmt>(std::make_unique<ast::BoolLiteralExpr>(false));
    }

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
            return std::make_unique<ast::BuiltinBindStmt>(*_runtime.find("bind(s)I"), std::move(args));
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

bool Parser::consumeNewlines()
{
    return consumeUntilNotOneOf(Token::Semicolon, Token::LineFeed);
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
                                           _lexer.currentTokenText());
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

// ============================================================================
// F# Style Let Bindings and Expression Parser
// ============================================================================

size_t Parser::currentTokenColumn() const noexcept
{
    return static_cast<size_t>(_lexer.currentRange().begin.column) + 1; // 1-based
}

bool Parser::isFSharpPrimary() const noexcept
{
    switch (_lexer.currentToken())
    {
        case Token::Number:
        case Token::String:        // String literal: 'hello'
        case Token::DblQuoteStart: // Double-quoted string: "hello"
        case Token::RndOpen:
        case Token::Fun:
        case Token::Match:
        case Token::True:          // Boolean literal: true
        case Token::False:         // Boolean literal: false
        case Token::BracketOpen:   // List literal: [1; 2; 3]
        case Token::BraceOpen:     // Block expression: { ... }
        case Token::Ampersand:     // Shell command expression: & git status
        case Token::DollarRndOpen: // Command substitution: $(whoami)
        case Token::OptionSome:    // Some expr
        case Token::OptionNone:    // None
        case Token::ResultOk:      // Ok expr
        case Token::ResultError:   // Error expr
        case Token::Try:           // try expr with ...
        case Token::FStringStart:  // F# interpolated string: $"..."
            return true;
        case Token::Identifier: {
            auto const& lit = _lexer.currentLiteral();
            if (lit.empty())
                return false;
            // Contextual keywords that should not be treated as primary expressions
            if (lit == "in" || lit == "then" || lit == "else" || lit == "elif" || lit == "do"
                || lit == "break" || lit == "continue" || lit == "exec")
                return false;
            // Variable identifiers start with alphanumeric or underscore
            // Operators like +, -, *, /, |>, etc. start with symbols
            char const first = lit[0];
            return std::isalnum(static_cast<unsigned char>(first)) || first == '_' || first == '[';
        }
        default: return false;
    }
}

bool Parser::isBinaryOperatorToken() const noexcept
{
    switch (_lexer.currentToken())
    {
        case Token::Plus:
        case Token::Minus:
        case Token::Star:
        case Token::Slash:
        case Token::Percent:
        case Token::StarStar:
        case Token::EqualEqual:
        case Token::NotEqual:
        case Token::Less:
        case Token::LessEqual:
        case Token::Greater:
        case Token::GreaterEqual:
        case Token::AmpAmp:
        case Token::PipePipe:
        case Token::ColonColon:
        case Token::At:
        case Token::GreaterGreater:
        case Token::LessLess:
        case Token::DotDot: return true;
        default: return false;
    }
}

TypePtr Parser::parseType()
{
    TRACE_SCOPE("parseType");
    auto left = parseBaseType();
    if (!left)
        return nullptr;

    // Right-associative function type: baseType -> type
    if (_lexer.currentToken() == Token::Arrow)
    {
        _lexer.nextToken(); // consume '->'
        auto right = parseType();
        if (!right)
            return nullptr;
        return types::function(std::move(left), std::move(right));
    }

    return left;
}

TypePtr Parser::parseBaseType()
{
    TRACE_SCOPE("parseBaseType");

    if (_lexer.currentToken() == Token::RndOpen)
    {
        // Parenthesized type or tuple type: (type) or (type, type, ...)
        _lexer.nextToken(); // consume '('

        if (_lexer.currentToken() == Token::RndClose)
        {
            _lexer.nextToken(); // consume ')'
            return types::unitType();
        }

        auto first = parseType();
        if (!first)
            return nullptr;

        if (_lexer.currentToken() == Token::Comma)
        {
            // Tuple type: (T1, T2, ...)
            std::vector<TypePtr> elements;
            elements.push_back(std::move(first));
            while (_lexer.currentToken() == Token::Comma)
            {
                _lexer.nextToken(); // consume ','
                auto elem = parseType();
                if (!elem)
                    return nullptr;
                elements.push_back(std::move(elem));
            }
            if (_lexer.currentToken() != Token::RndClose)
            {
                _report.syntaxErrorWithSuggestions(currentLocation(),
                                                   { "Add ')' to close tuple type" },
                                                   currentContextSnippet(),
                                                   "Expected ')' in tuple type, got '{}'",
                                                   _lexer.currentTokenText());
                return nullptr;
            }
            _lexer.nextToken(); // consume ')'
            return types::tuple(std::move(elements));
        }

        if (_lexer.currentToken() != Token::RndClose)
        {
            _report.syntaxErrorWithSuggestions(currentLocation(),
                                               { "Add ')' to close parenthesized type" },
                                               currentContextSnippet(),
                                               "Expected ')' in type expression, got '{}'",
                                               _lexer.currentTokenText());
            return nullptr;
        }
        _lexer.nextToken(); // consume ')'
        return first;
    }

    if (_lexer.currentToken() != Token::Identifier)
    {
        _report.syntaxErrorWithSuggestions(currentLocation(),
                                           { "Provide a type name like 'int', 'str', 'bool', etc." },
                                           currentContextSnippet(),
                                           "Expected type name, got '{}'",
                                           _lexer.currentTokenText());
        return nullptr;
    }

    auto const& typeName = _lexer.currentLiteral();

    // Primitive types
    if (typeName == "int")
    {
        _lexer.nextToken();
        return types::intType();
    }
    if (typeName == "float")
    {
        _lexer.nextToken();
        return types::floatType();
    }
    if (typeName == "str" || typeName == "string")
    {
        _lexer.nextToken();
        return types::strType();
    }
    if (typeName == "bool")
    {
        _lexer.nextToken();
        return types::boolType();
    }
    if (typeName == "unit")
    {
        _lexer.nextToken();
        return types::unitType();
    }

    // Generic types: list<T>, option<T>, result<T, E>
    if (typeName == "list")
    {
        _lexer.nextToken(); // consume 'list'
        if (_lexer.currentToken() != Token::Less)
        {
            _report.syntaxErrorWithSuggestions(currentLocation(),
                                               { "Add type parameter: 'list<int>'" },
                                               currentContextSnippet(),
                                               "Expected '<' after 'list', got '{}'",
                                               _lexer.currentTokenText());
            return nullptr;
        }
        _lexer.nextToken(); // consume '<'
        auto inner = parseType();
        if (!inner)
            return nullptr;
        if (_lexer.currentToken() != Token::Greater)
        {
            _report.syntaxErrorWithSuggestions(currentLocation(),
                                               { "Add '>' to close type parameter" },
                                               currentContextSnippet(),
                                               "Expected '>' after list element type, got '{}'",
                                               _lexer.currentTokenText());
            return nullptr;
        }
        _lexer.nextToken(); // consume '>'
        return types::list(std::move(inner));
    }

    if (typeName == "option")
    {
        _lexer.nextToken(); // consume 'option'
        if (_lexer.currentToken() != Token::Less)
        {
            _report.syntaxErrorWithSuggestions(currentLocation(),
                                               { "Add type parameter: 'option<int>'" },
                                               currentContextSnippet(),
                                               "Expected '<' after 'option', got '{}'",
                                               _lexer.currentTokenText());
            return nullptr;
        }
        _lexer.nextToken(); // consume '<'
        auto inner = parseType();
        if (!inner)
            return nullptr;
        if (_lexer.currentToken() != Token::Greater)
        {
            _report.syntaxErrorWithSuggestions(currentLocation(),
                                               { "Add '>' to close type parameter" },
                                               currentContextSnippet(),
                                               "Expected '>' after option inner type, got '{}'",
                                               _lexer.currentTokenText());
            return nullptr;
        }
        _lexer.nextToken(); // consume '>'
        return types::option(std::move(inner));
    }

    if (typeName == "result")
    {
        _lexer.nextToken(); // consume 'result'
        if (_lexer.currentToken() != Token::Less)
        {
            _report.syntaxErrorWithSuggestions(currentLocation(),
                                               { "Add type parameters: 'result<int, str>'" },
                                               currentContextSnippet(),
                                               "Expected '<' after 'result', got '{}'",
                                               _lexer.currentTokenText());
            return nullptr;
        }
        _lexer.nextToken(); // consume '<'
        auto okType = parseType();
        if (!okType)
            return nullptr;
        if (_lexer.currentToken() != Token::Comma)
        {
            _report.syntaxErrorWithSuggestions(currentLocation(),
                                               { "Provide error type: 'result<int, str>'" },
                                               currentContextSnippet(),
                                               "Expected ',' in result type, got '{}'",
                                               _lexer.currentTokenText());
            return nullptr;
        }
        _lexer.nextToken(); // consume ','
        auto errType = parseType();
        if (!errType)
            return nullptr;
        if (_lexer.currentToken() != Token::Greater)
        {
            _report.syntaxErrorWithSuggestions(currentLocation(),
                                               { "Add '>' to close type parameters" },
                                               currentContextSnippet(),
                                               "Expected '>' after result error type, got '{}'",
                                               _lexer.currentTokenText());
            return nullptr;
        }
        _lexer.nextToken(); // consume '>'
        return types::result(std::move(okType), std::move(errType));
    }

    _report.syntaxErrorWithSuggestions(currentLocation(),
                                       { "Use a valid type: int, float, str, bool, unit, list<T>, option<T>, "
                                         "result<T, E>" },
                                       currentContextSnippet(),
                                       "Unknown type name '{}'",
                                       typeName);
    return nullptr;
}

std::optional<ast::TypedParameter> Parser::parseTypedParameter()
{
    TRACE_SCOPE("parseTypedParameter");

    // Variadic parameter: ...name
    if (_lexer.currentToken() == Token::Ellipsis)
    {
        _lexer.nextToken(); // consume '...'
        if (_lexer.currentToken() != Token::Identifier)
        {
            _report.syntaxErrorWithSuggestions(currentLocation(),
                                               { "Add identifier after '...'" },
                                               currentContextSnippet(),
                                               "Expected identifier after '...' for variadic parameter");
            return std::nullopt;
        }
        auto name = consumeLiteral();
        return ast::TypedParameter(std::move(name), /*variadic=*/true);
    }

    if (_lexer.currentToken() == Token::Identifier)
    {
        auto const& lit = _lexer.currentLiteral();
        // Exclude contextual keywords that end parameter lists
        if (lit == "in")
            return std::nullopt;
        auto name = consumeLiteral();
        return ast::TypedParameter(std::move(name));
    }

    if (_lexer.currentToken() == Token::RndOpen)
    {
        // Could be (name: type) annotated parameter, or start of value expression
        // We need to look ahead: ( Identifier : means annotated parameter
        _lexer.nextToken(); // consume '('

        // Unit parameter: ()
        if (_lexer.currentToken() == Token::RndClose)
        {
            _lexer.nextToken(); // consume ')'
            return ast::TypedParameter::unitParam();
        }

        if (_lexer.currentToken() != Token::Identifier)
        {
            // Not a parameter — push back '(' and return nullopt
            _lexer.pushBackToken(Token::RndOpen, "(");
            return std::nullopt;
        }

        auto name = std::string(_lexer.currentLiteral());
        _lexer.nextToken(); // consume identifier

        if (_lexer.currentToken() != Token::Colon)
        {
            // Not annotated — push back identifier then '(' (reverse consumption order)
            _lexer.pushBackToken(Token::Identifier, name);
            _lexer.pushBackToken(Token::RndOpen, "(");
            _lexer.nextToken(); // re-read to restore current token to '('
            return std::nullopt;
        }

        // Annotated parameter: (name: type)
        _lexer.nextToken(); // consume ':'
        auto type = parseType();
        if (!type)
            return std::nullopt;
        if (_lexer.currentToken() != Token::RndClose)
        {
            _report.syntaxErrorWithSuggestions(currentLocation(),
                                               { "Add ')' to close annotated parameter" },
                                               currentContextSnippet(),
                                               "Expected ')' after parameter type annotation, got '{}'",
                                               _lexer.currentTokenText());
            return std::nullopt;
        }
        _lexer.nextToken(); // consume ')'
        return ast::TypedParameter(std::move(name), std::move(type));
    }

    return std::nullopt;
}

std::unique_ptr<ast::LetBindingStmt> Parser::parseLet()
{
    TRACE_SCOPE("parseLet");

    auto const letColumn = currentTokenColumn();
    auto const letLoc = _lexer.currentRange();
    auto const letLine = letLoc.begin.line;

    // Enter F# expression mode BEFORE consuming 'let' so that the next token
    // (the binding name) is tokenized with F# reserved symbols (including ':')
    _lexer.enterFSharpExpr();
    _lexer.nextToken(); // consume 'let'

    // Check for 'export' modifier
    bool const isExported = _lexer.currentToken() == Token::Identifier && _lexer.currentLiteral() == "export";
    if (isExported)
        _lexer.nextToken(); // consume 'export'

    // Check for 'mut' modifier
    bool const isMutable = _lexer.currentToken() == Token::Mut;
    if (isMutable)
        _lexer.nextToken(); // consume 'mut'

    // Check for 'rec' modifier
    bool const isRecursive = _lexer.currentToken() == Token::Rec;
    if (isRecursive)
    {
        if (isMutable)
        {
            _report.syntaxErrorWithSuggestions(
                currentLocation(),
                { "Use 'let rec' without 'mut'" },
                currentContextSnippet(),
                "'let mut rec' is not allowed; 'mut' and 'rec' are mutually exclusive");
            _lexer.leaveFSharpExpr();
            return nullptr;
        }
        if (isExported)
        {
            _report.syntaxErrorWithSuggestions(
                currentLocation(),
                { "Use 'let export' without 'rec'" },
                currentContextSnippet(),
                "'let export rec' is not allowed; functions cannot be exported");
            _lexer.leaveFSharpExpr();
            return nullptr;
        }
        _lexer.nextToken(); // consume 'rec'
    }

    // Check for destructuring pattern: let (x, y) = expr  or  let { x; y } = expr
    if (_lexer.currentToken() == Token::RndOpen || _lexer.currentToken() == Token::BraceOpen)
    {
        auto pat = (_lexer.currentToken() == Token::BraceOpen) ? parseRecordPattern() : parseTuplePattern();
        if (!pat)
        {
            _lexer.leaveFSharpExpr();
            return nullptr;
        }

        // Expect '='
        if (_lexer.currentToken() != Token::Equal)
        {
            _report.syntaxErrorWithSuggestions(currentLocation(),
                                               { "Add '=' followed by an expression" },
                                               currentContextSnippet(),
                                               "Expected '=' in destructuring let binding, got '{}'",
                                               _lexer.currentTokenText());
            _lexer.leaveFSharpExpr();
            return nullptr;
        }
        _lexer.nextToken(); // consume '='
        consumeNewlines();

        auto value = parseFSharpExpr();
        if (!value)
        {
            _lexer.leaveFSharpExpr();
            return nullptr;
        }

        _lexer.leaveFSharpExpr();
        auto result = std::make_unique<ast::LetBindingStmt>(isMutable, std::move(pat), std::move(value));
        result->location = result->value && result->value->location
                               ? SourceLocationRange { letLoc.begin, result->value->location->end }
                               : letLoc;
        return result;
    }

    // Expect identifier (binding name)
    if (_lexer.currentToken() != Token::Identifier)
    {
        _report.syntaxErrorWithSuggestions(currentLocation(),
                                           { "Provide a name for the let binding" },
                                           currentContextSnippet(),
                                           "Expected identifier after 'let', got '{}'",
                                           _lexer.currentTokenText());
        _lexer.leaveFSharpExpr();
        return nullptr;
    }

    std::string name = consumeLiteral();

    // Collect parameters (for function definitions)
    // Parameters can be bare identifiers or annotated: (x: int)
    std::vector<ast::TypedParameter> parameters;
    while (true)
    {
        auto param = parseTypedParameter();
        if (!param)
            break;
        parameters.push_back(std::move(*param));
    }

    // Check for return type annotation (or binding type for simple bindings)
    std::optional<TypePtr> returnType;
    if (_lexer.currentToken() == Token::Colon)
    {
        _lexer.nextToken(); // consume ':'
        returnType = parseType();
        if (!returnType)
        {
            _lexer.leaveFSharpExpr();
            return nullptr;
        }
    }

    // Property syntax: let Name with get/set ...
    // Unambiguous: Token::With after a property name with no parameters is distinct from match-with
    // Allow 'with' on the next line after the property name (consume newlines with pushback)
    auto const skippedNewlinesBeforeWith = consumeNewlines();
    if (_lexer.currentToken() == Token::With && parameters.empty() && !isRecursive)
    {
        return parsePropertyAccessors(
            isExported, isMutable, std::move(name), std::move(returnType), letColumn);
    }
    // Not a property — push back newline to preserve statement boundary
    if (skippedNewlinesBeforeWith > 0)
        _lexer.pushBackToken(Token::LineFeed, "\n");

    // Validate: 'let rec' requires parameters (must be a function definition)
    if (isRecursive && parameters.empty())
    {
        _report.syntaxErrorWithSuggestions(
            currentLocation(),
            { "Add parameters to define a recursive function: 'let rec f x = ...'" },
            currentContextSnippet(),
            "'let rec' requires parameters (must be a function definition)");
        _lexer.leaveFSharpExpr();
        return nullptr;
    }

    // Expect '='
    if (_lexer.currentToken() != Token::Equal)
    {
        _report.syntaxErrorWithSuggestions(currentLocation(),
                                           { "Add '=' followed by an expression" },
                                           currentContextSnippet(),
                                           "Expected '=' in let binding, got '{}'",
                                           _lexer.currentTokenText());
        _lexer.leaveFSharpExpr();
        return nullptr;
    }
    _lexer.nextToken(); // consume '='
    consumeNewlines();

    // Parse the value expression.
    // Function bodies that start on a new line use sequence parsing for multi-statement support
    // (indentation-based). Single-line bodies use parseFSharpExpr() to avoid consuming tokens
    // beyond the function body (e.g., match arm's consumeNewlines() can eat ';' separators).
    auto const bodyOnNewLine = !parameters.empty() && _lexer.currentRange().begin.line > letLine;
    auto value = bodyOnNewLine ? parseFSharpExprSequence(letColumn) : parseFSharpExpr();
    if (!value)
    {
        _lexer.leaveFSharpExpr();
        return nullptr;
    }

    auto result = std::make_unique<ast::LetBindingStmt>(isExported,
                                                        isMutable,
                                                        isRecursive,
                                                        std::move(name),
                                                        std::move(parameters),
                                                        std::move(returnType),
                                                        std::move(value));

    // Parse 'and' bindings for mutual recursion: let rec f ... and g ...
    while (isRecursive)
    {
        auto const skippedNewlines = consumeNewlines();
        if (_lexer.currentToken() != Token::And)
        {
            if (skippedNewlines)
                _lexer.pushBackToken(Token::LineFeed, "\n");
            break;
        }
        _lexer.nextToken(); // consume 'and'

        if (_lexer.currentToken() != Token::Identifier)
        {
            _report.syntaxErrorWithSuggestions(currentLocation(),
                                               { "Provide a name for the 'and' binding" },
                                               currentContextSnippet(),
                                               "Expected identifier after 'and', got '{}'",
                                               _lexer.currentTokenText());
            _lexer.leaveFSharpExpr();
            return nullptr;
        }

        auto andName = consumeLiteral();

        std::vector<ast::TypedParameter> andParams;
        while (true)
        {
            auto param = parseTypedParameter();
            if (!param)
                break;
            andParams.push_back(std::move(*param));
        }

        // Check for return type annotation
        std::optional<TypePtr> andReturnType;
        if (_lexer.currentToken() == Token::Colon)
        {
            _lexer.nextToken(); // consume ':'
            andReturnType = parseType();
            if (!andReturnType)
            {
                _lexer.leaveFSharpExpr();
                return nullptr;
            }
        }

        if (andParams.empty())
        {
            _report.syntaxErrorWithSuggestions(
                currentLocation(),
                { "Add parameters: 'and f x = ...'" },
                currentContextSnippet(),
                "'and' binding in 'let rec' must be a function definition (needs parameters)");
            _lexer.leaveFSharpExpr();
            return nullptr;
        }

        if (_lexer.currentToken() != Token::Equal)
        {
            _report.syntaxErrorWithSuggestions(currentLocation(),
                                               { "Add '=' followed by function body" },
                                               currentContextSnippet(),
                                               "Expected '=' in 'and' binding, got '{}'",
                                               _lexer.currentTokenText());
            _lexer.leaveFSharpExpr();
            return nullptr;
        }
        _lexer.nextToken(); // consume '='
        consumeNewlines();

        auto const andBodyOnNewLine = !andParams.empty() && _lexer.currentRange().begin.line > letLine;
        auto andValue = andBodyOnNewLine ? parseFSharpExprSequence(letColumn) : parseFSharpExpr();
        if (!andValue)
        {
            _lexer.leaveFSharpExpr();
            return nullptr;
        }

        result->andBindings.push_back(ast::AndBinding {
            std::move(andName), std::move(andParams), std::move(andReturnType), std::move(andValue) });
    }

    // Register known F# function names for bare top-level call dispatch
    if (result->isFunction())
    {
        _knownFSharpFunctions.insert(result->name);
        for (auto const& ab: result->andBindings)
            _knownFSharpFunctions.insert(ab.name);

        // Track variadic functions separately for shell-mode argument parsing
        auto const hasVariadic =
            std::ranges::any_of(result->parameters, [](auto const& p) { return p.isVariadic; });
        if (hasVariadic)
        {
            _knownVariadicFunctions.insert(result->name);
            // Remove from regular F# functions so variadic dispatch takes priority
            _knownFSharpFunctions.erase(result->name);
        }
    }
    else if (dynamic_cast<ast::LambdaExpr const*>(result->value.get()) != nullptr)
    {
        _knownFSharpFunctions.insert(result->name);
    }
    else
    {
        _knownFSharpFunctions.insert(result->name);
    }

    // Set location spanning from 'let' keyword to end of value (or last and-binding)
    auto endLoc = letLoc;
    if (!result->andBindings.empty() && result->andBindings.back().value
        && result->andBindings.back().value->location)
        endLoc = *result->andBindings.back().value->location;
    else if (result->value && result->value->location)
        endLoc = *result->value->location;
    result->location = SourceLocationRange { letLoc.begin, endLoc.end };

    _lexer.leaveFSharpExpr();
    return result;
}

std::unique_ptr<ast::LetBindingStmt> Parser::parsePropertyAccessors(
    bool isExported, bool isMutable, std::string name, std::optional<TypePtr> returnType, size_t letColumn)
{
    TRACE_SCOPE("parsePropertyAccessors");

    // We're at Token::With, F# mode is already active from parseLet()
    _lexer.nextToken(); // consume 'with'
    consumeNewlines();  // allow 'get'/'set' on the next line after 'with'

    auto result = std::make_unique<ast::LetBindingStmt>(isExported,
                                                        isMutable,
                                                        false,
                                                        std::move(name),
                                                        std::vector<ast::TypedParameter> {},
                                                        std::move(returnType),
                                                        nullptr);

    // Parse first accessor: must be 'get' or 'set'
    if (_lexer.currentToken() != Token::Identifier
        || (_lexer.currentLiteral() != "get" && _lexer.currentLiteral() != "set"))
    {
        _report.syntaxErrorWithSuggestions(currentLocation(),
                                           { "Use 'get' or 'set' after 'with'" },
                                           currentContextSnippet(),
                                           "Expected 'get' or 'set' after 'with', got '{}'",
                                           _lexer.currentTokenText());
        _lexer.leaveFSharpExpr();
        return nullptr;
    }

    auto const parseAccessor = [&](bool isSetter) -> std::unique_ptr<ast::PropertyAccessor> {
        _lexer.nextToken(); // consume 'get'/'set'

        // Expect '('
        if (_lexer.currentToken() != Token::RndOpen)
        {
            _report.syntaxErrorWithSuggestions(
                currentLocation(),
                { isSetter ? "Use 'set (value) = ...'" : "Use 'get () = ...'" },
                currentContextSnippet(),
                "Expected '(' after '{}', got '{}'",
                isSetter ? "set" : "get",
                _lexer.currentTokenText());
            return nullptr;
        }
        _lexer.nextToken(); // consume '('

        auto accessor = std::make_unique<ast::PropertyAccessor>();

        if (isSetter)
        {
            // Parse parameter name
            if (_lexer.currentToken() != Token::Identifier)
            {
                _report.syntaxErrorWithSuggestions(currentLocation(),
                                                   { "Provide a parameter name: 'set (value) = ...'" },
                                                   currentContextSnippet(),
                                                   "Expected parameter name in setter, got '{}'",
                                                   _lexer.currentTokenText());
                return nullptr;
            }
            accessor->paramName = consumeLiteral();

            // Optional type annotation: (value: int)
            if (_lexer.currentToken() == Token::Colon)
            {
                _lexer.nextToken(); // consume ':'
                accessor->paramType = parseType();
                if (!accessor->paramType)
                    return nullptr;
            }
        }

        // Expect ')'
        if (_lexer.currentToken() != Token::RndClose)
        {
            _report.syntaxErrorWithSuggestions(currentLocation(),
                                               { "Close the parameter list with ')'" },
                                               currentContextSnippet(),
                                               "Expected ')' in {} accessor, got '{}'",
                                               isSetter ? "set" : "get",
                                               _lexer.currentTokenText());
            return nullptr;
        }
        _lexer.nextToken(); // consume ')'

        // Optional return type annotation
        if (_lexer.currentToken() == Token::Colon)
        {
            _lexer.nextToken(); // consume ':'
            accessor->returnType = parseType();
            if (!accessor->returnType)
                return nullptr;
        }

        // Expect '='
        if (_lexer.currentToken() != Token::Equal)
        {
            _report.syntaxErrorWithSuggestions(currentLocation(),
                                               { "Add '=' followed by the accessor body" },
                                               currentContextSnippet(),
                                               "Expected '=' in {} accessor, got '{}'",
                                               isSetter ? "set" : "get",
                                               _lexer.currentTokenText());
            return nullptr;
        }
        auto const eqLine = _lexer.currentRange().begin.line;
        _lexer.nextToken(); // consume '='
        consumeNewlines();

        // Multi-line body support: use sequence parsing when body starts on a new line
        auto const bodyOnNewLine = _lexer.currentRange().begin.line > eqLine;
        accessor->body = bodyOnNewLine ? parseFSharpExprSequence(letColumn) : parseFSharpExpr();
        if (!accessor->body)
            return nullptr;

        return accessor;
    };

    auto const firstIsGet = _lexer.currentLiteral() == "get";
    if (firstIsGet)
    {
        result->getter = parseAccessor(false);
        if (!result->getter)
        {
            _lexer.leaveFSharpExpr();
            return nullptr;
        }
    }
    else
    {
        result->setter = parseAccessor(true);
        if (!result->setter)
        {
            _lexer.leaveFSharpExpr();
            return nullptr;
        }
    }

    // Check for 'and' second accessor
    auto const skippedNewlines = consumeNewlines();
    if (_lexer.currentToken() == Token::And)
    {
        _lexer.nextToken(); // consume 'and'

        std::string const expectSecond = firstIsGet ? "set" : "get";
        if (_lexer.currentToken() != Token::Identifier || _lexer.currentLiteral() != expectSecond)
        {
            _report.syntaxErrorWithSuggestions(currentLocation(),
                                               { std::format("Use 'and {}' after the {} accessor",
                                                             expectSecond,
                                                             firstIsGet ? "get" : "set") },
                                               currentContextSnippet(),
                                               "Expected '{}' after 'and', got '{}'",
                                               expectSecond,
                                               _lexer.currentTokenText());
            _lexer.leaveFSharpExpr();
            return nullptr;
        }

        auto const secondIsSetter = (expectSecond == "set");
        auto secondAccessor = parseAccessor(secondIsSetter);
        if (!secondAccessor)
        {
            _lexer.leaveFSharpExpr();
            return nullptr;
        }

        if (secondIsSetter)
            result->setter = std::move(secondAccessor);
        else
            result->getter = std::move(secondAccessor);
    }
    else if (skippedNewlines)
    {
        _lexer.pushBackToken(Token::LineFeed, "\n");
    }

    // Register property name as known F# function for completion/dispatch
    _knownFSharpFunctions.insert(result->name);

    _lexer.leaveFSharpExpr();
    return result;
}

std::unique_ptr<ast::LetInExpr> Parser::parseLetInExpr()
{
    TRACE_SCOPE("parseLetInExpr");
    auto const letColumn = currentTokenColumn();
    auto const letLine = _lexer.currentRange().begin.line;
    auto const letLoc = _lexer.currentRange();
    _lexer.nextToken(); // consume 'let'

    // Check for 'rec' modifier
    auto const isRecursive = _lexer.currentToken() == Token::Rec;
    if (isRecursive)
        _lexer.nextToken(); // consume 'rec'

    // Check for destructuring pattern: let (x, y) = expr in body
    if (!isRecursive && _lexer.currentToken() == Token::RndOpen)
    {
        auto pat = parseTuplePattern();
        if (!pat)
            return nullptr;

        // Expect '='
        if (_lexer.currentToken() != Token::Equal)
        {
            _report.syntaxErrorWithSuggestions(currentLocation(),
                                               { "Add '=' followed by an expression" },
                                               currentContextSnippet(),
                                               "Expected '=' in destructuring let-in binding, got '{}'",
                                               _lexer.currentTokenText());
            return nullptr;
        }
        _lexer.nextToken(); // consume '='
        consumeNewlines();

        auto value = parseFSharpExpr();
        if (!value)
            return nullptr;

        // Expect 'in' keyword
        consumeNewlines();
        if (_lexer.currentToken() != Token::Identifier || _lexer.currentLiteral() != "in")
        {
            _report.syntaxErrorWithSuggestions(currentLocation(),
                                               { "Add 'in' followed by body expression" },
                                               currentContextSnippet(),
                                               "Expected 'in' after destructuring value, got '{}'",
                                               _lexer.currentTokenText());
            return nullptr;
        }
        _lexer.nextToken(); // consume 'in'
        consumeNewlines();

        auto body = parseFSharpExpr();
        if (!body)
            return nullptr;

        auto const endLoc = body->location;
        auto node = std::make_unique<ast::LetInExpr>(std::move(pat), std::move(value), std::move(body));
        node->location = endLoc ? SourceLocationRange { letLoc.begin, endLoc->end } : letLoc;
        return node;
    }

    // Expect identifier (binding name)
    if (_lexer.currentToken() != Token::Identifier)
    {
        _report.syntaxErrorWithSuggestions(currentLocation(),
                                           { "Provide a name for the let binding" },
                                           currentContextSnippet(),
                                           "Expected identifier after 'let', got '{}'",
                                           _lexer.currentTokenText());
        return nullptr;
    }

    auto name = consumeLiteral();

    // Collect parameters (for function definitions)
    std::vector<ast::TypedParameter> parameters;
    while (true)
    {
        // Stop if we see 'in' keyword (not a parameter)
        if (_lexer.currentToken() == Token::Identifier && _lexer.currentLiteral() == "in")
            break;
        auto param = parseTypedParameter();
        if (!param)
            break;
        parameters.push_back(std::move(*param));
    }

    // Check for return type annotation
    std::optional<TypePtr> returnType;
    if (_lexer.currentToken() == Token::Colon)
    {
        _lexer.nextToken(); // consume ':'
        returnType = parseType();
        if (!returnType)
            return nullptr;
    }

    // Validate: 'let rec' requires parameters
    if (isRecursive && parameters.empty())
    {
        _report.syntaxErrorWithSuggestions(
            currentLocation(),
            { "Add parameters: 'let rec f x = ... in ...'" },
            currentContextSnippet(),
            "'let rec' in expression requires parameters (must be a function definition)");
        return nullptr;
    }

    // Expect '='
    if (_lexer.currentToken() != Token::Equal)
    {
        _report.syntaxErrorWithSuggestions(currentLocation(),
                                           { "Add '=' followed by an expression" },
                                           currentContextSnippet(),
                                           "Expected '=' in let-in binding, got '{}'",
                                           _lexer.currentTokenText());
        return nullptr;
    }
    _lexer.nextToken(); // consume '='
    consumeNewlines();

    // Parse the value expression.
    // Function bodies that start on a new line use sequence parsing for multi-statement support
    // (indentation-based). Single-line bodies use parseFSharpExpr() to avoid consuming tokens
    // beyond the function body.
    auto const bodyOnNewLine = !parameters.empty() && _lexer.currentRange().begin.line > letLine;
    auto value = bodyOnNewLine ? parseFSharpExprSequence(letColumn) : parseFSharpExpr();
    if (!value)
        return nullptr;

    // Expect 'in' keyword
    consumeNewlines();
    if (_lexer.currentToken() != Token::Identifier || _lexer.currentLiteral() != "in")
    {
        _report.syntaxErrorWithSuggestions(currentLocation(),
                                           { "Add 'in' followed by body expression" },
                                           currentContextSnippet(),
                                           "Expected 'in' after let binding value, got '{}'",
                                           _lexer.currentTokenText());
        return nullptr;
    }
    _lexer.nextToken(); // consume 'in'
    consumeNewlines();

    // Parse the body expression
    auto body = parseFSharpExpr();
    if (!body)
        return nullptr;

    auto const endLoc = body->location;
    auto node = std::make_unique<ast::LetInExpr>(isRecursive,
                                                 std::move(name),
                                                 std::move(parameters),
                                                 std::move(returnType),
                                                 std::move(value),
                                                 std::move(body));
    node->location = endLoc ? SourceLocationRange { letLoc.begin, endLoc->end } : letLoc;
    return node;
}

std::unique_ptr<ast::Expr> Parser::parseFSharpExpr()
{
    TRACE_SCOPE("parseFSharpExpr");
    auto expr = parseFSharpPipeline();
    if (!expr)
        return nullptr;

    // Handle mutable assignment as expression: identifier <- expr
    if (_lexer.currentToken() == Token::LeftArrow)
    {
        if (auto* identExpr = dynamic_cast<ast::IdentifierExpr*>(expr.get()))
        {
            auto name = identExpr->name;
            _lexer.nextToken(); // consume '<-'
            auto value = parseFSharpExpr();
            if (!value)
                return nullptr;
            auto mutExpr = std::make_unique<ast::MutAssignExpr>(std::move(name), std::move(value));
            mutExpr->location =
                (expr->location && mutExpr->value->location)
                    ? SourceLocationRange { expr->location->begin, mutExpr->value->location->end }
                    : expr->location;
            return mutExpr;
        }
    }

    return expr;
}

std::unique_ptr<ast::Expr> Parser::parseFSharpTupleExpr()
{
    TRACE_SCOPE("parseFSharpTupleExpr");
    auto first = parseFSharpExpr();
    if (!first)
        return nullptr;

    if (_lexer.currentToken() != Token::Comma)
        return first;

    std::vector<std::unique_ptr<ast::Expr>> elements;
    elements.push_back(std::move(first));
    while (_lexer.currentToken() == Token::Comma)
    {
        _lexer.nextToken(); // consume ','
        auto elem = parseFSharpExpr();
        if (!elem)
        {
            _report.syntaxErrorWithSuggestions(
                currentLocation(), {}, currentContextSnippet(), "Expected expression after ',' in tuple");
            return nullptr;
        }
        elements.push_back(std::move(elem));
    }
    return std::make_unique<ast::TupleExpr>(std::move(elements));
}

std::unique_ptr<ast::Expr> Parser::parseFSharpPipeline()
{
    TRACE_SCOPE("parseFSharpPipeline");
    auto left = parseFSharpComposition();
    if (!left)
        return nullptr;

    while (true)
    {
        auto const skippedNewlines = consumeUntilNotOneOf(Token::LineFeed);
        if (_lexer.currentToken() != Token::ForwardPipe)
        {
            if (skippedNewlines)
                _lexer.pushBackToken(Token::LineFeed, "\n");
            break;
        }
        _lexer.nextToken(); // consume '|>'
        auto right = parseFSharpComposition();
        if (!right)
            return nullptr;
        left = std::make_unique<ast::PipelineExpr>(std::move(left), std::move(right));
    }
    return left;
}

std::unique_ptr<ast::Expr> Parser::parseFSharpComposition()
{
    TRACE_SCOPE("parseFSharpComposition");
    auto const savedPlaceholderCount = _placeholderCount;

    auto left = parseFSharpOr();
    if (!left)
        return nullptr;

    auto hadComposition = false;
    while (_lexer.currentToken() == Token::GreaterGreater || _lexer.currentToken() == Token::LessLess)
    {
        hadComposition = true;
        auto const isForward = _lexer.currentToken() == Token::GreaterGreater;
        _lexer.nextToken(); // consume '>>' or '<<'
        auto right = parseFSharpOr();
        if (!right)
            return nullptr;

        left = std::make_unique<ast::CompositionExpr>(isForward ? ast::CompositionOp::Forward
                                                                : ast::CompositionOp::Backward,
                                                      std::move(left),
                                                      std::move(right));
    }

    // Unparenthesized placeholder lambda wrapping: _ + 1 → fun __x -> __x + 1
    auto const newPlaceholders = _placeholderCount - savedPlaceholderCount;
    if (newPlaceholders > 0 && !_placeholderScopeActive && !hadComposition)
    {
        // Don't wrap bare _ (identity function) — preserve existing behavior
        auto* ident = dynamic_cast<ast::IdentifierExpr*>(left.get());
        if (!ident || ident->name != "__x")
        {
            _placeholderCount = savedPlaceholderCount;
            left = std::make_unique<ast::PlaceholderLambdaExpr>(std::move(left), false);
        }
    }

    return left;
}

std::unique_ptr<ast::Expr> Parser::parseFSharpOr()
{
    TRACE_SCOPE("parseFSharpOr");
    auto left = parseFSharpAnd();
    if (!left)
        return nullptr;

    while (_lexer.currentToken() == Token::PipePipe)
    {
        _lexer.nextToken(); // consume '||'
        auto right = parseFSharpAnd();
        if (!right)
            return nullptr;
        left = std::make_unique<ast::BinaryExpr>(ast::BinaryOp::Or, std::move(left), std::move(right));
    }
    return left;
}

std::unique_ptr<ast::Expr> Parser::parseFSharpAnd()
{
    TRACE_SCOPE("parseFSharpAnd");
    auto left = parseFSharpCons();
    if (!left)
        return nullptr;

    while (_lexer.currentToken() == Token::AmpAmp)
    {
        _lexer.nextToken(); // consume '&&'
        auto right = parseFSharpCons();
        if (!right)
            return nullptr;
        left = std::make_unique<ast::BinaryExpr>(ast::BinaryOp::And, std::move(left), std::move(right));
    }
    return left;
}

std::unique_ptr<ast::Expr> Parser::parseFSharpCons()
{
    TRACE_SCOPE("parseFSharpCons");
    auto left = parseFSharpComparison();
    if (!left)
        return nullptr;

    auto const skippedNewlines = consumeUntilNotOneOf(Token::LineFeed);

    // Check for '::' cons operator (right-associative)
    if (_lexer.currentToken() == Token::ColonColon)
    {
        _lexer.nextToken();             // consume '::'
        auto right = parseFSharpCons(); // Right-associative: recurse
        if (!right)
        {
            _report.syntaxErrorWithSuggestions(
                currentLocation(), {}, currentContextSnippet(), "Expected expression after '::'");
            return nullptr;
        }
        return std::make_unique<ast::ConsExpr>(std::move(left), std::move(right));
    }

    // Check for '@' list concatenation operator (right-associative)
    if (_lexer.currentToken() == Token::At)
    {
        _lexer.nextToken();             // consume '@'
        auto right = parseFSharpCons(); // Right-associative: recurse
        if (!right)
        {
            _report.syntaxErrorWithSuggestions(
                currentLocation(), {}, currentContextSnippet(), "Expected expression after '@'");
            return nullptr;
        }
        return std::make_unique<ast::ConcatListExpr>(std::move(left), std::move(right));
    }

    // Push back the newline if we consumed one but didn't find a continuation operator,
    // so the caller can see the statement boundary.
    if (skippedNewlines)
        _lexer.pushBackToken(Token::LineFeed, "\n");

    return left;
}

std::unique_ptr<ast::Expr> Parser::parseFSharpComparison()
{
    TRACE_SCOPE("parseFSharpComparison");
    auto left = parseFSharpRange();
    if (!left)
        return nullptr;

    while (true)
    {
        ast::BinaryOp op;
        bool found = false;

        switch (_lexer.currentToken())
        {
            case Token::Less:
                op = ast::BinaryOp::Lt;
                found = true;
                break;
            case Token::Greater:
                op = ast::BinaryOp::Gt;
                found = true;
                break;
            case Token::LessEqual:
                op = ast::BinaryOp::Le;
                found = true;
                break;
            case Token::GreaterEqual:
                op = ast::BinaryOp::Ge;
                found = true;
                break;
            case Token::EqualEqual:
                op = ast::BinaryOp::Eq;
                found = true;
                break;
            case Token::NotEqual:
                op = ast::BinaryOp::Ne;
                found = true;
                break;
            default: break;
        }

        if (!found)
            break;

        _lexer.nextToken();
        auto right = parseFSharpRange();
        if (!right)
            return nullptr;
        left = std::make_unique<ast::BinaryExpr>(op, std::move(left), std::move(right));
    }
    return left;
}

std::unique_ptr<ast::Expr> Parser::parseFSharpRange()
{
    TRACE_SCOPE("parseFSharpRange");
    auto start = parseFSharpAddSub();
    if (!start || _lexer.currentToken() != Token::DotDot)
        return start;

    _lexer.nextToken(); // consume first ..
    auto second = parseFSharpAddSub();
    if (!second)
        return nullptr;

    if (_lexer.currentToken() == Token::DotDot)
    {
        _lexer.nextToken(); // consume second ..
        auto endExpr = parseFSharpAddSub();
        if (!endExpr)
            return nullptr;
        auto result =
            std::make_unique<ast::ListRangeExpr>(std::move(start), std::move(second), std::move(endExpr));
        if (result->start->location && result->end->location)
            result->location =
                SourceLocationRange { result->start->location->begin, result->end->location->end };
        return result;
    }

    auto result = std::make_unique<ast::ListRangeExpr>(std::move(start), nullptr, std::move(second));
    if (result->start->location && result->end->location)
        result->location = SourceLocationRange { result->start->location->begin, result->end->location->end };
    return result;
}

std::unique_ptr<ast::Expr> Parser::parseFSharpAddSub()
{
    TRACE_SCOPE("parseFSharpAddSub");
    auto left = parseFSharpMulDivMod();
    if (!left)
        return nullptr;

    while (true)
    {
        ast::BinaryOp op;
        bool found = false;

        switch (_lexer.currentToken())
        {
            case Token::Plus:
                op = ast::BinaryOp::Add;
                found = true;
                break;
            case Token::Minus:
                op = ast::BinaryOp::Sub;
                found = true;
                break;
            default: break;
        }

        if (!found)
            break;

        _lexer.nextToken();
        auto right = parseFSharpMulDivMod();
        if (!right)
            return nullptr;
        left = std::make_unique<ast::BinaryExpr>(op, std::move(left), std::move(right));
    }
    return left;
}

std::unique_ptr<ast::Expr> Parser::parseFSharpMulDivMod()
{
    TRACE_SCOPE("parseFSharpMulDivMod");
    auto left = parseFSharpPow();
    if (!left)
        return nullptr;

    while (true)
    {
        ast::BinaryOp op;
        bool found = false;

        switch (_lexer.currentToken())
        {
            case Token::Star:
                op = ast::BinaryOp::Mul;
                found = true;
                break;
            case Token::Slash:
                op = ast::BinaryOp::Div;
                found = true;
                break;
            case Token::Percent:
                op = ast::BinaryOp::Mod;
                found = true;
                break;
            default: break;
        }

        if (!found)
            break;

        _lexer.nextToken();
        auto right = parseFSharpPow();
        if (!right)
            return nullptr;
        left = std::make_unique<ast::BinaryExpr>(op, std::move(left), std::move(right));
    }
    return left;
}

std::unique_ptr<ast::Expr> Parser::parseFSharpPow()
{
    TRACE_SCOPE("parseFSharpPow");
    auto left = parseFSharpUnary();
    if (!left)
        return nullptr;

    // ** is right-associative: 2**3**4 = 2**(3**4)
    if (_lexer.currentToken() == Token::StarStar)
    {
        _lexer.nextToken();
        auto right = parseFSharpPow(); // Recursive for right-associativity
        if (!right)
            return nullptr;
        return std::make_unique<ast::BinaryExpr>(ast::BinaryOp::Pow, std::move(left), std::move(right));
    }
    return left;
}

std::unique_ptr<ast::Expr> Parser::parseFSharpUnary()
{
    TRACE_SCOPE("parseFSharpUnary");

    // Handle negation: -expr
    if (_lexer.currentToken() == Token::Minus)
    {
        auto const opLoc = _lexer.currentRange();
        _lexer.nextToken();
        auto operand = parseFSharpUnary();
        if (!operand)
            return nullptr;
        auto const endLoc = operand->location;
        auto node = std::make_unique<ast::UnaryExpr>(ast::UnaryOp::Neg, std::move(operand));
        node->location = endLoc ? SourceLocationRange { opLoc.begin, endLoc->end } : opLoc;
        return node;
    }

    // Handle logical not: !expr
    if (_lexer.currentToken() == Token::Not)
    {
        auto const opLoc = _lexer.currentRange();
        _lexer.nextToken();
        auto operand = parseFSharpUnary();
        if (!operand)
            return nullptr;
        auto const endLoc = operand->location;
        auto node = std::make_unique<ast::UnaryExpr>(ast::UnaryOp::Not, std::move(operand));
        node->location = endLoc ? SourceLocationRange { opLoc.begin, endLoc->end } : opLoc;
        return node;
    }

    return parseFSharpApplication();
}

std::unique_ptr<ast::Expr> Parser::parseFSharpApplication()
{
    TRACE_SCOPE("parseFSharpApplication");
    auto func = parseFSharpPostfix();
    if (!func)
        return nullptr;

    // Function application: func arg1 arg2 ...
    // Continue while we see primary expressions (identifiers, numbers, parens)
    while (isFSharpPrimary())
    {
        auto arg = parseFSharpPostfix();
        if (!arg)
            break;

        // Computation expression: wrap block arguments as zero-arg thunks
        // f { body } → f (fun () -> { body })
        if (dynamic_cast<ast::BlockExpr*>(arg.get()) != nullptr)
            arg = std::make_unique<ast::LambdaExpr>(
                std::vector<ast::TypedParameter> { ast::TypedParameter::unitParam() }, std::move(arg));

        func = std::make_unique<ast::ApplicationExpr>(std::move(func), std::move(arg));
    }

    // Option default operator: expr ?| default
    // Handled after application so `find pred list ?| default` parses as `(find pred list) ?| default`
    if (_lexer.currentToken() == Token::QuestionPipe)
    {
        _lexer.nextToken(); // consume '?|'
        auto defaultExpr = parseFSharpExpr();
        if (!defaultExpr)
            return nullptr;
        func = std::make_unique<ast::OptionDefaultExpr>(std::move(func), std::move(defaultExpr));
    }

    return func;
}

std::unique_ptr<ast::Expr> Parser::parseFSharpPostfix()
{
    TRACE_SCOPE("parseFSharpPostfix");

    auto const savedPlaceholderCount = _placeholderCount;
    auto expr = parseFSharpPrimary();
    if (!expr)
        return nullptr;

    // Handle postfix operators: field access (.) and error propagation (?)
    auto hasPostfixOps = false;
    for (;;)
    {
        if (_lexer.currentToken() == Token::Dot)
        {
            _lexer.nextToken(); // consume '.'
            if (_lexer.currentToken() != Token::Identifier && _lexer.currentToken() != Token::Number)
            {
                _report.syntaxErrorWithSuggestions(currentLocation(),
                                                   { "Provide a field name or numeric index after '.'" },
                                                   currentContextSnippet(),
                                                   "Expected field name or index after '.', got '{}'",
                                                   _lexer.currentTokenText());
                return nullptr;
            }
            auto fieldName = std::string(_lexer.currentLiteral());
            auto const fieldLoc = _lexer.currentRange();
            _lexer.nextToken(); // consume field name
            auto const beginLoc = expr->location;
            expr = std::make_unique<ast::FieldAccessExpr>(std::move(expr), std::move(fieldName));
            expr->location = beginLoc ? SourceLocationRange { beginLoc->begin, fieldLoc.end } : fieldLoc;
            hasPostfixOps = true;
        }
        else if (_lexer.currentToken() == Token::QuestionDot)
        {
            _lexer.nextToken(); // consume '?.'
            if (_lexer.currentToken() != Token::Identifier)
            {
                _report.syntaxErrorWithSuggestions(currentLocation(),
                                                   { "Provide a field name after '?.'" },
                                                   currentContextSnippet(),
                                                   "Expected field name after '?.', got '{}'",
                                                   _lexer.currentTokenText());
                return nullptr;
            }
            auto fieldName = _lexer.currentLiteral();
            auto const fieldLoc = _lexer.currentRange();
            _lexer.nextToken(); // consume field name
            auto const beginLoc = expr->location;
            expr = std::make_unique<ast::OptionalChainExpr>(std::move(expr), std::move(fieldName));
            expr->location = beginLoc ? SourceLocationRange { beginLoc->begin, fieldLoc.end } : fieldLoc;
            hasPostfixOps = true;
        }
        else if (_lexer.currentToken() == Token::Question)
        {
            auto const qLoc = _lexer.currentRange();
            auto const beginLoc = expr->location;
            _lexer.nextToken(); // consume '?'
            expr = std::make_unique<ast::TryExpr>(std::move(expr));
            expr->location = beginLoc ? SourceLocationRange { beginLoc->begin, qLoc.end } : qLoc;
            hasPostfixOps = true;
        }
        else
        {
            break;
        }
    }

    // Bare postfix placeholder wrapping: _.field → fun __x -> __x.field
    auto const newPlaceholders = _placeholderCount - savedPlaceholderCount;
    if (newPlaceholders > 0 && hasPostfixOps && !_placeholderScopeActive && !isBinaryOperatorToken())
    {
        _placeholderCount = savedPlaceholderCount;
        return std::make_unique<ast::PlaceholderLambdaExpr>(std::move(expr), false);
    }

    return expr;
}

std::unique_ptr<ast::Expr> Parser::parseTryWith()
{
    TRACE_SCOPE("parseTryWith");

    // try expr with | pattern -> handler | ...
    // try expr finally cleanup
    auto const tryLoc = _lexer.currentRange();
    _lexer.nextToken(); // consume 'try'

    consumeNewlines(); // allow body on next line

    // Parse the body expression
    auto body = parseFSharpExpr();
    if (!body)
        return nullptr;

    consumeNewlines();

    // Branch on 'with' vs 'finally'
    if (_lexer.currentToken() == Token::Finally)
    {
        _lexer.nextToken(); // consume 'finally'

        consumeNewlines(); // allow cleanup expression on next line

        auto cleanup = parseFSharpExpr();
        if (!cleanup)
            return nullptr;

        auto const endLoc = cleanup->location;
        auto node = std::make_unique<ast::TryFinallyExpr>(std::move(body), std::move(cleanup));
        node->location = endLoc ? SourceLocationRange { tryLoc.begin, endLoc->end } : tryLoc;
        return node;
    }

    // Expect 'with' keyword
    if (_lexer.currentToken() != Token::With)
    {
        _report.syntaxErrorWithSuggestions(currentLocation(),
                                           { "Add 'with' or 'finally' after try body" },
                                           currentContextSnippet(),
                                           "Expected 'with' or 'finally' after try body, got '{}'",
                                           _lexer.currentTokenText());
        return nullptr;
    }
    _lexer.nextToken(); // consume 'with'

    // Parse handlers: | pattern when guard -> body
    // Skip newlines between 'with' and first '|', and between handler arms.
    std::vector<ast::MatchArm> handlers;
    while (true)
    {
        auto const skippedNewlines = consumeNewlines();
        if (_lexer.currentToken() != Token::Pipe)
        {
            if (skippedNewlines)
                _lexer.pushBackToken(Token::LineFeed, "\n");
            break;
        }

        auto const handlerPipeColumn = currentTokenColumn();
        _lexer.nextToken(); // consume '|'

        // Parse pattern
        auto pat = parsePattern();
        if (!pat)
            return nullptr;

        // Parse optional guard: when expr
        std::unique_ptr<ast::Expr> guard = nullptr;
        if (_lexer.currentToken() == Token::When)
        {
            _lexer.nextToken(); // consume 'when'
            guard = parseFSharpExpr();
            if (!guard)
                return nullptr;
        }

        // Expect '->'
        if (_lexer.currentToken() != Token::Arrow)
        {
            _report.syntaxErrorWithSuggestions(currentLocation(),
                                               { "Use '->' to separate pattern from handler body" },
                                               currentContextSnippet(),
                                               "Expected '->' in try-with handler, got '{}'",
                                               _lexer.currentTokenText());
            return nullptr;
        }
        auto const handlerArrowLine = _lexer.currentRange().begin.line;
        _lexer.nextToken();                    // consume '->'
        consumeUntilNotOneOf(Token::LineFeed); // allow handler body on next line

        // Parse handler body — use sequence parsing for multi-line bodies
        auto const handlerBodyOnNewLine = _lexer.currentRange().begin.line > handlerArrowLine;
        auto handlerBody =
            handlerBodyOnNewLine ? parseFSharpExprSequence(handlerPipeColumn) : parseFSharpExpr();
        if (!handlerBody)
            return nullptr;

        handlers.emplace_back(std::move(pat), std::move(guard), std::move(handlerBody));
    }

    if (handlers.empty())
    {
        _report.syntaxErrorWithSuggestions(currentLocation(),
                                           { "Add at least one handler: | pattern -> expr" },
                                           currentContextSnippet(),
                                           "Expected at least one handler in try-with expression");
        return nullptr;
    }

    auto node = std::make_unique<ast::TryWithExpr>(std::move(body), std::move(handlers));
    auto const& lastHandler = node->handlers.back();
    if (lastHandler.body && lastHandler.body->location)
        node->location = SourceLocationRange { tryLoc.begin, lastHandler.body->location->end };
    else
        node->location = tryLoc;
    return node;
}

std::unique_ptr<ast::LambdaExpr> Parser::parseLambda()
{
    TRACE_SCOPE("parseLambda");
    auto const funLoc = _lexer.currentRange();
    _lexer.nextToken(); // consume 'fun'

    // Parse parameters until we see '->'
    // Parameters can be bare identifiers or annotated: (x: int)
    std::vector<ast::TypedParameter> parameters;
    while (_lexer.currentToken() != Token::Arrow)
    {
        auto param = parseTypedParameter();
        if (!param)
            break;
        parameters.push_back(std::move(*param));
    }

    if (parameters.empty())
    {
        _report.syntaxErrorWithSuggestions(currentLocation(),
                                           { "Provide at least one parameter for the lambda" },
                                           currentContextSnippet(),
                                           "Expected parameter after 'fun', got '{}'",
                                           _lexer.currentTokenText());
        return nullptr;
    }

    // Parse optional return type annotation: fun (x: int) : int -> body
    std::optional<TypePtr> returnType;
    if (_lexer.currentToken() == Token::Colon)
    {
        _lexer.nextToken(); // consume ':'
        returnType = parseBaseType();
        if (!returnType)
            return nullptr;
    }

    // Expect '->'
    if (_lexer.currentToken() != Token::Arrow)
    {
        _report.syntaxErrorWithSuggestions(currentLocation(),
                                           { "Use '->' to separate parameters from body" },
                                           currentContextSnippet(),
                                           "Expected '->' in lambda expression, got '{}'",
                                           _lexer.currentTokenText());
        return nullptr;
    }
    _lexer.nextToken(); // consume '->'
    consumeNewlines();

    // Parse the lambda body expression
    auto body = parseFSharpExpr();
    if (!body)
        return nullptr;

    auto const endLoc = body->location;
    auto node =
        std::make_unique<ast::LambdaExpr>(std::move(parameters), std::move(body), std::move(returnType));
    node->location = endLoc ? SourceLocationRange { funLoc.begin, endLoc->end } : funLoc;
    return node;
}

std::unique_ptr<ast::Expr> Parser::parseShellCommandExpr()
{
    TRACE_SCOPE("parseShellCommandExpr");

    // Shell command expression: & git status
    // Parses a shell command in F# expression context.
    // The output is captured as a string.

    _lexer.nextToken(); // consume '&'

    // Leave F# mode to parse the shell command with shell tokenization rules
    _lexer.leaveFSharpExpr();

    // Parse the shell command as a logical expression (same as command substitution)
    // This handles pipes, &&, ||, etc.
    auto command = parseLogicalExpr();

    // Re-enter F# mode
    _lexer.enterFSharpExpr();

    if (!command)
    {
        _report.syntaxErrorWithSuggestions(currentLocation(),
                                           { "Add a command after '&'" },
                                           currentContextSnippet(),
                                           "Expected shell command after '&'");
        return nullptr;
    }

    return std::make_unique<ast::ShellCommandExpr>(std::move(command));
}

std::unique_ptr<ast::Expr> Parser::parseExecPipeline()
{
    TRACE_SCOPE("parseExecPipeline");

    std::vector<ast::ExecPipelineExpr::Command> commands;

    // Parse the first command (we're already positioned at "exec")
    for (;;)
    {
        _lexer.nextToken(); // consume "exec"

        // Parse program expression (F# primary)
        auto program = parseFSharpPrimary();
        if (!program)
        {
            _report.syntaxErrorWithSuggestions(currentLocation(),
                                               { "Add a program path after 'exec'" },
                                               currentContextSnippet(),
                                               "Expected program expression after 'exec'");
            return nullptr;
        }

        // Parse argument expressions (while we have F# primaries)
        std::vector<std::unique_ptr<ast::Expr>> arguments;
        while (isFSharpPrimary())
        {
            auto arg = parseFSharpPrimary();
            if (!arg)
                break;
            arguments.push_back(std::move(arg));
        }

        commands.push_back(ast::ExecPipelineExpr::Command { std::move(program), std::move(arguments) });

        // Check for | followed by "exec" for piped exec commands.
        // Two-token lookahead: save state, consume |, check next token.
        if (_lexer.currentToken() != Token::Pipe)
            break;

        // Save current state to restore if | is not followed by "exec"
        auto const pipeTok = _lexer.currentToken();
        auto const pipeLit = std::string(_lexer.currentLiteral());
        auto const pipeRange = _lexer.currentRange();
        _lexer.nextToken(); // consume |

        if (_lexer.currentToken() == Token::Identifier && _lexer.currentLiteral() == "exec")
        {
            // This is a piped exec — continue the loop (will consume "exec" at top)
            continue;
        }

        // Not "exec" after | — push back both tokens (current → deferred, | → current)
        _lexer.pushBackToken(pipeTok, pipeLit, pipeRange);
        break;
    }

    return std::make_unique<ast::ExecPipelineExpr>(std::move(commands));
}

std::unique_ptr<ast::Statement> Parser::parseTypeDefinition()
{
    TRACE_SCOPE("parseTypeDefinition");

    _lexer.nextToken(); // consume 'type'

    if (_lexer.currentToken() != Token::Identifier)
    {
        _report.syntaxErrorWithSuggestions(currentLocation(),
                                           { "Provide a type name" },
                                           currentContextSnippet(),
                                           "Expected type name after 'type', got '{}'",
                                           _lexer.currentTokenText());
        return nullptr;
    }
    auto typeName = _lexer.currentLiteral();
    _lexer.nextToken(); // consume type name

    if (_lexer.currentToken() != Token::Equal)
    {
        _report.syntaxErrorWithSuggestions(currentLocation(),
                                           { "Add '=' after type name" },
                                           currentContextSnippet(),
                                           "Expected '=' after type name '{}', got '{}'",
                                           typeName,
                                           _lexer.currentTokenText());
        return nullptr;
    }
    _lexer.enterFSharpExpr();
    _lexer.nextToken(); // consume '='
    consumeNewlines();

    // Discriminated union path: starts with '|'
    if (_lexer.currentToken() == Token::Pipe)
    {
        std::vector<ast::UnionVariantDef> variants;

        while (_lexer.currentToken() == Token::Pipe)
        {
            _lexer.nextToken(); // consume '|'

            if (_lexer.currentToken() != Token::Identifier)
            {
                _lexer.leaveFSharpExpr();
                _report.syntaxErrorWithSuggestions(currentLocation(),
                                                   { "Provide a constructor name" },
                                                   currentContextSnippet(),
                                                   "Expected constructor name after '|', got '{}'",
                                                   _lexer.currentTokenText());
                return nullptr;
            }
            auto ctorName = _lexer.currentLiteral();
            _lexer.nextToken(); // consume constructor name

            std::vector<TypePtr> payloadTypes;

            // Check for 'of' keyword (payload types)
            std::vector<std::string> fieldNames;
            if (_lexer.currentToken() == Token::Of)
            {
                _lexer.nextToken(); // consume 'of'

                // Parse payload types separated by '*', optionally with field names
                // Syntax: "of name: type * name: type" or "of type * type" (no mixing within a variant)
                auto parseOneField = [&]() -> bool {
                    std::string fieldName;
                    // Lookahead: if we see Identifier followed by Colon, it's a named field
                    if (_lexer.currentToken() == Token::Identifier)
                    {
                        auto savedLit = _lexer.currentLiteral();
                        auto savedRange = _lexer.currentRange();
                        _lexer.nextToken();
                        if (_lexer.currentToken() == Token::Colon)
                        {
                            // Named field: consume the colon
                            fieldName = std::move(savedLit);
                            _lexer.nextToken();
                        }
                        else
                        {
                            // Not a named field — push back the identifier
                            _lexer.pushBackToken(Token::Identifier, std::move(savedLit), savedRange);
                        }
                    }
                    auto payloadType = parseBaseType();
                    if (!payloadType)
                        return false;
                    payloadTypes.push_back(std::move(payloadType));
                    fieldNames.push_back(std::move(fieldName));
                    return true;
                };

                if (!parseOneField())
                {
                    _lexer.leaveFSharpExpr();
                    return nullptr;
                }

                while (_lexer.currentToken() == Token::Star)
                {
                    _lexer.nextToken(); // consume '*'
                    if (!parseOneField())
                    {
                        _lexer.leaveFSharpExpr();
                        return nullptr;
                    }
                }

                // Validate: all fields must be either all named or all unnamed within a variant
                auto const hasNamed =
                    std::ranges::any_of(fieldNames, [](auto const& n) { return !n.empty(); });
                auto const hasUnnamed =
                    std::ranges::any_of(fieldNames, [](auto const& n) { return n.empty(); });
                if (hasNamed && hasUnnamed)
                {
                    _lexer.leaveFSharpExpr();
                    _report.syntaxErrorWithSuggestions(currentLocation(),
                                                       { "Either name all fields or leave all unnamed" },
                                                       currentContextSnippet(),
                                                       "Cannot mix named and unnamed fields in variant '{}'",
                                                       ctorName);
                    return nullptr;
                }
            }

            // Register constructor for later lookup during expression/pattern parsing
            _constructorLookup[ctorName] = { typeName, variants.size() };
            _constructorPayloadSlots[ctorName] = static_cast<uint8_t>(payloadTypes.size());

            variants.push_back(
                ast::UnionVariantDef { std::move(ctorName), std::move(payloadTypes), std::move(fieldNames) });

            // Consume newlines between variants
            consumeNewlines();
        }

        // Validate field name uniqueness across the entire union type
        std::unordered_set<std::string> allFieldNames;
        for (auto const& variant: variants)
        {
            for (auto const& fn: variant.fieldNames)
            {
                if (fn.empty())
                    continue;
                if (!allFieldNames.insert(fn).second)
                {
                    _lexer.leaveFSharpExpr();
                    _report.syntaxErrorWithSuggestions(currentLocation(),
                                                       { "Use a different field name" },
                                                       currentContextSnippet(),
                                                       "Duplicate field name '{}' in union type '{}'",
                                                       fn,
                                                       typeName);
                    return nullptr;
                }
            }
        }

        _lexer.leaveFSharpExpr();
        return std::make_unique<ast::UnionTypeDefStmt>(std::move(typeName), std::move(variants));
    }

    // Record type path: starts with '{'
    if (_lexer.currentToken() != Token::BraceOpen)
    {
        _lexer.leaveFSharpExpr();
        _report.syntaxErrorWithSuggestions(currentLocation(),
                                           { "Add '{' for record or '|' for union" },
                                           currentContextSnippet(),
                                           "Expected '{{' or '|' after '=', got '{}'",
                                           _lexer.currentTokenText());
        return nullptr;
    }
    _lexer.nextToken(); // consume '{'
    consumeNewlines();

    std::vector<ast::RecordFieldDef> fields;

    while (_lexer.currentToken() != Token::BraceClose && _lexer.currentToken() != Token::EndOfInput)
    {
        if (_lexer.currentToken() != Token::Identifier)
        {
            _lexer.leaveFSharpExpr();
            _report.syntaxErrorWithSuggestions(currentLocation(),
                                               { "Provide a field name" },
                                               currentContextSnippet(),
                                               "Expected field name, got '{}'",
                                               _lexer.currentTokenText());
            return nullptr;
        }
        auto fieldName = _lexer.currentLiteral();
        _lexer.nextToken(); // consume field name

        if (_lexer.currentToken() != Token::Colon)
        {
            _lexer.leaveFSharpExpr();
            _report.syntaxErrorWithSuggestions(currentLocation(),
                                               { "Add ':' and type after field name" },
                                               currentContextSnippet(),
                                               "Expected ':' after field name '{}', got '{}'",
                                               fieldName,
                                               _lexer.currentTokenText());
            return nullptr;
        }
        _lexer.nextToken(); // consume ':'

        auto fieldType = parseType();
        if (!fieldType)
        {
            _lexer.leaveFSharpExpr();
            return nullptr;
        }

        fields.push_back(ast::RecordFieldDef { std::move(fieldName), std::move(fieldType) });

        // Consume separator: semicolon or newline
        if (_lexer.currentToken() == Token::Semicolon)
            _lexer.nextToken();
        consumeNewlines();
    }

    if (_lexer.currentToken() != Token::BraceClose)
    {
        _lexer.leaveFSharpExpr();
        _report.syntaxErrorWithSuggestions(currentLocation(),
                                           { "Add a closing '}'" },
                                           currentContextSnippet(),
                                           "Expected '}}' at end of record type definition, got '{}'",
                                           _lexer.currentTokenText());
        return nullptr;
    }
    _lexer.nextToken(); // consume '}'
    _lexer.leaveFSharpExpr();

    // Register the record type for later use in record literal resolution
    std::vector<std::string> fieldNames;
    fieldNames.reserve(fields.size());
    for (auto const& f: fields)
        fieldNames.push_back(f.name);
    _knownRecordTypes[typeName] = std::move(fieldNames);

    return std::make_unique<ast::RecordTypeDefStmt>(std::move(typeName), std::move(fields));
}

std::vector<ast::DataSourceFieldDef> Parser::parseDataSourceFieldDefs()
{
    TRACE_SCOPE("parseDataSourceFieldDefs");
    std::vector<ast::DataSourceFieldDef> fields;

    while (_lexer.currentToken() != Token::BraceClose && _lexer.currentToken() != Token::EndOfInput)
    {
        if (_lexer.currentToken() != Token::Identifier)
        {
            _report.syntaxErrorWithSuggestions(currentLocation(),
                                               { "Provide a field name" },
                                               currentContextSnippet(),
                                               "Expected field name in data source type annotation, got '{}'",
                                               _lexer.currentTokenText());
            return {};
        }
        auto fieldName = _lexer.currentLiteral();
        _lexer.nextToken(); // consume field name

        if (_lexer.currentToken() != Token::Colon)
        {
            _report.syntaxErrorWithSuggestions(currentLocation(),
                                               { "Add ':' and type after field name" },
                                               currentContextSnippet(),
                                               "Expected ':' after field name '{}', got '{}'",
                                               fieldName,
                                               _lexer.currentTokenText());
            return {};
        }
        _lexer.nextToken(); // consume ':'

        auto fieldType = parseType();
        if (!fieldType)
            return {};

        // Check for optional default value: = expr
        std::unique_ptr<ast::Expr> defaultValue;
        if (_lexer.currentToken() == Token::Equal)
        {
            _lexer.nextToken(); // consume '='
            defaultValue = parseFSharpExpr();
            if (!defaultValue)
                return {};
        }

        fields.push_back(
            ast::DataSourceFieldDef { std::move(fieldName), std::move(fieldType), std::move(defaultValue) });

        // Consume separator: semicolon or newline
        if (_lexer.currentToken() == Token::Semicolon)
            _lexer.nextToken();
        consumeNewlines();
    }

    return fields;
}

std::unique_ptr<ast::Expr> Parser::tryParseDataSource(std::unique_ptr<ast::Statement> stmt)
{
    TRACE_SCOPE("tryParseDataSource");

    // Must be at 'as' keyword
    if (_lexer.currentToken() != Token::As)
        return nullptr;

    // Determine the data source kind and extract components from the parsed statement
    ast::DataSourceExpr::Kind kind {};
    std::unique_ptr<ast::Expr> filePath;
    std::unique_ptr<ast::Statement> pipeSource;
    std::optional<SourceLocationRange> cmdLoc; // Source location for formatter comment ordering

    auto const isDataSourceName = [](std::string_view name) {
        return name == "open-json" || name == "open-csv" || name == "from-json" || name == "from-csv";
    };

    auto const nameToKind = [](std::string_view name) -> ast::DataSourceExpr::Kind {
        if (name == "open-json")
            return ast::DataSourceExpr::Kind::OpenJson;
        if (name == "open-csv")
            return ast::DataSourceExpr::Kind::OpenCsv;
        if (name == "from-json")
            return ast::DataSourceExpr::Kind::FromJson;
        return ast::DataSourceExpr::Kind::FromCsv;
    };

    if (auto* call = dynamic_cast<ast::ProgramCall*>(stmt.get()))
    {
        if (!isDataSourceName(call->program))
            return nullptr;

        kind = nameToKind(call->program);
        cmdLoc = call->programLocation;

        // For open-*: extract file path from first argument
        if (kind == ast::DataSourceExpr::Kind::OpenJson || kind == ast::DataSourceExpr::Kind::OpenCsv)
        {
            if (!call->parameters.empty())
                filePath = std::move(call->parameters[0]);
        }
    }
    else if (auto* pipeline = dynamic_cast<ast::CallPipeline*>(stmt.get()))
    {
        // Check if last call in pipeline is from-json or from-csv
        if (pipeline->calls.empty())
            return nullptr;

        auto& lastCall = pipeline->calls.back();
        if (!isDataSourceName(lastCall->program))
            return nullptr;

        kind = nameToKind(lastCall->program);
        cmdLoc = pipeline->calls.front()->programLocation;

        // Build pipe source from all calls except the last
        if (pipeline->calls.size() == 1)
        {
            // Standalone from-json/from-csv (reads from stdin)
            pipeSource = nullptr;
        }
        else
        {
            // Extract all calls except the last as the pipe source
            std::vector<std::unique_ptr<ast::ProgramCall>> sourceCalls;
            for (size_t i = 0; i + 1 < pipeline->calls.size(); ++i)
                sourceCalls.push_back(std::move(pipeline->calls[i]));

            if (sourceCalls.size() == 1)
            {
                pipeSource = std::move(sourceCalls[0]);
            }
            else
            {
                pipeSource = std::make_unique<ast::CallPipeline>(std::move(sourceCalls));
            }
        }
    }
    else
    {
        return nullptr;
    }

    // Consume 'as' keyword
    _lexer.enterFSharpExpr();
    _lexer.nextToken(); // consume 'as'
    consumeNewlines();

    // Parse type specification
    auto result = std::make_unique<ast::DataSourceExpr>();
    result->location = cmdLoc;
    result->kind = kind;
    result->filePath = std::move(filePath);
    result->pipeSource = std::move(pipeSource);

    if (_lexer.currentToken() == Token::BraceOpen)
    {
        // Inline record type: { name: string; age: int }
        _lexer.nextToken(); // consume '{'
        consumeNewlines();

        result->inlineFields = parseDataSourceFieldDefs();
        if (result->inlineFields.empty() && _lexer.currentToken() != Token::BraceClose)
        {
            _lexer.leaveFSharpExpr();
            return nullptr; // error already reported
        }

        if (_lexer.currentToken() != Token::BraceClose)
        {
            _lexer.leaveFSharpExpr();
            _report.syntaxErrorWithSuggestions(currentLocation(),
                                               { "Add a closing '}'" },
                                               currentContextSnippet(),
                                               "Expected '}}' at end of type annotation, got '{}'",
                                               _lexer.currentTokenText());
            return nullptr;
        }
        _lexer.nextToken(); // consume '}'
    }
    else if (_lexer.currentToken() == Token::Identifier)
    {
        // Named type reference: Person, UserRecord, etc.
        result->typeName = _lexer.currentLiteral();
        _lexer.nextToken(); // consume type name
    }
    else
    {
        _lexer.leaveFSharpExpr();
        _report.syntaxErrorWithSuggestions(currentLocation(),
                                           { "Provide a type: '{ ... }' or a type name" },
                                           currentContextSnippet(),
                                           "Expected type annotation after 'as', got '{}'",
                                           _lexer.currentTokenText());
        return nullptr;
    }

    _lexer.leaveFSharpExpr();
    return result;
}

std::unique_ptr<ast::Expr> Parser::parseBlockExprOrRecord()
{
    TRACE_SCOPE("parseBlockExprOrRecord");

    // We're at '{'. We need to disambiguate:
    // - Record literal: { name = "Alice"; age = 30 }
    // - Record update:  { expr with field = val; ... }
    // - Block expression: { let x = 1; x + 2 } or { expr; expr }

    // Save current position for backtracking
    auto savedToken = _lexer.currentToken();
    auto savedLiteral = _lexer.currentLiteral();
    auto savedRange = _lexer.currentRange();

    _lexer.nextToken(); // consume '{'
    consumeNewlines();

    // If we see 'let', it's definitely a block expression
    if (_lexer.currentToken() == Token::Let)
    {
        // Push '{' back isn't needed since parseBlockExpr consumed it
        // But parseBlockExpr expects '{' already consumed, which we did
        // Continue directly with block expression parsing logic
        std::vector<std::unique_ptr<ast::Statement>> statements;

        while (_lexer.currentToken() != Token::BraceClose && _lexer.currentToken() != Token::EndOfInput)
        {
            if (_lexer.currentToken() == Token::Let)
            {
                auto let = parseLet();
                if (!let)
                    return nullptr;
                statements.push_back(std::move(let));
                consumeNewlines();
                continue;
            }

            auto expr = parseFSharpExpr();
            if (!expr)
                return nullptr;

            consumeNewlines();
            if (_lexer.currentToken() == Token::BraceClose)
            {
                _lexer.nextToken(); // consume '}'
                return std::make_unique<ast::BlockExpr>(std::move(statements), std::move(expr), true);
            }

            statements.push_back(std::make_unique<ast::ExprStmt>(std::move(expr)));
            consumeNewlines();
        }

        if (_lexer.currentToken() != Token::BraceClose)
        {
            _report.syntaxErrorWithSuggestions(currentLocation(),
                                               { "Add a closing '}'" },
                                               currentContextSnippet(),
                                               "Expected '}}' at end of block expression, got '{}'",
                                               _lexer.currentTokenText());
            return nullptr;
        }
        _lexer.nextToken(); // consume '}'
        return std::make_unique<ast::BlockExpr>(
            std::move(statements), std::make_unique<ast::UnitExpr>(), true);
    }

    // Check for record literal: Identifier followed by '='
    if (_lexer.currentToken() == Token::Identifier)
    {
        auto peekIdent = _lexer.currentLiteral();
        auto peekRange = _lexer.currentRange();
        _lexer.nextToken(); // consume identifier

        if (_lexer.currentToken() == Token::Equal)
        {
            // This is a record literal: { name = expr; ... }
            _lexer.nextToken(); // consume '='
            auto firstValue = parseFSharpExpr();
            if (!firstValue)
                return nullptr;

            std::vector<ast::RecordFieldInit> fields;
            fields.push_back(ast::RecordFieldInit { std::move(peekIdent), std::move(firstValue) });

            // Consume separator
            if (_lexer.currentToken() == Token::Semicolon)
                _lexer.nextToken();
            consumeNewlines();

            // Parse remaining fields
            while (_lexer.currentToken() != Token::BraceClose && _lexer.currentToken() != Token::EndOfInput)
            {
                if (_lexer.currentToken() != Token::Identifier)
                {
                    _report.syntaxErrorWithSuggestions(currentLocation(),
                                                       { "Provide a field name" },
                                                       currentContextSnippet(),
                                                       "Expected field name in record literal, got '{}'",
                                                       _lexer.currentTokenText());
                    return nullptr;
                }
                auto fieldName = _lexer.currentLiteral();
                _lexer.nextToken(); // consume field name

                if (_lexer.currentToken() != Token::Equal)
                {
                    _report.syntaxErrorWithSuggestions(
                        currentLocation(),
                        { "Add '=' after field name" },
                        currentContextSnippet(),
                        "Expected '=' after field name '{}' in record literal, got '{}'",
                        fieldName,
                        _lexer.currentTokenText());
                    return nullptr;
                }
                _lexer.nextToken(); // consume '='

                auto fieldValue = parseFSharpExpr();
                if (!fieldValue)
                    return nullptr;

                fields.push_back(ast::RecordFieldInit { std::move(fieldName), std::move(fieldValue) });

                if (_lexer.currentToken() == Token::Semicolon)
                    _lexer.nextToken();
                consumeNewlines();
            }

            if (_lexer.currentToken() != Token::BraceClose)
            {
                _report.syntaxErrorWithSuggestions(currentLocation(),
                                                   { "Add a closing '}'" },
                                                   currentContextSnippet(),
                                                   "Expected '}}' at end of record literal, got '{}'",
                                                   _lexer.currentTokenText());
                return nullptr;
            }
            _lexer.nextToken(); // consume '}'

            // Resolve the record type name by matching field names
            std::string typeName;
            std::vector<std::string> fieldNames;
            for (auto const& f: fields)
                fieldNames.push_back(f.name);

            for (auto const& [name, registeredFields]: _knownRecordTypes)
            {
                if (registeredFields == fieldNames)
                {
                    typeName = name;
                    break;
                }
            }

            return std::make_unique<ast::RecordExpr>(std::move(typeName), std::move(fields));
        }

        if (_lexer.currentToken() == Token::With)
        {
            // Record update: { identifier with field = val; ... }
            // The identifier is a variable name referencing a record
            auto baseExpr = std::make_unique<ast::IdentifierExpr>(std::move(peekIdent));

            _lexer.nextToken(); // consume 'with'
            consumeNewlines();

            std::vector<ast::RecordFieldInit> updates;

            while (_lexer.currentToken() != Token::BraceClose && _lexer.currentToken() != Token::EndOfInput)
            {
                if (_lexer.currentToken() != Token::Identifier)
                {
                    _report.syntaxErrorWithSuggestions(currentLocation(),
                                                       { "Provide a field name" },
                                                       currentContextSnippet(),
                                                       "Expected field name in record update, got '{}'",
                                                       _lexer.currentTokenText());
                    return nullptr;
                }
                auto fieldName = _lexer.currentLiteral();
                _lexer.nextToken(); // consume field name

                if (_lexer.currentToken() != Token::Equal)
                {
                    _report.syntaxErrorWithSuggestions(
                        currentLocation(),
                        { "Add '=' after field name" },
                        currentContextSnippet(),
                        "Expected '=' after field name '{}' in record update, got '{}'",
                        fieldName,
                        _lexer.currentTokenText());
                    return nullptr;
                }
                _lexer.nextToken(); // consume '='

                auto fieldValue = parseFSharpExpr();
                if (!fieldValue)
                    return nullptr;

                updates.push_back(ast::RecordFieldInit { std::move(fieldName), std::move(fieldValue) });

                if (_lexer.currentToken() == Token::Semicolon)
                    _lexer.nextToken();
                consumeNewlines();
            }

            if (_lexer.currentToken() != Token::BraceClose)
            {
                _report.syntaxErrorWithSuggestions(currentLocation(),
                                                   { "Add a closing '}'" },
                                                   currentContextSnippet(),
                                                   "Expected '}}' at end of record update, got '{}'",
                                                   _lexer.currentTokenText());
                return nullptr;
            }
            _lexer.nextToken(); // consume '}'

            return std::make_unique<ast::RecordUpdateExpr>(std::move(baseExpr), std::move(updates));
        }

        // Neither '=' nor 'with' after identifier — this is a block expression.
        // Push back the consumed identifier and fall through to block parsing.
        _lexer.pushBackToken(Token::Identifier, std::move(peekIdent), peekRange);
    }

    // Fall through to block expression parsing.
    // The '{' was already consumed, so we do inline block parsing.
    std::vector<std::unique_ptr<ast::Statement>> statements;

    while (_lexer.currentToken() != Token::BraceClose && _lexer.currentToken() != Token::EndOfInput)
    {
        if (_lexer.currentToken() == Token::Let)
        {
            auto let = parseLet();
            if (!let)
                return nullptr;
            statements.push_back(std::move(let));
            consumeNewlines();
            continue;
        }

        auto expr = parseFSharpExpr();
        if (!expr)
            return nullptr;

        consumeNewlines();
        if (_lexer.currentToken() == Token::BraceClose)
        {
            _lexer.nextToken(); // consume '}'
            return std::make_unique<ast::BlockExpr>(std::move(statements), std::move(expr), true);
        }

        statements.push_back(std::make_unique<ast::ExprStmt>(std::move(expr)));
        consumeNewlines();
    }

    if (_lexer.currentToken() != Token::BraceClose)
    {
        _report.syntaxErrorWithSuggestions(currentLocation(),
                                           { "Add a closing '}'" },
                                           currentContextSnippet(),
                                           "Expected '}}' at end of block expression, got '{}'",
                                           _lexer.currentTokenText());
        return nullptr;
    }
    _lexer.nextToken(); // consume '}'
    return std::make_unique<ast::BlockExpr>(std::move(statements), std::make_unique<ast::UnitExpr>(), true);
}

std::unique_ptr<ast::Expr> Parser::parseBlockExpr()
{
    TRACE_SCOPE("parseBlockExpr");

    _lexer.nextToken(); // consume '{'
    consumeNewlines();

    std::vector<std::unique_ptr<ast::Statement>> statements;

    // Parse statements until we find the closing brace or an expression that
    // isn't followed by a semicolon/newline + more statements
    while (_lexer.currentToken() != Token::BraceClose && _lexer.currentToken() != Token::EndOfInput)
    {
        // Try parsing as a let binding first
        if (_lexer.currentToken() == Token::Let)
        {
            auto let = parseLet();
            if (!let)
                return nullptr;
            statements.push_back(std::move(let));
            consumeNewlines();
            continue;
        }

        // Parse as an expression
        auto expr = parseFSharpExpr();
        if (!expr)
            return nullptr;

        // Check if this is the last item in the block (followed by '}')
        consumeNewlines();
        if (_lexer.currentToken() == Token::BraceClose)
        {
            // This is the result expression
            _lexer.nextToken(); // consume '}'
            return std::make_unique<ast::BlockExpr>(std::move(statements), std::move(expr), true);
        }

        // Otherwise, treat as a statement and continue
        statements.push_back(std::make_unique<ast::ExprStmt>(std::move(expr)));
        consumeNewlines();
    }

    if (_lexer.currentToken() != Token::BraceClose)
    {
        _report.syntaxErrorWithSuggestions(currentLocation(),
                                           { "Add a closing '}'" },
                                           currentContextSnippet(),
                                           "Expected '}}' at end of block expression, got '{}'",
                                           _lexer.currentTokenText());
        return nullptr;
    }

    _lexer.nextToken(); // consume '}'

    // Empty block or block with only statements — result is unit (0)
    return std::make_unique<ast::BlockExpr>(std::move(statements), std::make_unique<ast::UnitExpr>(), true);
}

std::unique_ptr<ast::Expr> Parser::parseFSharpExprSequence(size_t referenceColumn,
                                                           std::optional<std::string_view> terminatorKeyword)
{
    TRACE_SCOPE("parseFSharpExprSequence");

    std::vector<std::unique_ptr<ast::Statement>> statements;
    std::unique_ptr<ast::Expr> lastExpr;

    // Parse the first item
    if (_lexer.currentToken() == Token::Let)
    {
        auto let = parseLet();
        if (!let)
            return nullptr;
        // Check if this is a let-in expression (let x = ... in body) inside a sequence
        if (_lexer.currentToken() == Token::Identifier && _lexer.currentLiteral() == "in")
        {
            _lexer.nextToken(); // consume 'in'
            consumeNewlines();
            auto body = parseFSharpExpr();
            if (!body)
                return nullptr;
            if (let->destructurePattern)
                lastExpr = std::make_unique<ast::LetInExpr>(
                    std::move(let->destructurePattern), std::move(let->value), std::move(body));
            else
                lastExpr = std::make_unique<ast::LetInExpr>(let->isRecursive,
                                                            std::move(let->name),
                                                            std::move(let->parameters),
                                                            std::move(let->returnType),
                                                            std::move(let->value),
                                                            std::move(body));
        }
        else
        {
            statements.push_back(std::move(let));
        }
    }
    else
    {
        lastExpr = parseFSharpExpr();
        if (!lastExpr)
            return nullptr;
    }

    // Try to parse more items, terminated by indentation or keyword.
    // Only continue across LineFeed tokens (true line breaks), NOT semicolons.
    // Semicolons are hard statement separators that terminate the sequence.
    for (;;)
    {
        // Semicolons terminate the expression sequence (hard statement boundary)
        if (_lexer.currentToken() == Token::Semicolon)
            break;

        // Only consume LineFeed tokens (true line breaks)
        bool sawLineFeed = false;
        while (_lexer.currentToken() == Token::LineFeed)
        {
            _lexer.nextToken();
            sawLineFeed = true;
        }

        // Check for terminator keyword (e.g., "else") or "elif" (sugar for "else if")
        if (_lexer.currentToken() == Token::Identifier
            && ((terminatorKeyword.has_value() && _lexer.currentLiteral() == terminatorKeyword.value())
                || _lexer.currentLiteral() == "elif"))
        {
            if (sawLineFeed)
                _lexer.pushBackToken(Token::LineFeed, "\n");
            break;
        }

        // Check column: if current token is at or before reference column, stop
        if (sawLineFeed && currentTokenColumn() <= referenceColumn)
        {
            _lexer.pushBackToken(Token::LineFeed, "\n");
            break;
        }

        // Check if current token can start a new expression or let binding
        if (_lexer.currentToken() != Token::Let && !isFSharpPrimary())
        {
            if (sawLineFeed)
                _lexer.pushBackToken(Token::LineFeed, "\n");
            break;
        }

        // Must have a line feed to continue with a new item
        if (!sawLineFeed)
            break;

        // Demote previous expression to statement
        if (lastExpr)
        {
            statements.push_back(std::make_unique<ast::ExprStmt>(std::move(lastExpr)));
            lastExpr = nullptr;
        }

        // Parse next item
        if (_lexer.currentToken() == Token::Let)
        {
            auto let = parseLet();
            if (!let)
                return nullptr;
            // Check if this is a let-in expression (let x = ... in body) inside a sequence
            if (_lexer.currentToken() == Token::Identifier && _lexer.currentLiteral() == "in")
            {
                _lexer.nextToken(); // consume 'in'
                consumeNewlines();
                auto body = parseFSharpExpr();
                if (!body)
                    return nullptr;
                if (let->destructurePattern)
                    lastExpr = std::make_unique<ast::LetInExpr>(
                        std::move(let->destructurePattern), std::move(let->value), std::move(body));
                else
                    lastExpr = std::make_unique<ast::LetInExpr>(let->isRecursive,
                                                                std::move(let->name),
                                                                std::move(let->parameters),
                                                                std::move(let->returnType),
                                                                std::move(let->value),
                                                                std::move(body));
            }
            else
            {
                statements.push_back(std::move(let));
            }
        }
        else
        {
            lastExpr = parseFSharpExpr();
            if (!lastExpr)
                return nullptr;
        }
    }

    // If only one expression and no statements, return it directly (backward compat)
    if (statements.empty() && lastExpr)
        return lastExpr;

    // If we have statements but no trailing expression, use unit as the result
    if (!lastExpr)
        lastExpr = std::make_unique<ast::UnitExpr>();

    return std::make_unique<ast::BlockExpr>(std::move(statements), std::move(lastExpr));
}

std::unique_ptr<ast::Expr> Parser::parseFSharpPrimary()
{
    TRACE_SCOPE("parseFSharpPrimary");

    switch (_lexer.currentToken())
    {
        case Token::Let: {
            // Let-in expression: let x = 5 in x + 10
            return parseLetInExpr();
        }

        case Token::Fun: {
            // Lambda expression: fun x -> expr
            return parseLambda();
        }

        case Token::Match: {
            // Match expression: match x with | pattern -> expr
            return parseMatch();
        }

        case Token::Number: {
            // Parse as integer or float
            auto const lit = std::string(_lexer.currentLiteral());
            auto const loc = _lexer.currentRange();

            // Detect base prefix (skip optional leading '-')
            auto sv = std::string_view(lit);
            auto const negative = sv.starts_with("-");
            auto const digits = negative ? sv.substr(1) : sv;
            auto const hasBasePrefix = digits.starts_with("0x") || digits.starts_with("0X")
                                       || digits.starts_with("0o") || digits.starts_with("0O")
                                       || digits.starts_with("0b") || digits.starts_with("0B");

            // Float detection: only for non-base-prefixed literals
            // (hex like 0xfe contains 'e' but is NOT a float)
            if (!hasBasePrefix
                && (lit.find('.') != std::string::npos || lit.find('e') != std::string::npos
                    || lit.find('E') != std::string::npos))
            {
                auto originalText = std::string(lit);
                auto const value = std::stod(lit);
                _lexer.nextToken();
                auto node = std::make_unique<ast::FloatLiteralExpr>(value, std::move(originalText));
                node->location = loc;
                return node;
            }

            // Integer parsing with base detection
            int64_t value = 0;
            if (hasBasePrefix)
            {
                auto const baseDigits = digits.substr(2); // skip "0x"/"0o"/"0b"
                auto const base = (digits[1] == 'x' || digits[1] == 'X')   ? 16
                                  : (digits[1] == 'o' || digits[1] == 'O') ? 8
                                                                           : 2;
                auto [ptr, ec] =
                    std::from_chars(baseDigits.data(), baseDigits.data() + baseDigits.size(), value, base);
                if (ec != std::errc())
                    value = 0;
                if (negative)
                    value = -value;
            }
            else
            {
                auto [ptr, ec] = std::from_chars(lit.data(), lit.data() + lit.size(), value);
                if (ec != std::errc())
                    value = 0;
            }
            _lexer.nextToken();

            // Check for size literal suffix: 1_B, 1_KB, 1_MB, 1_GB, 1_TB
            if (_lexer.currentToken() == Token::Identifier)
            {
                auto const& suffix = _lexer.currentLiteral();
                int64_t multiplier = 0;
                if (suffix == "_B")
                    multiplier = 1;
                else if (suffix == "_KB")
                    multiplier = 1024;
                else if (suffix == "_MB")
                    multiplier = int64_t { 1024 } * 1024;
                else if (suffix == "_GB")
                    multiplier = int64_t { 1024 } * 1024 * 1024;
                else if (suffix == "_TB")
                    multiplier = int64_t { 1024 } * 1024 * 1024 * 1024;
                if (multiplier > 0)
                {
                    _lexer.nextToken();
                    auto sizeNode = std::make_unique<ast::SizeLiteralExpr>(value * multiplier);
                    sizeNode->location = loc;
                    return sizeNode;
                }
            }

            auto node = std::make_unique<ast::IntLiteralExpr>(
                value, hasBasePrefix ? std::string(lit) : std::string {});
            node->location = loc;
            return node;
        }

        case Token::BracketOpen: {
            // List literal: [1; 2; 3] or [1..10] or [for x in xs -> x * 2]
            return parseListLiteral();
        }

        case Token::True: {
            auto const loc = _lexer.currentRange();
            _lexer.nextToken();
            auto node = std::make_unique<ast::BoolLiteralExpr>(true);
            node->location = loc;
            return node;
        }
        case Token::False: {
            auto const loc = _lexer.currentRange();
            _lexer.nextToken();
            auto node = std::make_unique<ast::BoolLiteralExpr>(false);
            node->location = loc;
            return node;
        }

        case Token::Identifier: {
            auto const& lit = _lexer.currentLiteral();

            // Check for break/continue expressions (used inside loops in F# if-then-else)
            if (lit == "break")
            {
                auto const loc = _lexer.currentRange();
                _lexer.nextToken();
                auto node = std::make_unique<ast::BreakExpr>();
                node->location = loc;
                return node;
            }
            if (lit == "continue")
            {
                auto const loc = _lexer.currentRange();
                _lexer.nextToken();
                auto node = std::make_unique<ast::ContinueExpr>();
                node->location = loc;
                return node;
            }

            // Check for if-then-else expression
            if (lit == "if")
            {
                auto const ifColumn = currentTokenColumn();
                auto const ifLoc = _lexer.currentRange();
                _lexer.nextToken(); // consume 'if'
                consumeNewlines();
                auto condition = parseFSharpExpr();
                if (!condition)
                    return nullptr;

                consumeNewlines();
                if (_lexer.currentToken() != Token::Identifier || _lexer.currentLiteral() != "then")
                {
                    _report.syntaxErrorWithSuggestions(currentLocation(),
                                                       { "Add 'then' after the condition" },
                                                       currentContextSnippet(),
                                                       "Expected 'then' in if-expression, got '{}'",
                                                       _lexer.currentTokenText());
                    return nullptr;
                }
                _lexer.nextToken(); // consume 'then'
                consumeNewlines();

                auto thenExpr = parseFSharpExprSequence(ifColumn, "else");
                if (!thenExpr)
                    return nullptr;

                // Else/elif branch is optional — `if cond then expr` returns unit when false.
                // Use pushback pattern: consume newlines to look for `else`/`elif`, but push back
                // a LineFeed if not found, so the statement boundary is preserved.
                std::unique_ptr<ast::Expr> elseExpr;
                auto const skippedNewlines = consumeNewlines();
                if (_lexer.currentToken() == Token::Identifier && _lexer.currentLiteral() == "else")
                {
                    _lexer.nextToken(); // consume 'else'
                    consumeNewlines();
                    elseExpr = parseFSharpExprSequence(ifColumn);
                    if (!elseExpr)
                        return nullptr;
                }
                else if (_lexer.currentToken() == Token::Identifier && _lexer.currentLiteral() == "elif")
                {
                    // `elif` is sugar for `else if` — rewrite to `if` and parse as nested
                    // if-expression. This naturally handles chained elif via recursion.
                    _lexer.nextToken(); // consume 'elif'
                    _lexer.pushBackToken(Token::Identifier, "if");
                    elseExpr = parseFSharpExpr();
                    if (!elseExpr)
                        return nullptr;
                }
                else if (skippedNewlines)
                {
                    // No `else`/`elif` found — push back the newline to preserve statement boundary
                    _lexer.pushBackToken(Token::LineFeed, "\n");
                }

                auto const endLoc = elseExpr ? elseExpr->location : thenExpr->location;
                auto node = std::make_unique<ast::IfExpr>(
                    std::move(condition), std::move(thenExpr), std::move(elseExpr));
                if (endLoc)
                    node->location = SourceLocationRange { ifLoc.begin, endLoc->end };
                else
                    node->location = ifLoc;
                return node;
            }

            // exec expression: exec prog args | exec prog args
            if (lit == "exec")
                return parseExecPipeline();

            // Check for list literal starting with '[' (in non-F# mode, [ is part of identifier)
            if (!lit.empty() && lit[0] == '[')
            {
                return parseListLiteral();
            }

            // Placeholder lambda sugar: _ → IdentifierExpr("__x")
            if (lit == "_")
            {
                auto const loc = _lexer.currentRange();
                _lexer.nextToken();
                ++_placeholderCount;
                auto node = std::make_unique<ast::IdentifierExpr>("__x");
                node->location = loc;
                return node;
            }

            // User-defined union constructor
            if (auto ctorIt = _constructorLookup.find(lit); ctorIt != _constructorLookup.end())
            {
                auto const& [ctorTypeName, variantIdx] = ctorIt->second;
                auto payloadSlots = _constructorPayloadSlots[lit];
                auto const ctorLoc = _lexer.currentRange();
                auto ctorName = consumeLiteral();

                std::vector<std::unique_ptr<ast::Expr>> args;
                std::optional<SourceLocationRange> lastArgLoc;
                if (payloadSlots > 0 && isFSharpPrimary())
                {
                    auto arg = parseFSharpPrimary();
                    if (!arg)
                        return nullptr;
                    lastArgLoc = arg->location;

                    // For multi-slot constructors, if the argument is a tuple, flatten it
                    if (payloadSlots > 1)
                    {
                        if (auto* tuple = dynamic_cast<ast::TupleExpr*>(arg.get()))
                        {
                            for (auto& elem: tuple->elements)
                                args.push_back(std::move(elem));
                        }
                        else
                        {
                            args.push_back(std::move(arg));
                        }
                    }
                    else
                    {
                        args.push_back(std::move(arg));
                    }
                }

                auto node = std::make_unique<ast::UnionConstructorExpr>(
                    ctorTypeName, std::move(ctorName), std::move(args));
                node->location =
                    lastArgLoc ? SourceLocationRange { ctorLoc.begin, lastArgLoc->end } : ctorLoc;
                return node;
            }

            // Regular identifier
            auto const loc = _lexer.currentRange();
            std::string name = consumeLiteral();
            auto node = std::make_unique<ast::IdentifierExpr>(std::move(name));
            node->location = loc;
            return node;
        }

        case Token::RndOpen: {
            // Unit expression: (), or parenthesized expression, or tuple
            auto const openLoc = _lexer.currentRange();
            _lexer.nextToken(); // consume '('
            if (_lexer.currentToken() == Token::RndClose)
            {
                auto const closeLoc = _lexer.currentRange();
                _lexer.nextToken(); // consume ')'
                auto node = std::make_unique<ast::UnitExpr>();
                node->location = SourceLocationRange { openLoc.begin, closeLoc.end };
                return node;
            }

            // Save and reset placeholder state for this parenthesized scope
            auto const savedPlaceholderCount = _placeholderCount;
            auto const savedPlaceholderScope = _placeholderScopeActive;
            _placeholderCount = 0;
            _placeholderScopeActive = true;

            auto first = parseFSharpExpr();
            if (!first)
                return nullptr;

            // Check for comma → tuple
            if (_lexer.currentToken() == Token::Comma)
            {
                std::vector<std::unique_ptr<ast::Expr>> elements;
                elements.push_back(std::move(first));
                while (_lexer.currentToken() == Token::Comma)
                {
                    _lexer.nextToken(); // consume ','
                    auto elem = parseFSharpExpr();
                    if (!elem)
                        return nullptr;
                    elements.push_back(std::move(elem));
                }
                if (_lexer.currentToken() != Token::RndClose)
                {
                    _report.syntaxErrorWithSuggestions(currentLocation(),
                                                       { "Add a closing ')'" },
                                                       currentContextSnippet(),
                                                       "Expected ')' after tuple, got '{}'",
                                                       _lexer.currentTokenText());
                    return nullptr;
                }
                auto const closeLoc = _lexer.currentRange();
                _lexer.nextToken(); // consume ')'

                auto const localPlaceholders = _placeholderCount;
                _placeholderCount = savedPlaceholderCount;
                _placeholderScopeActive = savedPlaceholderScope;

                auto const spanLoc = SourceLocationRange { openLoc.begin, closeLoc.end };
                if (localPlaceholders > 0)
                {
                    // Wrap tuple in placeholder lambda: (_.a, _.b)
                    auto node = std::make_unique<ast::PlaceholderLambdaExpr>(
                        std::make_unique<ast::TupleExpr>(std::move(elements)), true);
                    node->location = spanLoc;
                    return node;
                }
                auto node = std::make_unique<ast::TupleExpr>(std::move(elements));
                node->location = spanLoc;
                return node;
            }

            if (_lexer.currentToken() != Token::RndClose)
            {
                _report.syntaxErrorWithSuggestions(currentLocation(),
                                                   { "Add a closing ')'" },
                                                   currentContextSnippet(),
                                                   "Expected ')' after expression, got '{}'",
                                                   _lexer.currentTokenText());
                return nullptr;
            }
            auto const closeLoc = _lexer.currentRange();
            _lexer.nextToken(); // consume ')'

            auto const localPlaceholders = _placeholderCount;
            _placeholderCount = savedPlaceholderCount;
            _placeholderScopeActive = savedPlaceholderScope;

            auto const spanLoc = SourceLocationRange { openLoc.begin, closeLoc.end };
            if (localPlaceholders > 0)
            {
                // Wrap expression in placeholder lambda: (_ + 1)
                auto node = std::make_unique<ast::PlaceholderLambdaExpr>(std::move(first), true);
                node->location = spanLoc;
                return node;
            }
            auto node = std::make_unique<ast::ParenExpr>(std::move(first));
            node->location = spanLoc;
            return node;
        }

        case Token::BraceOpen: {
            // Block expression, record literal, or record update
            return parseBlockExprOrRecord();
        }

        case Token::Ampersand: {
            // Shell command expression: & git status
            return parseShellCommandExpr();
        }

        case Token::DollarRndOpen: {
            // Command substitution in F# expression context: $(whoami)
            // We must fully exit F# mode for the shell command inside $(...), then
            // restore F# depth BEFORE consuming ')' so that the token after ')'
            // is lexed in F# mode (e.g., '+' as Token::Plus, not a shell word).
            // Save/restore depth instead of single leave/enter to handle nested F# contexts
            // (e.g., match arm body → parseLet → parseFSharpExpr → $(...)).
            auto const savedFSharpDepth = _lexer.fsharpDepth();
            _lexer.setFSharpDepth(0);
            _lexer.nextToken(); // consume $(, lex next token in shell mode
            auto command = parseLogicalExpr();
            if (!command)
                return nullptr;
            _lexer.setFSharpDepth(savedFSharpDepth); // restore F# mode before consuming ')'
            if (_lexer.currentToken() != Token::RndClose)
            {
                _report.syntaxErrorWithSuggestions(currentLocation(),
                                                   { "Add a closing ')' after the command" },
                                                   currentContextSnippet(),
                                                   "Expected ')' after command substitution, got '{}'",
                                                   _lexer.currentTokenText());
                return nullptr;
            }
            _lexer.nextToken(); // consume ), lex next token in F# mode
            return std::make_unique<ast::SubstitutionExpr>(std::move(command), false);
        }

        case Token::OptionSome: {
            // Some expr - Option constructor with value
            auto const someLoc = _lexer.currentRange();
            _lexer.nextToken(); // consume 'Some'
            auto value = parseFSharpPrimary();
            if (!value)
            {
                _report.syntaxErrorWithSuggestions(currentLocation(),
                                                   { "Provide a value after 'Some'" },
                                                   currentContextSnippet(),
                                                   "Expected expression after 'Some', got '{}'",
                                                   _lexer.currentTokenText());
                return nullptr;
            }
            auto const endLoc = value->location;
            auto node = std::make_unique<ast::OptionExpr>(true, std::move(value));
            node->location = endLoc ? SourceLocationRange { someLoc.begin, endLoc->end } : someLoc;
            return node;
        }

        case Token::OptionNone: {
            // None - Option constructor without value
            auto const loc = _lexer.currentRange();
            _lexer.nextToken(); // consume 'None'
            auto node = std::make_unique<ast::OptionExpr>(false);
            node->location = loc;
            return node;
        }

        case Token::ResultOk: {
            // Ok expr - Result constructor for success
            auto const okLoc = _lexer.currentRange();
            _lexer.nextToken(); // consume 'Ok'
            auto value = parseFSharpPrimary();
            if (!value)
            {
                _report.syntaxErrorWithSuggestions(currentLocation(),
                                                   { "Provide a value after 'Ok'" },
                                                   currentContextSnippet(),
                                                   "Expected expression after 'Ok', got '{}'",
                                                   _lexer.currentTokenText());
                return nullptr;
            }
            auto const endLoc = value->location;
            auto node = std::make_unique<ast::ResultExpr>(true, std::move(value));
            node->location = endLoc ? SourceLocationRange { okLoc.begin, endLoc->end } : okLoc;
            return node;
        }

        case Token::ResultError: {
            // Error expr - Result constructor for error
            auto const errorLoc = _lexer.currentRange();
            _lexer.nextToken(); // consume 'Error'
            auto value = parseFSharpPrimary();
            if (!value)
            {
                _report.syntaxErrorWithSuggestions(currentLocation(),
                                                   { "Provide a value after 'Error'" },
                                                   currentContextSnippet(),
                                                   "Expected expression after 'Error', got '{}'",
                                                   _lexer.currentTokenText());
                return nullptr;
            }
            auto const endLoc = value->location;
            auto node = std::make_unique<ast::ResultExpr>(false, std::move(value));
            node->location = endLoc ? SourceLocationRange { errorLoc.begin, endLoc->end } : errorLoc;
            return node;
        }

        case Token::Try: {
            // try expr with | pattern -> handler | ...
            return parseTryWith();
        }

        case Token::String: {
            // Single-quoted string literal: 'hello'
            auto const loc = _lexer.currentRange();
            std::string value = consumeLiteral();
            auto node =
                std::make_unique<ast::LiteralExpr>(std::move(value), ast::LiteralQuoting::SingleQuoted);
            node->location = loc;
            return node;
        }

        case Token::DblQuoteStart: {
            // Double-quoted string: "hello" (may contain interpolation)
            // For F# context, parse as interpolated string and return the expression
            return parseInterpolatedString();
        }

        case Token::FStringStart: {
            // F#-style interpolated string: $"Hello, {name}"
            return parseFStringExpression();
        }

        default:
            _report.syntaxErrorWithSuggestions(currentLocation(),
                                               {},
                                               currentContextSnippet(),
                                               "Expected expression, got '{}'",
                                               _lexer.currentTokenText());
            return nullptr;
    }
}

// ============================================================================
// List Literal Parsing
// ============================================================================

std::unique_ptr<ast::Expr> Parser::parseListLiteral()
{
    TRACE_SCOPE("parseListLiteral");

    // In F# mode, [ is a separate BracketOpen token
    if (_lexer.currentToken() == Token::BracketOpen)
    {
        return parseListLiteralTokenized();
    }

    // In shell mode (or when [ is part of identifier), parse from literal
    // The lexer handles list literals in different ways depending on content:
    // 1. "[1..10]" or "[]" or "[42]" - lexed as single identifier (no internal semicolons)
    // 2. "[1; 2; 3]" - split by semicolons into multiple tokens: "[1", ";", "2", ";", "3]"
    // 3. "[for x in items -> expr]" - list comprehension (multiple tokens)
    //
    // We handle all cases here.

    auto const& lit = _lexer.currentLiteral();

    if (lit.empty() || lit[0] != '[')
    {
        _report.syntaxErrorWithSuggestions(
            currentLocation(), {}, currentContextSnippet(), "Expected list literal starting with '['");
        return nullptr;
    }

    // Check for list comprehension: [for ...] or [for...] (no space after [)
    // The token could be "[for" or "[" followed by "for" token
    std::string_view afterBracket = std::string_view(lit).substr(1);
    if (afterBracket == "for" || afterBracket.starts_with("for"))
    {
        // This is a list comprehension
        return parseListComprehension();
    }

    // Check if the entire list is in one token (no internal semicolons)
    // by looking for a closing ] in the current token
    size_t depth = 0;
    size_t closePos = std::string::npos;
    for (size_t i = 0; i < lit.size(); ++i)
    {
        if (lit[i] == '[')
            ++depth;
        else if (lit[i] == ']')
        {
            --depth;
            if (depth == 0)
            {
                closePos = i;
                break;
            }
        }
    }

    // Case 1: Entire list is in one token (e.g., "[]", "[42]", "[1..10]")
    if (closePos != std::string::npos)
    {
        std::string_view content = std::string_view(lit).substr(1, closePos - 1);

        // Empty list: []
        if (content.empty())
        {
            _lexer.nextToken();
            return std::make_unique<ast::ListExpr>(std::vector<std::unique_ptr<ast::Expr>> {});
        }

        // Check for range expression: [1..10] or [1..2..10]
        size_t rangePos = std::string::npos;
        size_t bracketDepth = 0;
        for (size_t i = 0; i + 1 < content.size(); ++i)
        {
            if (content[i] == '[')
                ++bracketDepth;
            else if (content[i] == ']')
                --bracketDepth;
            else if (bracketDepth == 0 && content[i] == '.' && content[i + 1] == '.')
            {
                rangePos = i;
                break;
            }
        }

        if (rangePos != std::string::npos)
        {
            // Parse range expression
            return parseListRangeFromContent(content);
        }

        // Single element list (no semicolons in token)
        std::vector<std::unique_ptr<ast::Expr>> elements;
        auto elem = parseListElementFromString(content);
        if (!elem)
            return nullptr;
        elements.push_back(std::move(elem));

        _lexer.nextToken();
        return std::make_unique<ast::ListExpr>(std::move(elements));
    }

    // Case 2: List has internal semicolons, so it's split into multiple tokens
    // Current token is "[1" or just "[", we need to consume until we find "]"

    std::vector<std::unique_ptr<ast::Expr>> elements;

    // Parse first part: "[..." - extract content after '['
    // Use string_view into the literal to avoid dangling reference
    std::string_view firstPart = std::string_view(lit).substr(1); // Skip '['

    // Trim leading whitespace
    while (!firstPart.empty() && std::isspace(static_cast<unsigned char>(firstPart[0])))
        firstPart.remove_prefix(1);

    // If there's content after '[', parse it as first element
    if (!firstPart.empty())
    {
        // Check if first part ends with ']' (single element list like "[42]")
        if (firstPart.back() == ']')
        {
            firstPart.remove_suffix(1);
            if (!firstPart.empty())
            {
                auto elem = parseListElementFromString(firstPart);
                if (!elem)
                    return nullptr;
                elements.push_back(std::move(elem));
            }
            _lexer.nextToken();
            return std::make_unique<ast::ListExpr>(std::move(elements));
        }

        auto elem = parseListElementFromString(firstPart);
        if (!elem)
            return nullptr;
        elements.push_back(std::move(elem));
    }

    _lexer.nextToken(); // consume first token

    // Continue parsing: expect pattern of (';' element)* ']' or 'element]'
    while (true)
    {
        // Check for semicolon separator
        if (_lexer.currentToken() == Token::Semicolon)
        {
            _lexer.nextToken(); // consume ';'
            continue;
        }

        // Check for identifier token (next element or closing bracket)
        if (_lexer.currentToken() == Token::Identifier || _lexer.currentToken() == Token::Number)
        {
            auto const& elemLit = _lexer.currentLiteral();

            // Check if this token ends with ']'
            bool endsWithBracket = !elemLit.empty() && elemLit.back() == ']';

            // Create string_view directly from the reference
            std::string_view elemContent(elemLit);
            if (endsWithBracket)
                elemContent = elemContent.substr(0, elemContent.size() - 1);

            // Trim whitespace
            while (!elemContent.empty() && std::isspace(static_cast<unsigned char>(elemContent[0])))
                elemContent.remove_prefix(1);
            while (!elemContent.empty() && std::isspace(static_cast<unsigned char>(elemContent.back())))
                elemContent.remove_suffix(1);

            if (!elemContent.empty())
            {
                auto elem = parseListElementFromString(elemContent);
                if (!elem)
                    return nullptr;
                elements.push_back(std::move(elem));
            }

            _lexer.nextToken();

            if (endsWithBracket)
            {
                // We found the closing bracket
                return std::make_unique<ast::ListExpr>(std::move(elements));
            }

            continue;
        }

        // Unexpected token
        _report.syntaxErrorWithSuggestions(currentLocation(),
                                           { "Add closing ']'" },
                                           currentContextSnippet(),
                                           "Unexpected token in list literal: '{}'",
                                           _lexer.currentTokenText());
        return nullptr;
    }
}

// Parse list literal when [ is a separate BracketOpen token (F# mode)
std::unique_ptr<ast::Expr> Parser::parseListLiteralTokenized()
{
    TRACE_SCOPE("parseListLiteralTokenized");

    auto const openLoc = _lexer.currentRange();
    _lexer.nextToken(); // consume '['
    consumeNewlines();  // allow newline after '['

    // Empty list: []
    if (_lexer.currentToken() == Token::BracketClose)
    {
        auto const closeLoc = _lexer.currentRange();
        _lexer.nextToken();
        auto node = std::make_unique<ast::ListExpr>(std::vector<std::unique_ptr<ast::Expr>> {});
        node->location = SourceLocationRange { openLoc.begin, closeLoc.end };
        return node;
    }

    // Check for list comprehension: [for ...
    if (_lexer.currentToken() == Token::Identifier && _lexer.currentLiteral() == "for")
    {
        return parseListComprehensionTokenized();
    }

    // Parse first element (range expressions like 1..10 are handled by parseFSharpRange)
    auto firstElem = parseFSharpExpr();
    if (!firstElem)
        return nullptr;

    // If the first element is a range expression and next is ']', return it directly
    if (dynamic_cast<ast::ListRangeExpr*>(firstElem.get()) && _lexer.currentToken() == Token::BracketClose)
    {
        auto const closeLoc = _lexer.currentRange();
        _lexer.nextToken(); // consume ']'
        firstElem->location = SourceLocationRange { openLoc.begin, closeLoc.end };
        return firstElem;
    }

    // Regular list: [elem; elem; ...] or [elem, elem, ...]
    std::vector<std::unique_ptr<ast::Expr>> elements;
    elements.push_back(std::move(firstElem));

    auto useComma = false;
    auto firstSeparator = true;
    while (_lexer.currentToken() == Token::Semicolon || _lexer.currentToken() == Token::Comma)
    {
        if (firstSeparator)
        {
            useComma = _lexer.currentToken() == Token::Comma;
            firstSeparator = false;
        }
        _lexer.nextToken(); // consume separator
        consumeNewlines();  // allow newline after separator

        // Allow trailing separator
        if (_lexer.currentToken() == Token::BracketClose)
            break;

        auto elem = parseFSharpExpr();
        if (!elem)
            return nullptr;
        elements.push_back(std::move(elem));
    }

    consumeNewlines(); // allow newline before ']'

    if (_lexer.currentToken() != Token::BracketClose)
    {
        _report.syntaxErrorWithSuggestions(currentLocation(),
                                           { "Add closing ']'" },
                                           currentContextSnippet(),
                                           "Expected ']' or ';' in list literal, got '{}'",
                                           _lexer.currentTokenText());
        return nullptr;
    }
    auto const closeLoc = _lexer.currentRange();
    _lexer.nextToken(); // consume ']'

    auto node = std::make_unique<ast::ListExpr>(std::move(elements), useComma);
    node->location = SourceLocationRange { openLoc.begin, closeLoc.end };
    return node;
}

// Parse list comprehension when [ is already consumed (F# mode)
std::unique_ptr<ast::Expr> Parser::parseListComprehensionTokenized()
{
    TRACE_SCOPE("parseListComprehensionTokenized");

    auto result = parseComprehensionGenerator();
    if (!result)
        return nullptr;

    // Expect ']'
    if (_lexer.currentToken() != Token::BracketClose)
    {
        _report.syntaxErrorWithSuggestions(currentLocation(),
                                           { "Add closing ']'" },
                                           currentContextSnippet(),
                                           "Expected ']' after list comprehension, got '{}'",
                                           _lexer.currentTokenText());
        return nullptr;
    }
    _lexer.nextToken(); // consume ']'

    return result;
}

std::unique_ptr<ast::Expr> Parser::parseComprehensionGenerator()
{
    TRACE_SCOPE("parseComprehensionGenerator");

    // Expect 'for'
    if (_lexer.currentToken() != Token::Identifier || _lexer.currentLiteral() != "for")
    {
        _report.syntaxErrorWithSuggestions(
            currentLocation(), {}, currentContextSnippet(), "Expected 'for' in list comprehension");
        return nullptr;
    }
    _lexer.nextToken(); // consume 'for'

    // Parse binding variable
    if (_lexer.currentToken() != Token::Identifier)
    {
        _report.syntaxErrorWithSuggestions(currentLocation(),
                                           {},
                                           currentContextSnippet(),
                                           "Expected variable name after 'for', got '{}'",
                                           _lexer.currentTokenText());
        return nullptr;
    }
    std::string varName = consumeLiteral();

    // Expect 'in'
    if (_lexer.currentToken() != Token::Identifier || _lexer.currentLiteral() != "in")
    {
        _report.syntaxErrorWithSuggestions(
            currentLocation(), {}, currentContextSnippet(), "Expected 'in' after variable name");
        return nullptr;
    }
    _lexer.nextToken(); // consume 'in'

    // Parse source expression - may be a range like 1..10 or 1..2..10
    auto source = parseFSharpExpr();
    if (!source)
        return nullptr;

    // Check for range expression: start..end or start..step..end
    if (_lexer.currentToken() == Token::DotDot)
    {
        _lexer.nextToken(); // consume '..'

        auto second = parseFSharpExpr();
        if (!second)
            return nullptr;

        // Check for step: start..step..end
        if (_lexer.currentToken() == Token::DotDot)
        {
            _lexer.nextToken(); // consume '..'

            auto endExpr = parseFSharpExpr();
            if (!endExpr)
                return nullptr;

            // start..step..end
            source = std::make_unique<ast::ListRangeExpr>(
                std::move(source), std::move(second), std::move(endExpr));
        }
        else
        {
            // start..end (no step)
            source = std::make_unique<ast::ListRangeExpr>(std::move(source), nullptr, std::move(second));
        }
    }

    // Check for optional 'when' filter
    std::unique_ptr<ast::Expr> filter;
    if (_lexer.currentToken() == Token::When)
    {
        _lexer.nextToken(); // consume 'when'
        filter = parseFSharpExpr();
        if (!filter)
            return nullptr;
    }

    // Expect '->'
    if (_lexer.currentToken() != Token::Arrow)
    {
        _report.syntaxErrorWithSuggestions(currentLocation(),
                                           {},
                                           currentContextSnippet(),
                                           "Expected '->' in list comprehension, got '{}'",
                                           _lexer.currentTokenText());
        return nullptr;
    }
    _lexer.nextToken(); // consume '->'

    // Parse body — if next token is 'for', recursively parse nested comprehension
    std::unique_ptr<ast::Expr> body;
    if (_lexer.currentToken() == Token::Identifier && _lexer.currentLiteral() == "for")
        body = parseComprehensionGenerator();
    else
        body = parseFSharpExpr();
    if (!body)
        return nullptr;

    return std::make_unique<ast::ListComprehensionExpr>(
        std::move(varName), std::move(source), std::move(filter), std::move(body));
}

// Helper to parse a range expression from content string like "1..10" or "2..2..20"
std::unique_ptr<ast::Expr> Parser::parseListRangeFromContent(std::string_view content)
{
    // Find ".." pattern
    size_t rangePos = std::string::npos;
    size_t bracketDepth = 0;
    for (size_t i = 0; i + 1 < content.size(); ++i)
    {
        if (content[i] == '[')
            ++bracketDepth;
        else if (content[i] == ']')
            --bracketDepth;
        else if (bracketDepth == 0 && content[i] == '.' && content[i + 1] == '.')
        {
            rangePos = i;
            break;
        }
    }

    if (rangePos == std::string::npos)
    {
        _report.syntaxErrorWithSuggestions(
            currentLocation(), {}, currentContextSnippet(), "Expected range '..' in list range expression");
        return nullptr;
    }

    // Parse: [start..end] or [start..step..end]
    std::string_view startPart = content.substr(0, rangePos);
    std::string_view restPart = content.substr(rangePos + 2); // Skip ".."

    // Check for second ".." (step)
    size_t secondRangePos = std::string::npos;
    bracketDepth = 0;
    for (size_t i = 0; i + 1 < restPart.size(); ++i)
    {
        if (restPart[i] == '[')
            ++bracketDepth;
        else if (restPart[i] == ']')
            --bracketDepth;
        else if (bracketDepth == 0 && restPart[i] == '.' && restPart[i + 1] == '.')
        {
            secondRangePos = i;
            break;
        }
    }

    // Parse start value
    auto startExpr = parseListElementFromString(startPart);
    if (!startExpr)
    {
        _report.syntaxErrorWithSuggestions(
            currentLocation(), {}, currentContextSnippet(), "Invalid start value in list range");
        return nullptr;
    }

    std::unique_ptr<ast::Expr> stepExpr;
    std::unique_ptr<ast::Expr> endExpr;

    if (secondRangePos != std::string::npos)
    {
        // Format: [start..step..end]
        std::string_view stepPart = restPart.substr(0, secondRangePos);
        std::string_view endPart = restPart.substr(secondRangePos + 2);

        stepExpr = parseListElementFromString(stepPart);
        if (!stepExpr)
        {
            _report.syntaxErrorWithSuggestions(
                currentLocation(), {}, currentContextSnippet(), "Invalid step value in list range");
            return nullptr;
        }

        endExpr = parseListElementFromString(endPart);
        if (!endExpr)
        {
            _report.syntaxErrorWithSuggestions(
                currentLocation(), {}, currentContextSnippet(), "Invalid end value in list range");
            return nullptr;
        }
    }
    else
    {
        // Format: [start..end]
        endExpr = parseListElementFromString(restPart);
        if (!endExpr)
        {
            _report.syntaxErrorWithSuggestions(
                currentLocation(), {}, currentContextSnippet(), "Invalid end value in list range");
            return nullptr;
        }
    }

    _lexer.nextToken(); // consume the list literal token
    return std::make_unique<ast::ListRangeExpr>(
        std::move(startExpr), std::move(stepExpr), std::move(endExpr));
}

// Helper to parse a single element from a string
std::unique_ptr<ast::Expr> Parser::parseListElementFromString(std::string_view elemStr)
{
    // Trim whitespace
    while (!elemStr.empty() && std::isspace(static_cast<unsigned char>(elemStr.front())))
        elemStr.remove_prefix(1);
    while (!elemStr.empty() && std::isspace(static_cast<unsigned char>(elemStr.back())))
        elemStr.remove_suffix(1);

    if (elemStr.empty())
        return nullptr;

    // Check for bool literals
    if (elemStr == "true")
        return std::make_unique<ast::BoolLiteralExpr>(true);
    if (elemStr == "false")
        return std::make_unique<ast::BoolLiteralExpr>(false);

    // Check for nested list
    if (!elemStr.empty() && elemStr[0] == '[')
    {
        // Nested list - this would require recursive parsing
        // For now, return an error
        _report.syntaxErrorWithSuggestions(
            currentLocation(), {}, currentContextSnippet(), "Nested list literals are not yet supported");
        return nullptr;
    }

    // Try to parse as integer or float
    std::string elemStrCopy(elemStr);
    if (elemStr.find('.') != std::string_view::npos || elemStr.find('e') != std::string_view::npos
        || elemStr.find('E') != std::string_view::npos)
    {
        // Try float
        try
        {
            double value = std::stod(elemStrCopy);
            return std::make_unique<ast::FloatLiteralExpr>(value);
        }
        catch (...)
        {
            // Fall through to identifier
        }
    }
    else
    {
        // Try integer (handle negative numbers too)
        int64_t value = 0;
        auto [ptr, ec] = std::from_chars(elemStr.data(), elemStr.data() + elemStr.size(), value);
        if (ec == std::errc() && ptr == elemStr.data() + elemStr.size())
        {
            return std::make_unique<ast::IntLiteralExpr>(value);
        }
    }

    // String literal (quoted)
    if (elemStr.size() >= 2
        && ((elemStr.front() == '"' && elemStr.back() == '"')
            || (elemStr.front() == '\'' && elemStr.back() == '\'')))
    {
        std::string stringValue(elemStr.substr(1, elemStr.size() - 2));
        return std::make_unique<ast::LiteralExpr>(std::move(stringValue));
    }

    // Identifier
    return std::make_unique<ast::IdentifierExpr>(std::string(elemStr));
}

// Parse list comprehension: [for x in source -> body] or [for x in source when cond -> body]
std::unique_ptr<ast::Expr> Parser::parseListComprehension()
{
    TRACE_SCOPE("parseListComprehension");

    // Current token is "[for" or "[" - we need to extract the start and get to the variable name
    auto const& lit = _lexer.currentLiteral();

    // Handle the case where token is "[for" (no space after [)
    std::string_view afterBracket = std::string_view(lit).substr(1);

    // If afterBracket is exactly "for", consume and move to next token (variable name)
    // If afterBracket is "for" followed by more (like "forx" - unlikely but handle it)
    if (afterBracket == "for")
    {
        _lexer.nextToken(); // consume "[for", next should be variable name
    }
    else if (afterBracket.starts_with("for"))
    {
        // Edge case: "[forx" where "forx" is an identifier, not a comprehension
        // This shouldn't happen in valid code, but handle gracefully
        _report.syntaxErrorWithSuggestions(currentLocation(),
                                           { "Add a space after 'for': [for x in ...]" },
                                           currentContextSnippet(),
                                           "Expected space after 'for' in list comprehension");
        return nullptr;
    }
    else
    {
        // Shouldn't reach here based on caller check, but be defensive
        _report.syntaxErrorWithSuggestions(
            currentLocation(), {}, currentContextSnippet(), "Expected 'for' at start of list comprehension");
        return nullptr;
    }

    // Parse variable name
    if (_lexer.currentToken() != Token::Identifier)
    {
        _report.syntaxErrorWithSuggestions(currentLocation(),
                                           { "Provide a variable name: [for x in ...]" },
                                           currentContextSnippet(),
                                           "Expected variable name after 'for', got '{}'",
                                           _lexer.currentTokenText());
        return nullptr;
    }
    std::string variable = _lexer.currentLiteral();
    _lexer.nextToken();

    // Expect 'in' keyword
    if (!_lexer.isDirective("in"))
    {
        _report.syntaxErrorWithSuggestions(currentLocation(),
                                           { "Add 'in' keyword: [for x in ...]" },
                                           currentContextSnippet(),
                                           "Expected 'in' after variable name, got '{}'",
                                           _lexer.currentTokenText());
        return nullptr;
    }
    _lexer.nextToken(); // consume 'in'

    // Parse source expression - stop at 'when', '->', or ']'
    // Source can be:
    // 1. A single identifier: "items"
    // 2. A range: start..end or start..step..end
    std::unique_ptr<ast::Expr> source;

    // Parse the first element of the source (could be start of a range or just an identifier)
    std::unique_ptr<ast::Expr> firstExpr;
    if (_lexer.currentToken() == Token::Number)
    {
        firstExpr = parseListElementFromString(_lexer.currentLiteral());
        _lexer.nextToken();
    }
    else if (_lexer.currentToken() == Token::Identifier)
    {
        firstExpr = std::make_unique<ast::IdentifierExpr>(_lexer.currentLiteral());
        _lexer.nextToken();
    }
    else
    {
        _report.syntaxErrorWithSuggestions(currentLocation(),
                                           {},
                                           currentContextSnippet(),
                                           "Expected source expression after 'in', got '{}'",
                                           _lexer.currentTokenText());
        return nullptr;
    }

    // Check if this is a range expression (followed by ..)
    if (_lexer.currentToken() == Token::DotDot)
    {
        _lexer.nextToken(); // consume '..'

        // Parse second element (could be end or step)
        std::unique_ptr<ast::Expr> secondExpr;
        if (_lexer.currentToken() == Token::Number)
        {
            secondExpr = parseListElementFromString(_lexer.currentLiteral());
            _lexer.nextToken();
        }
        else if (_lexer.currentToken() == Token::Identifier)
        {
            secondExpr = parseListElementFromString(_lexer.currentLiteral());
            _lexer.nextToken();
        }
        else
        {
            _report.syntaxErrorWithSuggestions(currentLocation(),
                                               {},
                                               currentContextSnippet(),
                                               "Expected value after '..' in range, got '{}'",
                                               _lexer.currentTokenText());
            return nullptr;
        }

        // Check for another '..' (step..end)
        if (_lexer.currentToken() == Token::DotDot)
        {
            _lexer.nextToken(); // consume second '..'

            // Parse the end value
            std::unique_ptr<ast::Expr> endExpr;
            if (_lexer.currentToken() == Token::Number)
            {
                endExpr = parseListElementFromString(_lexer.currentLiteral());
                _lexer.nextToken();
            }
            else if (_lexer.currentToken() == Token::Identifier)
            {
                endExpr = parseListElementFromString(_lexer.currentLiteral());
                _lexer.nextToken();
            }
            else
            {
                _report.syntaxErrorWithSuggestions(currentLocation(),
                                                   {},
                                                   currentContextSnippet(),
                                                   "Expected end value after '..' in range, got '{}'",
                                                   _lexer.currentTokenText());
                return nullptr;
            }

            // start..step..end
            source = std::make_unique<ast::ListRangeExpr>(
                std::move(firstExpr), std::move(secondExpr), std::move(endExpr));
        }
        else
        {
            // start..end (no step)
            source =
                std::make_unique<ast::ListRangeExpr>(std::move(firstExpr), nullptr, std::move(secondExpr));
        }
    }
    else
    {
        // Just a simple identifier/number as source
        source = std::move(firstExpr);
    }

    // Check for optional 'when' filter
    std::unique_ptr<ast::Expr> filter;
    if (_lexer.currentToken() == Token::When)
    {
        _lexer.nextToken(); // consume 'when'

        // Parse filter expression - stop at '->' or ']'
        // For now, parse a simple expression (identifier or comparison)
        filter = parseFSharpComparison();
        if (!filter)
            return nullptr;
    }

    // Expect '->' arrow
    if (_lexer.currentToken() != Token::Arrow)
    {
        _report.syntaxErrorWithSuggestions(currentLocation(),
                                           { "Add '->' before the body expression" },
                                           currentContextSnippet(),
                                           "Expected '->' in list comprehension, got '{}'",
                                           _lexer.currentTokenText());
        return nullptr;
    }
    _lexer.nextToken(); // consume '->'

    // Parse body expression using F# parser for proper expression handling (including nesting)
    _lexer.enterFSharpExpr();
    std::unique_ptr<ast::Expr> body;
    if (_lexer.currentToken() == Token::Identifier && _lexer.currentLiteral() == "for")
        body = parseComprehensionGenerator();
    else
        body = parseFSharpExpr();
    _lexer.leaveFSharpExpr();
    if (!body)
        return nullptr;

    // Expect ']'
    if (_lexer.currentToken() != Token::BracketClose)
    {
        _report.syntaxErrorWithSuggestions(currentLocation(),
                                           { "Add closing ']'" },
                                           currentContextSnippet(),
                                           "Expected ']' after list comprehension, got '{}'",
                                           _lexer.currentTokenText());
        return nullptr;
    }
    _lexer.nextToken(); // consume ']'

    return std::make_unique<ast::ListComprehensionExpr>(
        std::move(variable), std::move(source), std::move(filter), std::move(body));
}

// ============================================================================
// Pattern Parsing for Match Expressions
// ============================================================================

std::unique_ptr<ast::MatchExpr> Parser::parseMatch()
{
    TRACE_SCOPE("parseMatch");

    // Consume 'match' keyword
    if (_lexer.currentToken() != Token::Match)
        return nullptr;
    auto const matchLoc = _lexer.currentRange();
    _lexer.nextToken();

    // Parse scrutinee expression
    auto scrutinee = parseFSharpTupleExpr();
    if (!scrutinee)
    {
        _report.syntaxErrorWithSuggestions(currentLocation(),
                                           { "Add an expression to match against" },
                                           currentContextSnippet(),
                                           "Expected expression after 'match'");
        return nullptr;
    }

    // Expect 'with' keyword
    if (_lexer.currentToken() != Token::With)
    {
        _report.syntaxErrorWithSuggestions(currentLocation(),
                                           { "Add 'with' keyword" },
                                           currentContextSnippet(),
                                           "Expected 'with' after match expression, got '{}'",
                                           _lexer.currentTokenText());
        return nullptr;
    }
    _lexer.nextToken(); // consume 'with'

    // Parse match arms: | pattern -> expr
    // Skip newlines between 'with' and first '|', and between arms.
    std::vector<ast::MatchArm> arms;

    while (true)
    {
        auto const skippedNewlines = consumeNewlines();
        if (_lexer.currentToken() != Token::Pipe)
        {
            // Not a continuation arm. Push back a newline if we consumed any,
            // so the caller's expression parser sees the statement boundary.
            if (skippedNewlines)
                _lexer.pushBackToken(Token::LineFeed, "\n");
            break;
        }

        auto const pipeColumn = currentTokenColumn();
        _lexer.nextToken(); // consume '|'

        // Lambda: parse a pattern element that may be a bare tuple (comma-separated).
        // Comma binds tighter than '|', so `Some f, Some l | None, None` parses as
        // `(Some f, Some l) | (None, None)`.
        auto parseArmPattern = [this]() -> pattern::PatternPtr {
            auto pat = parseAsPattern();
            if (!pat)
                return nullptr;
            if (_lexer.currentToken() != Token::Comma)
                return pat;
            std::vector<pattern::PatternPtr> elements;
            elements.push_back(std::move(pat));
            while (_lexer.currentToken() == Token::Comma)
            {
                _lexer.nextToken(); // consume ','
                auto elem = parseAsPattern();
                if (!elem)
                    return nullptr;
                elements.push_back(std::move(elem));
            }
            return pattern::patterns::tuple(std::move(elements));
        };

        // Parse first pattern of this arm (may be a bare tuple)
        auto pattern = parseArmPattern();
        if (!pattern)
        {
            _report.syntaxErrorWithSuggestions(
                currentLocation(), {}, currentContextSnippet(), "Expected pattern after '|'");
            return nullptr;
        }

        // Check for or-pattern alternatives: | pat1 | pat2 | pat3 -> expr
        // Each alternative may itself be a bare tuple pattern.
        consumeUntilNotOneOf(Token::LineFeed);
        if (_lexer.currentToken() == Token::Pipe)
        {
            std::vector<pattern::PatternPtr> alternatives;
            alternatives.push_back(std::move(pattern));
            while (_lexer.currentToken() == Token::Pipe)
            {
                _lexer.nextToken(); // consume '|'
                auto alt = parseArmPattern();
                if (!alt)
                {
                    _report.syntaxErrorWithSuggestions(
                        currentLocation(), {}, currentContextSnippet(), "Expected pattern after '|'");
                    return nullptr;
                }
                // If this alternative is followed by '->' or 'when', it's the last one
                alternatives.push_back(std::move(alt));
                consumeUntilNotOneOf(Token::LineFeed);
                if (_lexer.currentToken() == Token::Arrow || _lexer.currentToken() == Token::When)
                    break;
            }
            pattern = pattern::patterns::or_(std::move(alternatives));
        }

        // Check for optional 'when' guard
        std::unique_ptr<ast::Expr> guard = nullptr;
        if (_lexer.currentToken() == Token::When)
        {
            _lexer.nextToken(); // consume 'when'
            guard = parseFSharpExpr();
            if (!guard)
            {
                _report.syntaxErrorWithSuggestions(
                    currentLocation(), {}, currentContextSnippet(), "Expected expression after 'when'");
                return nullptr;
            }
        }

        // Expect '->' arrow
        if (_lexer.currentToken() != Token::Arrow)
        {
            _report.syntaxErrorWithSuggestions(currentLocation(),
                                               { "Add '->' after pattern" },
                                               currentContextSnippet(),
                                               "Expected '->' after pattern, got '{}'",
                                               _lexer.currentTokenText());
            return nullptr;
        }
        auto const arrowLine = _lexer.currentRange().begin.line;
        _lexer.nextToken();                    // consume '->'
        consumeUntilNotOneOf(Token::LineFeed); // allow arm body on next line

        // Parse body expression — use sequence parsing for multi-line bodies
        auto const bodyOnNewLine = _lexer.currentRange().begin.line > arrowLine;
        auto body = bodyOnNewLine ? parseFSharpExprSequence(pipeColumn) : parseFSharpExpr();
        if (!body)
        {
            _report.syntaxErrorWithSuggestions(
                currentLocation(), {}, currentContextSnippet(), "Expected expression after '->'");
            return nullptr;
        }

        arms.emplace_back(std::move(pattern), std::move(guard), std::move(body));
    }

    if (arms.empty())
    {
        _report.syntaxErrorWithSuggestions(currentLocation(),
                                           { "Add at least one match arm: | pattern -> expr" },
                                           currentContextSnippet(),
                                           "Match expression requires at least one arm");
        return nullptr;
    }

    auto node = std::make_unique<ast::MatchExpr>(std::move(scrutinee), std::move(arms));
    // Span from 'match' to last arm body
    auto const& lastArm = node->arms.back();
    if (lastArm.body && lastArm.body->location)
        node->location = SourceLocationRange { matchLoc.begin, lastArm.body->location->end };
    else
        node->location = matchLoc;
    return node;
}

std::unique_ptr<pattern::Pattern> Parser::parsePattern()
{
    TRACE_SCOPE("parsePattern");
    // For match arms, we handle 'when' in parseMatch() itself
    // This method just parses the pattern without guard
    return parseOrPattern();
}

std::unique_ptr<pattern::Pattern> Parser::parseOrPattern()
{
    TRACE_SCOPE("parseOrPattern");

    auto first = parseAsPattern();
    if (!first)
        return nullptr;

    // Check for '|' alternatives (but not in match arm context)
    // Note: In match context, '|' starts a new arm, so we don't parse it here
    // This handles patterns like: "quit" | "exit" within a single arm
    std::vector<pattern::PatternPtr> alternatives;
    alternatives.push_back(std::move(first));

    // Or-patterns within match arms are handled in parseMatchArm() (see '|' alternative parsing there).
    // This function handles or-patterns in non-match contexts.

    if (alternatives.size() == 1)
        return std::move(alternatives[0]);

    return pattern::patterns::or_(std::move(alternatives));
}

std::unique_ptr<pattern::Pattern> Parser::parseAsPattern()
{
    TRACE_SCOPE("parseAsPattern");

    auto inner = parseConsPattern();
    if (!inner)
        return nullptr;

    // Check for 'as' binding
    if (_lexer.currentToken() == Token::As)
    {
        _lexer.nextToken(); // consume 'as'

        if (_lexer.currentToken() != Token::Identifier)
        {
            _report.syntaxErrorWithSuggestions(
                currentLocation(), {}, currentContextSnippet(), "Expected identifier after 'as'");
            return nullptr;
        }
        std::string name = _lexer.currentLiteral();
        _lexer.nextToken();

        return pattern::patterns::as(std::move(inner), std::move(name));
    }

    return inner;
}

std::unique_ptr<pattern::Pattern> Parser::parseConsPattern()
{
    TRACE_SCOPE("parseConsPattern");

    auto head = parsePrimaryPattern();
    if (!head)
        return nullptr;

    // Check for '::' cons operator (lexed as ColonColon in F# mode, or Identifier "::" in shell mode)
    if (_lexer.currentToken() == Token::ColonColon
        || (_lexer.currentToken() == Token::Identifier && _lexer.currentLiteral() == "::"))
    {
        _lexer.nextToken(); // consume '::'

        auto tail = parseConsPattern(); // Right-associative
        if (!tail)
        {
            _report.syntaxErrorWithSuggestions(
                currentLocation(), {}, currentContextSnippet(), "Expected pattern after '::'");
            return nullptr;
        }

        return pattern::patterns::cons(std::move(head), std::move(tail));
    }

    return head;
}

bool Parser::canStartPattern() const
{
    switch (_lexer.currentToken())
    {
        case Token::Identifier:
        case Token::Number:
        case Token::String:
        case Token::DblQuoteStart:
        case Token::True:
        case Token::False:
        case Token::RndOpen:
        case Token::OptionSome:
        case Token::OptionNone:
        case Token::ResultOk:
        case Token::ResultError:
        case Token::Mut:
        case Token::BracketOpen:
        case Token::BraceOpen: return true;
        default: return false;
    }
}

std::unique_ptr<pattern::Pattern> Parser::parsePrimaryPattern()
{
    TRACE_SCOPE("parsePrimaryPattern");

    switch (_lexer.currentToken())
    {
        case Token::OptionSome:
        case Token::OptionNone:
        case Token::ResultOk:
        case Token::ResultError: {
            // Constructor pattern: Some, None, Ok, Error
            std::string name = _lexer.currentLiteral();
            Token constructorToken = _lexer.currentToken();
            _lexer.nextToken();

            // Check for optional payload (Some x, Ok value, Error e)
            std::optional<pattern::PatternPtr> payload = std::nullopt;
            if (constructorToken != Token::OptionNone)
            {
                // These constructors expect a payload, check if one follows
                if (canStartPattern())
                {
                    payload = parsePrimaryPattern();
                    if (!payload)
                        return nullptr;
                }
            }

            return pattern::patterns::constructor(std::move(name), std::move(payload));
        }

        case Token::Number: {
            // Numeric literal
            auto const& lit = _lexer.currentLiteral();

            // Check for float (contains '.' or 'e'/'E')
            if (lit.find('.') != std::string::npos || lit.find('e') != std::string::npos
                || lit.find('E') != std::string::npos)
            {
                double value = std::stod(lit);
                _lexer.nextToken();
                return pattern::patterns::literal(value);
            }

            // Parse as integer
            int64_t value = 0;
            auto [ptr, ec] = std::from_chars(lit.data(), lit.data() + lit.size(), value);
            if (ec != std::errc())
                value = 0;
            _lexer.nextToken();
            return pattern::patterns::literal(value);
        }

        case Token::String: {
            // String literal pattern (single-quoted)
            std::string str = _lexer.currentLiteral();
            // Remove quotes if present
            if (!str.empty() && (str.front() == '"' || str.front() == '\''))
                str = str.substr(1);
            if (!str.empty() && (str.back() == '"' || str.back() == '\''))
                str = str.substr(0, str.size() - 1);
            _lexer.nextToken();
            return pattern::patterns::literal(std::move(str));
        }

        case Token::DblQuoteStart: {
            // Double-quoted string literal pattern: "hello"
            _lexer.nextToken(); // consume DblQuoteStart
            std::string str;
            if (_lexer.currentToken() == Token::StringFragment)
            {
                str = _lexer.currentLiteral();
                _lexer.nextToken(); // consume StringFragment
            }
            if (_lexer.currentToken() != Token::DblQuoteEnd)
            {
                _report.syntaxErrorWithSuggestions(currentLocation(),
                                                   {},
                                                   currentContextSnippet(),
                                                   "Only plain string literals are allowed in patterns");
                return nullptr;
            }
            _lexer.nextToken(); // consume DblQuoteEnd
            return pattern::patterns::literal(std::move(str));
        }

        case Token::True: {
            _lexer.nextToken();
            return pattern::patterns::literal(true);
        }
        case Token::False: {
            _lexer.nextToken();
            return pattern::patterns::literal(false);
        }

        case Token::Identifier: {
            auto const& lit = _lexer.currentLiteral();

            // Check for special identifiers
            if (lit == "_")
            {
                // Wildcard pattern
                _lexer.nextToken();
                return pattern::patterns::wildcard();
            }

            // User-defined constructor pattern
            if (_constructorLookup.contains(lit))
            {
                auto payloadSlots = _constructorPayloadSlots[lit];
                auto ctorName = consumeLiteral();

                std::optional<pattern::PatternPtr> payload = std::nullopt;
                if (payloadSlots > 0 && canStartPattern())
                {
                    if (payloadSlots > 1)
                    {
                        // Multi-slot constructors expect a tuple pattern for the payload
                        payload = parsePrimaryPattern();
                    }
                    else
                    {
                        payload = parsePrimaryPattern();
                    }
                    if (!payload)
                        return nullptr;
                }

                return pattern::patterns::constructor(std::move(ctorName), std::move(payload));
            }

            // In pattern context, the lexer may have consumed more than we want.
            // Shell lexer includes commas in identifiers (for brace expansion like {a,b,c}).
            // In F# pattern context, we need to stop at commas.
            // Check if the identifier ends with a comma and split it.
            if (lit.size() > 1 && lit.back() == ',')
            {
                // Extract the variable name without the trailing comma.
                // The tuple parser will detect this via hadTrailingComma.
                std::string name = lit.substr(0, lit.size() - 1);
                _lexer.nextToken();
                return pattern::patterns::variable(std::move(name));
            }

            // Regular identifier (variable pattern)
            std::string name = consumeLiteral();
            return pattern::patterns::variable(std::move(name));
        }

        case Token::RndOpen:
            // Tuple pattern: (a, b, c)
            return parseTuplePattern();

        case Token::BracketOpen:
            // List pattern: [a; b; c] or []
            return parseListPattern();

        case Token::BraceOpen:
            // Record pattern: { name; age } or { name = n; _ }
            return parseRecordPattern();

        case Token::Mut: {
            // Mutable variable pattern: mut x
            _lexer.nextToken(); // consume 'mut'

            if (_lexer.currentToken() != Token::Identifier)
            {
                _report.syntaxErrorWithSuggestions(
                    currentLocation(), {}, currentContextSnippet(), "Expected identifier after 'mut'");
                return nullptr;
            }
            std::string name = consumeLiteral();
            return pattern::patterns::variable(std::move(name), true);
        }

        default:
            _report.syntaxErrorWithSuggestions(currentLocation(),
                                               {},
                                               currentContextSnippet(),
                                               "Expected pattern, got '{}'",
                                               _lexer.currentTokenText());
            return nullptr;
    }
}

std::unique_ptr<pattern::Pattern> Parser::parseTuplePattern()
{
    TRACE_SCOPE("parseTuplePattern");

    if (_lexer.currentToken() != Token::RndOpen)
        return nullptr;
    _lexer.nextToken(); // consume '('

    std::vector<pattern::PatternPtr> elements;

    // Empty tuple: () is unit pattern
    if (_lexer.currentToken() == Token::RndClose)
    {
        _lexer.nextToken();
        return pattern::patterns::tuple(std::move(elements));
    }

    // Helper to parse a pattern element, handling comma-containing identifiers.
    // Returns the pattern and sets hadTrailingComma if the literal ended with ','.
    auto parsePatternElement = [this](bool& hadTrailingComma) -> pattern::PatternPtr {
        hadTrailingComma = false;

        // Save the literal before parsing to check for trailing comma
        std::string savedLit;
        if (_lexer.currentToken() == Token::Identifier)
            savedLit = _lexer.currentLiteral();

        auto pat = parsePattern();
        if (!pat)
            return nullptr;

        // Check if the original literal ended with a comma
        // and the pattern consumed it (pattern literal is one char shorter)
        if (!savedLit.empty() && savedLit.back() == ',')
            hadTrailingComma = true;

        return pat;
    };

    // Parse first element
    bool hadComma = false;
    auto first = parsePatternElement(hadComma);
    if (!first)
        return nullptr;
    elements.push_back(std::move(first));

    // If the first element's literal had a trailing comma, we need to continue parsing
    // remaining elements without looking for a comma separator.
    while (hadComma)
    {
        auto elem = parsePatternElement(hadComma);
        if (!elem)
            return nullptr;
        elements.push_back(std::move(elem));
    }

    // Parse remaining elements separated by ','
    // Note: In F# mode, comma is lexed as Token::Comma. In shell context,
    // comma is lexed as part of identifiers. We need to handle both cases.
    auto const startsWithComma = [this]() {
        if (_lexer.currentToken() == Token::Comma)
            return true;
        return _lexer.currentToken() == Token::Identifier && !_lexer.currentLiteral().empty()
               && _lexer.currentLiteral()[0] == ',';
    };

    while (startsWithComma())
    {
        // In F# mode, comma is a separate token
        if (_lexer.currentToken() == Token::Comma)
        {
            _lexer.nextToken(); // consume ','
            auto elem = parsePattern();
            if (!elem)
                return nullptr;
            elements.push_back(std::move(elem));
            continue;
        }

        auto const& lit = _lexer.currentLiteral();

        if (lit == ",")
        {
            // Just a comma, consume it and parse next element normally
            _lexer.nextToken();
        }
        else
        {
            // Comma followed by more content (e.g., ",0" or ",x")
            // The content after the comma is the next pattern element.
            // We need to extract it and parse it.
            std::string rest = lit.substr(1);

            // Try to parse the rest as a pattern element inline
            // First, advance past this token
            _lexer.nextToken();

            // Now we need to parse 'rest' as a pattern. Since the lexer doesn't support
            // pushback, we handle common cases inline:
            if (rest == "_")
            {
                elements.push_back(pattern::patterns::wildcard());
            }
            else if (rest == "true")
            {
                elements.push_back(pattern::patterns::literal(true));
            }
            else if (rest == "false")
            {
                elements.push_back(pattern::patterns::literal(false));
            }
            else if (!rest.empty() && (std::isdigit(rest[0]) || (rest[0] == '-' && rest.size() > 1)))
            {
                // Integer literal
                int64_t value = 0;
                auto [ptr, ec] = std::from_chars(rest.data(), rest.data() + rest.size(), value);
                if (ec == std::errc())
                    elements.push_back(pattern::patterns::literal(value));
                else
                    elements.push_back(pattern::patterns::variable(std::move(rest)));
            }
            else if (!rest.empty() && (std::isalpha(rest[0]) || rest[0] == '_'))
            {
                // Variable pattern
                elements.push_back(pattern::patterns::variable(std::move(rest)));
            }
            else
            {
                _report.syntaxErrorWithSuggestions(currentLocation(),
                                                   {},
                                                   currentContextSnippet(),
                                                   "Invalid pattern element in tuple: '{}'",
                                                   rest);
                return nullptr;
            }
            continue; // Already added the element, continue to next
        }

        auto elem = parsePattern();
        if (!elem)
            return nullptr;
        elements.push_back(std::move(elem));
    }

    if (_lexer.currentToken() != Token::RndClose)
    {
        _report.syntaxErrorWithSuggestions(currentLocation(),
                                           { "Add closing ')'" },
                                           currentContextSnippet(),
                                           "Expected ')' or ',' in tuple pattern, got '{}'",
                                           _lexer.currentTokenText());
        return nullptr;
    }
    _lexer.nextToken(); // consume ')'

    // Single element in parens is just a parenthesized pattern, not a tuple
    if (elements.size() == 1)
        return std::move(elements[0]);

    return pattern::patterns::tuple(std::move(elements));
}

std::unique_ptr<pattern::Pattern> Parser::parseListPattern()
{
    TRACE_SCOPE("parseListPattern");

    // '[' already current token
    _lexer.nextToken(); // consume '['

    // Empty list pattern: []
    if (_lexer.currentToken() == Token::BracketClose)
    {
        _lexer.nextToken(); // consume ']'
        return std::make_unique<pattern::ListPattern>(std::vector<pattern::PatternPtr> {});
    }

    // Parse elements separated by ';'
    std::vector<pattern::PatternPtr> elements;
    auto first = parseConsPattern();
    if (!first)
        return nullptr;
    elements.push_back(std::move(first));

    while (_lexer.currentToken() == Token::Semicolon)
    {
        _lexer.nextToken(); // consume ';'
        auto elem = parseConsPattern();
        if (!elem)
            return nullptr;
        elements.push_back(std::move(elem));
    }

    if (_lexer.currentToken() != Token::BracketClose)
    {
        _report.syntaxErrorWithSuggestions(
            currentLocation(), {}, currentContextSnippet(), "Expected ']' to close list pattern");
        return nullptr;
    }
    _lexer.nextToken(); // consume ']'

    return std::make_unique<pattern::ListPattern>(std::move(elements));
}

std::unique_ptr<pattern::Pattern> Parser::parseRecordPattern()
{
    TRACE_SCOPE("parseRecordPattern");

    _lexer.nextToken(); // consume '{'
    consumeNewlines();

    std::vector<pattern::FieldPattern> fields;
    auto hasWildcard = false;

    while (_lexer.currentToken() != Token::BraceClose && _lexer.currentToken() != Token::EndOfInput)
    {
        // Check for wildcard: _
        if (_lexer.currentToken() == Token::Identifier && _lexer.currentLiteral() == "_")
        {
            hasWildcard = true;
            _lexer.nextToken(); // consume '_'
        }
        else if (_lexer.currentToken() == Token::Identifier)
        {
            auto fieldName = _lexer.currentLiteral();
            _lexer.nextToken(); // consume field name

            if (_lexer.currentToken() == Token::Equal)
            {
                // Explicit binding: name = pattern
                _lexer.nextToken(); // consume '='
                auto pat = parsePattern();
                if (!pat)
                    return nullptr;
                fields.push_back(pattern::FieldPattern { std::move(fieldName), std::move(pat) });
            }
            else
            {
                // Punning: { name } means { name = name }
                fields.push_back(pattern::FieldPattern { std::move(fieldName), nullptr });
            }
        }
        else
        {
            _report.syntaxErrorWithSuggestions(currentLocation(),
                                               { "Provide a field name or '_'" },
                                               currentContextSnippet(),
                                               "Expected field name in record pattern, got '{}'",
                                               _lexer.currentTokenText());
            return nullptr;
        }

        // Consume separator: semicolon or newline
        if (_lexer.currentToken() == Token::Semicolon)
            _lexer.nextToken();
        consumeNewlines();
    }

    if (_lexer.currentToken() != Token::BraceClose)
    {
        _report.syntaxErrorWithSuggestions(currentLocation(),
                                           { "Add a closing '}'" },
                                           currentContextSnippet(),
                                           "Expected '}}' at end of record pattern, got '{}'",
                                           _lexer.currentTokenText());
        return nullptr;
    }
    _lexer.nextToken(); // consume '}'

    return std::make_unique<pattern::RecordPattern>(std::move(fields), hasWildcard);
}

} // namespace endo
