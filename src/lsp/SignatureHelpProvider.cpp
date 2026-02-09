// SPDX-License-Identifier: Apache-2.0
#include "SignatureHelpProvider.hpp"

#include <vector>

#include "StubRuntime.hpp"
#include "SymbolCollector.hpp"
#include <endo-language/AST.hpp>
#include <endo-language/Lexer.hpp>
#include <endo-language/Parser.hpp>
#include <endo-language/Type.hpp>

namespace endo::lsp
{

namespace
{

    /// Information about a function call found in the AST.
    struct CallInfo
    {
        std::string functionName; ///< Name of the function being called
        int argCount = 0;         ///< Number of arguments applied so far
    };

    /// Unwraps an ApplicationExpr chain to find the root function name and argument count.
    /// `f a b` is represented as `App(App(f, a), b)` — 2 arguments.
    [[nodiscard]] std::optional<CallInfo> unwrapApplication(ast::ApplicationExpr const& app)
    {
        auto argCount = 1; // This application itself provides one argument

        // Walk the function chain leftward
        auto const* current = &app;
        while (auto const* inner = dynamic_cast<ast::ApplicationExpr const*>(current->function.get()))
        {
            ++argCount;
            current = inner;
        }

        // The innermost function should be an identifier
        if (auto const* ident = dynamic_cast<ast::IdentifierExpr const*>(current->function.get()))
        {
            return CallInfo {
                .functionName = ident->name,
                .argCount = argCount,
            };
        }

        return std::nullopt;
    }

    /// Collects all top-level ApplicationExpr chains from an expression tree.
    void collectCalls(ast::Expr const& expr, std::vector<CallInfo>& calls)
    {
        if (auto const* e = dynamic_cast<ast::ApplicationExpr const*>(&expr))
        {
            if (auto info = unwrapApplication(*e))
                calls.push_back(*info);
            // Recurse into arguments for nested calls (walk the chain)
            auto const* cur = e;
            while (auto const* inner = dynamic_cast<ast::ApplicationExpr const*>(cur->function.get()))
            {
                collectCalls(*cur->argument, calls);
                cur = inner;
            }
            collectCalls(*cur->argument, calls);
            return;
        }
        if (auto const* e = dynamic_cast<ast::ParenExpr const*>(&expr))
            collectCalls(*e->inner, calls);
        else if (auto const* e = dynamic_cast<ast::BinaryExpr const*>(&expr))
        {
            collectCalls(*e->left, calls);
            collectCalls(*e->right, calls);
        }
        else if (auto const* e = dynamic_cast<ast::IfExpr const*>(&expr))
        {
            collectCalls(*e->condition, calls);
            collectCalls(*e->thenExpr, calls);
            if (e->elseExpr)
                collectCalls(*e->elseExpr, calls);
        }
        else if (auto const* e = dynamic_cast<ast::LetInExpr const*>(&expr))
        {
            if (e->value)
                collectCalls(*e->value, calls);
            if (e->body)
                collectCalls(*e->body, calls);
        }
        else if (auto const* e = dynamic_cast<ast::LambdaExpr const*>(&expr))
        {
            collectCalls(*e->body, calls);
        }
        else if (auto const* e = dynamic_cast<ast::MatchExpr const*>(&expr))
        {
            collectCalls(*e->scrutinee, calls);
            for (auto const& arm: e->arms)
                if (arm.body)
                    collectCalls(*arm.body, calls);
        }
        else if (auto const* e = dynamic_cast<ast::PipelineExpr const*>(&expr))
        {
            collectCalls(*e->value, calls);
            collectCalls(*e->function, calls);
        }
        else if (auto const* e = dynamic_cast<ast::TupleExpr const*>(&expr))
        {
            for (auto const& elem: e->elements)
                collectCalls(*elem, calls);
        }
        else if (auto const* e = dynamic_cast<ast::ListExpr const*>(&expr))
        {
            for (auto const& elem: e->elements)
                collectCalls(*elem, calls);
        }
    }

    /// Collects all function application chains from statements.
    void collectCallsInStmt(ast::Node const& node, std::vector<CallInfo>& calls)
    {
        if (auto const* compound = dynamic_cast<ast::CompoundStmt const*>(&node))
        {
            for (auto const& stmt: compound->statements)
                collectCallsInStmt(*stmt, calls);
        }
        else if (auto const* letStmt = dynamic_cast<ast::LetBindingStmt const*>(&node))
        {
            if (letStmt->value)
                collectCalls(*letStmt->value, calls);
        }
        else if (auto const* exprStmt = dynamic_cast<ast::ExprStmt const*>(&node))
        {
            collectCalls(*exprStmt->expr, calls);
        }
    }

    /// Looks up a function definition by name in the symbol table.
    [[nodiscard]] SymbolDefinition const* findFunctionDef(SymbolTable const& table, std::string const& name)
    {
        for (auto const& def: table.definitions)
        {
            if (def.name == name && def.isFunction)
                return &def;
        }
        return nullptr;
    }

    /// Builds a signature label string from a function definition.
    [[nodiscard]] std::string buildSignatureLabel(SymbolDefinition const& def)
    {
        auto label = def.name;
        for (size_t i = 0; i < def.parameterNames.size(); ++i)
        {
            label += " ";
            auto const& paramName = def.parameterNames[i];
            auto const& paramType = i < def.parameterTypes.size() ? def.parameterTypes[i] : std::string {};
            if (!paramType.empty())
                label += "(" + paramName + ": " + paramType + ")";
            else
                label += paramName;
        }
        if (def.returnType)
            label += ": " + *def.returnType;
        return label;
    }

    /// Token info for position matching.
    struct TokInfo
    {
        Token token;
        std::string literal;
        int line;
        int col0;    ///< 0-based start column
        int endCol0; ///< 0-based end column (exclusive)
    };

    /// Tokenizes the source and returns all tokens with 0-based position info.
    [[nodiscard]] std::vector<TokInfo> tokenizeSource(std::string const& source)
    {
        std::vector<TokInfo> tokens;
        auto lexer = Lexer { std::make_unique<StringSource>(source) };
        lexer.enterFSharpExpr();
        while (lexer.currentToken() != Token::EndOfInput)
        {
            auto const range = lexer.currentRange();
            auto const lit = lexer.currentLiteral();
            auto const col0 = range.begin.column > 0 ? range.begin.column - 1 : 0;
            tokens.push_back(TokInfo {
                .token = lexer.currentToken(),
                .literal = lit,
                .line = range.begin.line,
                .col0 = col0,
                .endCol0 = col0 + static_cast<int>(lit.size()),
            });
            lexer.nextToken();
        }
        return tokens;
    }

} // namespace

std::optional<SignatureHelp> computeSignatureHelp(std::string const& source, Position position)
{
    // Parse AST to find function applications
    CoreVM::Runtime runtime;
    registerStubRuntime(runtime);

    CoreVM::diagnostics::BufferedReport report;
    Parser parser(runtime, report, std::make_unique<StringSource>(source));
    auto astRoot = parser.parse();
    if (!astRoot)
        return std::nullopt;

    std::vector<CallInfo> calls;
    collectCallsInStmt(*astRoot, calls);
    if (calls.empty())
        return std::nullopt;

    // Get symbol table for function definitions
    auto table = collectSymbols(source);
    if (!table)
        return std::nullopt;

    // Tokenize source for position matching
    auto tokens = tokenizeSource(source);

    // For each call, find the function name token on the cursor's line and check
    // if the cursor is in the argument region (after the function name).
    // Pick the closest function name before the cursor as the best match.
    struct Match
    {
        CallInfo const* call;
        SymbolDefinition const* def;
        int funcTokenIdx;
    };

    std::optional<Match> bestMatch;

    for (auto const& call: calls)
    {
        auto const* funcDef = findFunctionDef(*table, call.functionName);
        if (!funcDef || funcDef->parameterNames.empty())
            continue;

        // Find function name tokens on the cursor's line
        for (auto i = 0; i < static_cast<int>(tokens.size()); ++i)
        {
            auto const& tok = tokens[static_cast<size_t>(i)];
            if (tok.token != Token::Identifier || tok.literal != call.functionName)
                continue;
            if (tok.line != position.line)
                continue;
            if (tok.col0 > position.character)
                continue; // Function name starts after cursor

            // Cursor must be after the function name token
            if (position.character >= tok.endCol0)
            {
                if (!bestMatch || i > bestMatch->funcTokenIdx)
                    bestMatch = Match { &call, funcDef, i };
            }
        }
    }

    if (!bestMatch)
        return std::nullopt;

    // Count arguments between the function name token and the cursor position.
    // Each top-level token (at parenthesis depth 0) after the function name counts as one argument.
    auto argCount = 0;
    auto parenDepth = 0;
    for (auto i = bestMatch->funcTokenIdx + 1; i < static_cast<int>(tokens.size()); ++i)
    {
        auto const& tok = tokens[static_cast<size_t>(i)];
        if (tok.line != position.line)
            break;
        if (tok.col0 > position.character)
            break;

        if (tok.token == Token::RndOpen)
        {
            if (parenDepth == 0)
                ++argCount; // Parenthesized expression counts as one argument
            ++parenDepth;
            continue;
        }
        if (tok.token == Token::RndClose)
        {
            --parenDepth;
            continue;
        }

        if (parenDepth == 0)
            ++argCount;
    }

    // Determine active parameter (0-indexed, clamped to parameter count)
    auto activeParam = argCount > 0 ? argCount - 1 : 0;
    if (activeParam >= static_cast<int>(bestMatch->def->parameterNames.size()))
        activeParam = static_cast<int>(bestMatch->def->parameterNames.size()) - 1;

    // Build signature information
    auto sigInfo = SignatureInformation {
        .label = buildSignatureLabel(*bestMatch->def),
    };

    for (size_t i = 0; i < bestMatch->def->parameterNames.size(); ++i)
    {
        auto paramLabel = bestMatch->def->parameterNames[i];
        auto const& paramType =
            i < bestMatch->def->parameterTypes.size() ? bestMatch->def->parameterTypes[i] : std::string {};
        if (!paramType.empty())
            paramLabel += ": " + paramType;
        sigInfo.parameters.push_back(ParameterInformation { .label = std::move(paramLabel) });
    }

    return SignatureHelp {
        .signatures = { std::move(sigInfo) },
        .activeSignature = 0,
        .activeParameter = activeParam,
    };
}

} // namespace endo::lsp
