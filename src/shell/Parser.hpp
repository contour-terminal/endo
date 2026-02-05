// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "Lexer.hpp"

#include <shell/AST.hpp>
#include <shell/DiagnosticsAdapter.hpp>

#include <CoreVM/CoreVM.hpp>

#include <memory>
#include <optional>
#include <string_view>

namespace endo
{

class Parser
{
  public:
    explicit Parser(CoreVM::Runtime& runtime,
                    CoreVM::diagnostics::Report& report,
                    std::unique_ptr<Source> source);

    std::unique_ptr<ast::Statement> parse();

    /// Sets the source text for context snippets in error messages.
    void setSourceText(std::string_view source);

  private:
    /// Converts the current lexer location to CoreVM SourceLocation format.
    [[nodiscard]] CoreVM::SourceLocation currentLocation() const;

    /// Gets the context snippet for the current line.
    [[nodiscard]] std::optional<std::string> currentContextSnippet() const;

    [[nodiscard]] bool isEndOfBlock() const noexcept;
    [[nodiscard]] bool isEndOfStmt() const noexcept;
    [[nodiscard]] bool isParameterToken() const noexcept;

    std::unique_ptr<ast::Statement> parseBlock(std::string_view traceMessage = {});
    std::unique_ptr<ast::Statement> parseStmt();
    std::string consumeLiteral();
    std::unique_ptr<ast::IfStmt> parseIf();
    std::unique_ptr<ast::WhileStmt> parseWhile();
    std::unique_ptr<ast::Statement> parseFor();
    std::unique_ptr<ast::ForListStmt> parseForList();
    std::unique_ptr<ast::ForCStyleStmt> parseForCStyle();
    std::unique_ptr<ast::CaseStmt> parseCase();
    [[nodiscard]] bool isFunctionDefinition() const noexcept;
    std::unique_ptr<ast::FunctionDefStmt> parseFunctionDef();
    std::unique_ptr<ast::ReturnStmt> parseReturn();
    std::unique_ptr<ast::BreakStmt> parseBreak();
    std::unique_ptr<ast::ContinueStmt> parseContinue();
    [[nodiscard]] bool isRedirectToken() const noexcept;
    [[nodiscard]] bool isNumberBeforeRedirect() const noexcept;

    bool parseRedirect(std::vector<std::unique_ptr<ast::InputRedirect>>& inputRedirects,
                       std::vector<std::unique_ptr<ast::OutputRedirect>>& outputRedirects,
                       std::vector<std::unique_ptr<ast::HereDocument>>& hereDocuments,
                       std::vector<std::unique_ptr<ast::HereString>>& hereStrings);

    std::unique_ptr<ast::ProgramCall> parseCall(bool piped = false);
    std::vector<std::unique_ptr<ast::Expr>> parseParameterList();
    std::unique_ptr<ast::SubstitutionExpr> parseCommandSubstitution();
    std::unique_ptr<ast::SubstitutionExpr> parseBacktickSubstitution();
    std::unique_ptr<ast::CommandFileSubst> parseProcessSubstitution(ast::ProcessSubstMode mode);
    std::unique_ptr<ast::ParamExpansionExpr> parseParamExpansion();
    std::unique_ptr<ast::TildeExpr> parseTildeExpansion();
    std::unique_ptr<ast::ArithExpansionExpr> parseArithmeticExpansion();

    // Arithmetic expression parser
    std::unique_ptr<ast::ArithExpr> parseArithOr();
    std::unique_ptr<ast::ArithExpr> parseArithAnd();
    std::unique_ptr<ast::ArithExpr> parseArithBitOr();
    std::unique_ptr<ast::ArithExpr> parseArithBitXor();
    std::unique_ptr<ast::ArithExpr> parseArithBitAnd();
    std::unique_ptr<ast::ArithExpr> parseArithEquality();
    std::unique_ptr<ast::ArithExpr> parseArithComparison();
    std::unique_ptr<ast::ArithExpr> parseArithShift();
    std::unique_ptr<ast::ArithExpr> parseArithAddSub();
    std::unique_ptr<ast::ArithExpr> parseArithMulDiv();
    std::unique_ptr<ast::ArithExpr> parseArithPow();
    std::unique_ptr<ast::ArithExpr> parseArithUnary();
    std::unique_ptr<ast::ArithExpr> parseArithPrimary();

    std::unique_ptr<ast::Expr> parseParameter();
    std::unique_ptr<ast::Statement> parsePrimaryStmt();
    std::unique_ptr<ast::Statement> parseLogicalExpr();
    std::unique_ptr<ast::Statement> parseCallPipeline();

    bool tryConsumeToken(Token token);
    bool consumeOneOf(Token token);

    template <typename... T>
    bool consumeOneOf(Token a, T... tokens);

    template <typename... T>
    bool consumeUntilNotOneOf(T... token);

    void consumeDirective(const std::string& directive);

    // Brace expansion helpers
    [[nodiscard]] static bool containsGlobChars(std::string_view s);
    [[nodiscard]] static bool containsBracePattern(std::string_view s);
    [[nodiscard]] static size_t findMatchingBrace(std::string_view s, size_t start);
    [[nodiscard]] static std::vector<std::string> splitBraceItems(std::string_view content);
    [[nodiscard]] static std::vector<std::string> expandRange(std::string_view range);
    [[nodiscard]] static std::vector<std::string> expandBraces(std::string const& input);

    CoreVM::Runtime& _runtime;            // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
    CoreVM::diagnostics::Report& _report; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
    Lexer _lexer;
    std::string_view _sourceText;  ///< Original source text for context snippets
    int _backtickNestingLevel = 0; ///< Nesting level for backtick substitution
};

// Template implementations
template <typename... T>
bool Parser::consumeOneOf(Token a, T... tokens)
{
    return consumeOneOf(a) || ((consumeOneOf(tokens) || ...));
}

template <typename... T>
bool Parser::consumeUntilNotOneOf(T... token)
{
    if (!consumeOneOf(token...))
        return false;

    while (consumeOneOf(token...))
        ;

    return true;
}

} // namespace endo
