// SPDX-License-Identifier: Apache-2.0
module;

#include <shell/AST.h>
#include <shell/DiagnosticsAdapter.h>
#include <shell/ScopedLogger.h>

#include <crispy/utils.h>

#include <memory>
#include <optional>

#include "LogConfig.h"

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

import ASTPrinter;
import Lexer;
import CoreVM;

export module Parser;

namespace endo
{

export class Parser
{
  public:
    explicit Parser(CoreVM::Runtime& runtime,
                    CoreVM::diagnostics::Report& report,
                    std::unique_ptr<Source> source):
        _runtime { runtime }, _report { report }, _lexer { std::move(source) }
    {
    }

    std::unique_ptr<ast::Statement> parse() { return parseBlock("global"); }

    /// Sets the source text for context snippets in error messages.
    void setSourceText(std::string_view source) { _sourceText = source; }

  private:
    /// Converts the current lexer location to CoreVM SourceLocation format.
    [[nodiscard]] CoreVM::SourceLocation currentLocation() const { return toCoreLoc(_lexer.currentRange()); }

    /// Gets the context snippet for the current line.
    [[nodiscard]] std::optional<std::string> currentContextSnippet() const
    {
        if (_sourceText.empty())
            return std::nullopt;
        auto const line = extractSourceLine(_sourceText, _lexer.currentRange().begin.line);
        return line.empty() ? std::nullopt : std::make_optional(line);
    }

    [[nodiscard]] bool isEndOfBlock() const noexcept
    {
        // clang-format off
    return _lexer.currentToken() == Token::EndOfInput
        || _lexer.isDirective("else")
        || _lexer.isDirective("elif")
        || _lexer.isDirective("fi")
        || _lexer.isDirective("done");
        // clang-format on
    }

    [[nodiscard]] bool isEndOfStmt() const noexcept
    {
        // clang-format off
    return _lexer.currentToken() == Token::EndOfInput
        || _lexer.currentToken() == Token::LineFeed
        || _lexer.currentToken() == Token::Pipe
        || _lexer.currentToken() == Token::Semicolon;
        // clang-format on
    }

    std::unique_ptr<ast::Statement> parseBlock(std::string_view traceMessage = {})
    {
        TRACE_SCOPE(
            std::format("parseBlock{}", traceMessage.empty() ? "" : std::format(" ({})", traceMessage)));
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
        TRACE_FMT("Parsed scope.3 (current token: {}): {}",
                  _lexer.currentLiteral(),
                  ast::ASTPrinter::print(*scope));
        return scope;
    }

    std::unique_ptr<ast::Statement> parseStmt()
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
                else if (_lexer.isDirective("exit"))
                {
                    _lexer.nextToken();
                    std::unique_ptr<ast::Expr> code;
                    if (!isEndOfStmt())
                        code = parseParameter();
                    assert(_runtime.find("exit(I)V") != nullptr);
                    return std::make_unique<ast::BuiltinExitStmt>(*_runtime.find("exit(I)V"),
                                                                  std::move(code));
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
                    CoreVM::NativeCallback const& callback =
                        *_runtime.find(parameters.empty() ? "read()S" : "read(s)S");
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
                        return std::make_unique<ast::BuiltinChDirStmt>(*_runtime.find("cd(S)B"),
                                                                       std::move(param));
                    }
                }
                else if (_lexer.isDirective("unset"))
                {
                    _lexer.nextToken();
                    auto name = consumeLiteral();
                    return std::make_unique<ast::BuiltinUnsetStmt>(*_runtime.find("unset(S)B"), name);
                }
                else
                {
                    return parseCallPipeline();
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

    std::string consumeLiteral()
    {
        auto literal = _lexer.currentLiteral();
        _lexer.nextToken();
        return literal;
    }

    std::unique_ptr<ast::IfStmt> parseIf()
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
            TRACE_FMT("Parsing else branch (current token: {}, '{}')",
                      _lexer.currentToken(),
                      _lexer.currentLiteral());
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

        return std::make_unique<ast::IfStmt>(
            std::move(condition), std::move(thenBranch), std::move(elseBranch));
    }

    std::unique_ptr<ast::WhileStmt> parseWhile()
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

    /// Checks if the current token is a redirect token.
    [[nodiscard]] bool isRedirectToken() const noexcept
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

    /// Checks if the current token is a number that could be an fd prefix for a redirect.
    [[nodiscard]] bool isNumberBeforeRedirect() const noexcept
    {
        if (_lexer.currentToken() != Token::Number)
            return false;

        // We need to look ahead to see if a redirect operator follows
        // This is tricky without peeking, so for now we assume numbers followed by
        // redirect tokens are fd prefixes. The lexer doesn't consume whitespace between them.
        return true; // Will be validated in parseRedirects
    }

    /// Parses a redirect operator and its target.
    /// Returns true if a redirect was parsed, false otherwise.
    bool parseRedirect(std::vector<std::unique_ptr<ast::InputRedirect>>& inputRedirects,
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
                    outputRedirects.emplace_back(std::make_unique<ast::OutputRedirect>(
                        std::make_unique<ast::FileDescriptor>(sourceFd),
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

    std::unique_ptr<ast::ProgramCall> parseCall(bool piped = false)
    {
        TRACE_SCOPE("parseCall");
        std::string program = consumeLiteral();
        std::vector<std::unique_ptr<ast::Expr>> arguments;
        std::vector<std::unique_ptr<ast::InputRedirect>> inputRedirects;
        std::vector<std::unique_ptr<ast::OutputRedirect>> outputRedirects;
        std::vector<std::unique_ptr<ast::HereDocument>> hereDocuments;
        std::vector<std::unique_ptr<ast::HereString>> hereStrings;

        // Parse arguments and redirects interleaved
        while (!isEndOfStmt())
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
                                    std::make_unique<ast::FileDescriptor>(fdValue),
                                    std::move(target),
                                    false));
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
                arguments.emplace_back(std::move(arg));
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

    std::vector<std::unique_ptr<ast::Expr>> parseParameterList()
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

    std::unique_ptr<ast::Expr> parseParameter()
    {
        TRACE_FMT("parseParameter: {} \"{}\"", _lexer.currentToken(), _lexer.currentLiteral());
        switch (_lexer.currentToken())
        {
            case Token::String:
            case Token::Number:
            case Token::Identifier: return std::make_unique<ast::LiteralExpr>(consumeLiteral());
            case Token::DollarName:
                return std::make_unique<ast::VariableExpr>(consumeLiteral(), ast::VariableType::Named, false);
            case Token::DollarBraceName:
                return std::make_unique<ast::VariableExpr>(consumeLiteral(), ast::VariableType::Named, true);
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
            default:
                _report.syntaxErrorWithSuggestions(currentLocation(),
                                                   {},
                                                   currentContextSnippet(),
                                                   "Expected parameter, got '{}'",
                                                   _lexer.currentLiteral());
                return nullptr;
        }
    }

    std::unique_ptr<ast::Statement> parseCallPipeline()
    {
        TRACE_SCOPE("parseCallPipeline");

        auto call = parseCall();
        if (!call)
            return nullptr;

        if (_lexer.currentToken() != Token::Pipe)
            return call;

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

        return std::make_unique<ast::CallPipeline>(std::move(calls));
    }

    bool tryConsumeToken(Token token)
    {
        if (_lexer.currentToken() != token)
            return false;
        _lexer.nextToken();
        return true;
    }

    bool consumeOneOf(Token token)
    {
        if (_lexer.currentToken() != token)
            return false;
        _lexer.nextToken();
        return true;
    }

    template <typename... T>
    bool consumeOneOf(Token a, T... tokens)
    {
        return consumeOneOf(a) || ((consumeOneOf(tokens) || ...));
    }

    template <typename... T>
    bool consumeUntilNotOneOf(T... token)
    {
        if (!consumeOneOf(token...))
            return false;

        while (consumeOneOf(token...))
            ;

        return true;
    }

    void consumeDirective(const std::string& directive)
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

    CoreVM::Runtime& _runtime;            // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
    CoreVM::diagnostics::Report& _report; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
    Lexer _lexer;
    std::string_view _sourceText; ///< Original source text for context snippets
};

} // namespace endo
