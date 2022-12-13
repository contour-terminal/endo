// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <shell/AST.h>
#include <shell/Lexer.h>

#include <CoreVM/Diagnostics.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace CoreVM
{
class Runtime;
}

namespace crush
{

class Parser
{
  public:
    explicit Parser(CoreVM::Runtime& runtime,
                    CoreVM::diagnostics::Report& report,
                    std::unique_ptr<Source> source);

    std::unique_ptr<ast::Statement> parse();

  private:
    [[nodiscard]] bool isEndOfBlock() const noexcept;
    [[nodiscard]] bool isEndOfStmt() const noexcept;
    std::unique_ptr<ast::Statement> parseBlock(std::string_view traceMessage = {});
    std::unique_ptr<ast::Statement> parseStmt();
    std::string consumeLiteral();
    std::unique_ptr<ast::IfStmt> parseIf();
    std::unique_ptr<ast::WhileStmt> parseWhile();
    std::unique_ptr<ast::ProgramCall> parseCall();
    std::vector<std::unique_ptr<ast::Expr>> parseParameterList();
    std::unique_ptr<ast::Expr> parseParameter();
    std::unique_ptr<ast::CallPipeline> parseCallPipeline(std::unique_ptr<ast::ProgramCall> call);

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

    void consumeDirective(const std::string& directive);

    CoreVM::Runtime& _runtime;            // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
    CoreVM::diagnostics::Report& _report; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
    Lexer _lexer;
};

} // namespace crush
