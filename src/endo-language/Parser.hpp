// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <CoreVM/CoreVM.hpp>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include "AST.hpp"
#include "DiagnosticsAdapter.hpp"
#include "Lexer.hpp"

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

    /// Sets the known F# function names for bare top-level call dispatch.
    void setKnownFSharpFunctions(std::unordered_set<std::string> names);

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
    std::unique_ptr<ast::Expr> parseInterpolatedString();
    std::unique_ptr<ast::Expr> parseFStringExpression();

    // Arithmetic expression parser (for $((expr)))
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

    // Type annotation parsing
    TypePtr parseType();     ///< Parses function types: baseType (-> type)*
    TypePtr parseBaseType(); ///< Parses primitive, generic, paren, tuple types

    /// Parses a typed parameter: bare identifier or `(name: type)`.
    /// @return The parsed TypedParameter, or std::nullopt if no parameter found.
    std::optional<ast::TypedParameter> parseTypedParameter();

    // F# style let bindings and expressions
    std::unique_ptr<ast::LetBindingStmt> parseLet();
    std::unique_ptr<ast::LetInExpr> parseLetInExpr();
    std::unique_ptr<ast::LambdaExpr> parseLambda();
    std::unique_ptr<ast::MatchExpr> parseMatch();
    std::unique_ptr<ast::Expr> parseListLiteral();          ///< [1; 2; 3], [1..10], [for x in items -> x * 2]
    std::unique_ptr<ast::Expr> parseListLiteralTokenized(); ///< List literal when [ is BracketOpen token
    std::unique_ptr<ast::Expr> parseListComprehensionTokenized(); ///< List comprehension when [ is consumed
    std::unique_ptr<ast::Expr> parseListRangeFromContent(
        std::string_view content); ///< Helper for range expressions
    std::unique_ptr<ast::Expr> parseListElementFromString(
        std::string_view elemStr);                       ///< Helper to parse a list element
    std::unique_ptr<ast::Expr> parseListComprehension(); ///< [for x in source -> body]

    // F# expression parser (precedence climbing)
    // Precedence (low to high): |> || && comparisons +- */% ** unary application
    std::unique_ptr<ast::Expr> parseFSharpExpr();        // Entry point
    std::unique_ptr<ast::Expr> parseFSharpPipeline();    // |>
    std::unique_ptr<ast::Expr> parseFSharpComposition(); // >> <<
    std::unique_ptr<ast::Expr> parseFSharpOr();          // ||
    std::unique_ptr<ast::Expr> parseFSharpAnd();         // &&
    std::unique_ptr<ast::Expr> parseFSharpCons();        // :: (right-associative)
    std::unique_ptr<ast::Expr> parseFSharpComparison();  // == != < <= > >=
    std::unique_ptr<ast::Expr> parseFSharpAddSub();      // + -
    std::unique_ptr<ast::Expr> parseFSharpMulDivMod();   // * / %
    std::unique_ptr<ast::Expr> parseFSharpPow();         // **
    std::unique_ptr<ast::Expr> parseFSharpUnary();       // - !
    std::unique_ptr<ast::Expr> parseFSharpApplication(); // function application f x
    std::unique_ptr<ast::Expr> parseFSharpPostfix();     // postfix ? (error propagation)
    std::unique_ptr<ast::Expr> parseFSharpPrimary();     // literals, identifiers, (expr), fun ..., & command
    std::unique_ptr<ast::Expr> parseBlockExprOrRecord(); // { ... } — block, record literal, or record update
    std::unique_ptr<ast::Expr> parseBlockExpr();         // { let x = 1; x + 2 }
    std::unique_ptr<ast::Expr> parseShellCommandExpr();  // & git status (shell command in F# context)
    std::unique_ptr<ast::Expr> parseTryWith();           // try expr with ... | try expr finally ...

    // Type definitions (records and discriminated unions)
    std::unique_ptr<ast::Statement> parseTypeDefinition(); // type T = { ... } or type T = | A | B of int

    // Pattern parsing for match expressions
    // Grammar: pattern ::= or_pattern ('when' expr)?
    //          or_pattern ::= as_pattern ('|' as_pattern)*
    //          as_pattern ::= cons_pattern ('as' IDENT)?
    //          cons_pattern ::= primary ('::' cons_pattern)?
    //          primary ::= literal | IDENT | '_' | constructor | '(' tuple ')' | '[' list ']' | '{' record
    //          '}'
    std::unique_ptr<pattern::Pattern> parsePattern(); // Entry point (handles guards internally for MatchArm)
    std::unique_ptr<pattern::Pattern> parseOrPattern();      // `pat1 | pat2 | pat3`
    std::unique_ptr<pattern::Pattern> parseAsPattern();      // `pattern as name`
    std::unique_ptr<pattern::Pattern> parseConsPattern();    // `head :: tail`
    std::unique_ptr<pattern::Pattern> parsePrimaryPattern(); // Literals, identifiers, wildcards
    std::unique_ptr<pattern::Pattern> parseTuplePattern();   // `(a, b, c)`
    std::unique_ptr<pattern::Pattern> parseListPattern();    // `[a; b; c]` or `[a; rest...]`
    std::unique_ptr<pattern::Pattern> parseRecordPattern();  // `{ name; age = a }` (requires lexer extension)

    /// Check if the current token can start a pattern
    [[nodiscard]] bool canStartPattern() const;

    /// Check if looking at start of F# primary expression
    [[nodiscard]] bool isFSharpPrimary() const noexcept;

    /// Get current token column (1-based, for indentation tracking)
    [[nodiscard]] size_t currentTokenColumn() const noexcept;

    std::unique_ptr<ast::Expr> parseParameter();
    std::unique_ptr<ast::Statement> parsePrimaryStmt();
    std::unique_ptr<ast::Statement> parseLogicalExpr();
    std::unique_ptr<ast::Statement> parseCallPipeline();

    /// Skips LineFeed and Semicolon tokens (newlines between continuation lines).
    /// Returns true if any were consumed.
    bool consumeNewlines();

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
    std::unordered_set<std::string>
        _knownFSharpFunctions; ///< User-defined F# function names for bare call dispatch

    /// Known record type names → field names (populated by parseTypeDefinition).
    std::unordered_map<std::string, std::vector<std::string>> _knownRecordTypes;

    /// Maps constructor names to (type name, variant index) for user-defined discriminated unions.
    std::unordered_map<std::string, std::pair<std::string, size_t>> _constructorLookup;

    /// Maps constructor names to their payload slot count.
    std::unordered_map<std::string, uint8_t> _constructorPayloadSlots;

    int _placeholderCount = 0;            ///< Number of `_` placeholders in current scope
    bool _placeholderScopeActive = false; ///< Suppresses postfix wrapping inside parens
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
