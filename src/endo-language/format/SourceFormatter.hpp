// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <endo-language/ast/AST.hpp>
#include <endo-language/ast/Pattern.hpp>
#include <endo-language/ast/Visitor.hpp>
#include <endo-language/format/FormatConfig.hpp>
#include <endo-language/lexer/CommentTrivia.hpp>

#include <string>
#include <vector>

namespace endo::format
{

/// Source code formatter for Endo programs.
///
/// Implements `ast::Visitor` and `pattern::PatternVisitor` to traverse the AST and produce
/// properly formatted source code. Handles indentation, line-width awareness, comment
/// interleaving, and both shell and F# language constructs.
class SourceFormatter: public ast::Visitor, public pattern::PatternVisitor
{
  public:
    /// Formats an Endo source string.
    ///
    /// Parses the source, collects comments via the lexer's trivia system, then
    /// traverses the AST to produce formatted output. If parsing fails, returns
    /// the original source unchanged.
    /// @param source The source code to format.
    /// @param config Formatting configuration.
    /// @return The formatted source code.
    [[nodiscard]] static std::string format(std::string const& source, FormatConfig const& config = {});

    /// Formats an already-parsed AST with collected comments.
    ///
    /// @param root The root AST node.
    /// @param comments Collected comment trivia from the lexer.
    /// @param config Formatting configuration.
    /// @return The formatted source code.
    [[nodiscard]] static std::string format(ast::Node const& root,
                                            std::vector<CommentTrivia> const& comments,
                                            FormatConfig const& config = {});

    // ========================================================================
    // ast::Visitor overrides — Shell constructs
    // ========================================================================
    void visit(ast::FileDescriptor const& node) override;
    void visit(ast::InputRedirect const& node) override;
    void visit(ast::OutputRedirect const& node) override;
    void visit(ast::HereDocument const& node) override;
    void visit(ast::HereString const& node) override;
    void visit(ast::ProgramCall const& node) override;
    void visit(ast::CallPipeline const& node) override;
    void visit(ast::CompoundStmt const& node) override;
    void visit(ast::WhileStmt const& node) override;
    void visit(ast::ForInStmt const& node) override;
    void visit(ast::BreakStmt const& node) override;
    void visit(ast::ContinueStmt const& node) override;
    void visit(ast::LogicalAndStmt const& node) override;
    void visit(ast::LogicalOrStmt const& node) override;
    void visit(ast::BuiltinExitStmt const& node) override;
    void visit(ast::BuiltinExportStmt const& node) override;
    void visit(ast::BuiltinReadStmt const& node) override;
    void visit(ast::BuiltinChDirStmt const& node) override;
    void visit(ast::BuiltinSetStmt const& node) override;
    void visit(ast::BuiltinUnsetStmt const& node) override;
    void visit(ast::BuiltinJobsStmt const& node) override;
    void visit(ast::BuiltinFgStmt const& node) override;
    void visit(ast::BuiltinBgStmt const& node) override;
    void visit(ast::BuiltinWaitStmt const& node) override;
    void visit(ast::BuiltinBindStmt const& node) override;
    void visit(ast::BuiltinWhichStmt const& node) override;
    void visit(ast::LiteralExpr const& node) override;
    void visit(ast::VariableExpr const& node) override;
    void visit(ast::TildeExpr const& node) override;
    void visit(ast::GlobExpr const& node) override;
    void visit(ast::ConcatExpr const& node) override;
    void visit(ast::ArithExpansionExpr const& node) override;
    void visit(ast::ParamExpansionExpr const& node) override;
    void visit(ast::SubstitutionExpr const& node) override;
    void visit(ast::CommandFileSubst const& node) override;
    void visit(ast::StructuredPipelineSourceExpr const& node) override;
    void visit(ast::DataSourceExpr const& node) override;

    // ========================================================================
    // ast::Visitor overrides — F# expressions and statements
    // ========================================================================
    void visit(ast::IfExpr const& node) override;
    void visit(ast::TupleExpr const& node) override;
    void visit(ast::UnitExpr const& node) override;
    void visit(ast::BlockExpr const& node) override;
    void visit(ast::MutAssignStmt const& node) override;
    void visit(ast::MutAssignExpr const& node) override;
    void visit(ast::LetBindingStmt const& node) override;
    void visit(ast::LetInExpr const& node) override;
    void visit(ast::ExprStmt const& node) override;
    void visit(ast::BinaryExpr const& node) override;
    void visit(ast::UnaryExpr const& node) override;
    void visit(ast::PipelineExpr const& node) override;
    void visit(ast::ApplicationExpr const& node) override;
    void visit(ast::IdentifierExpr const& node) override;
    void visit(ast::IntLiteralExpr const& node) override;
    void visit(ast::FloatLiteralExpr const& node) override;
    void visit(ast::BoolLiteralExpr const& node) override;
    void visit(ast::SizeLiteralExpr const& node) override;
    void visit(ast::BreakExpr const& node) override;
    void visit(ast::ContinueExpr const& node) override;
    void visit(ast::ParenExpr const& node) override;
    void visit(ast::LambdaExpr const& node) override;
    void visit(ast::MatchExpr const& node) override;
    void visit(ast::ListExpr const& node) override;
    void visit(ast::ConsExpr const& node) override;
    void visit(ast::ConcatListExpr const& node) override;
    void visit(ast::ListRangeExpr const& node) override;
    void visit(ast::ListComprehensionExpr const& node) override;
    void visit(ast::ShellCommandExpr const& node) override;
    void visit(ast::SplatExpr const& node) override;
    void visit(ast::OptionExpr const& node) override;
    void visit(ast::ResultExpr const& node) override;
    void visit(ast::TryExpr const& node) override;
    void visit(ast::OptionDefaultExpr const& node) override;
    void visit(ast::TryWithExpr const& node) override;
    void visit(ast::TryFinallyExpr const& node) override;
    void visit(ast::FStringExpr const& node) override;
    void visit(ast::RecordTypeDefStmt const& node) override;
    void visit(ast::RecordExpr const& node) override;
    void visit(ast::RecordUpdateExpr const& node) override;
    void visit(ast::FieldAccessExpr const& node) override;
    void visit(ast::OptionalChainExpr const& node) override;
    void visit(ast::UnionTypeDefStmt const& node) override;
    void visit(ast::UnionConstructorExpr const& node) override;
    void visit(ast::ExecPipelineExpr const& node) override;

    // ========================================================================
    // pattern::PatternVisitor overrides
    // ========================================================================
    void visit(pattern::LiteralPattern const& pat) override;
    void visit(pattern::VariablePattern const& pat) override;
    void visit(pattern::WildcardPattern const& pat) override;
    void visit(pattern::TuplePattern const& pat) override;
    void visit(pattern::ListPattern const& pat) override;
    void visit(pattern::ConsPattern const& pat) override;
    void visit(pattern::RecordPattern const& pat) override;
    void visit(pattern::ConstructorPattern const& pat) override;
    void visit(pattern::AsPattern const& pat) override;
    void visit(pattern::OrPattern const& pat) override;
    void visit(pattern::GuardedPattern const& pat) override;

  private:
    explicit SourceFormatter(FormatConfig config, std::vector<CommentTrivia> const& comments);

    // Output helpers
    void emit(std::string_view text);
    void emitNewline();
    void emitIndent();
    void emitSpace();
    void emitBlankLine();

    // Indentation management
    void indent();
    void dedent();

    // Comment interleaving
    void emitLeadingComments(ast::Node const& node);
    void emitCommentsBeforeLine(int line);
    void emitTrailingComment(ast::Node const& node);
    void emitRemainingComments();

    /// Finds the first source line in a node's subtree (for nodes without locations).
    [[nodiscard]] static std::optional<int> findFirstLine(ast::Node const& node);

    /// Finds the last source line in a node's subtree (for trailing comment detection).
    [[nodiscard]] static std::optional<int> findLastLine(ast::Node const& node);

    // Width estimation for line-break decisions
    [[nodiscard]] size_t estimateWidth(ast::Node const& node) const;

    // Arithmetic expression helper (mirrors ASTPrinter)
    void printArithExpr(ast::ArithExpr const* expr);

    // Comprehension helper
    void printComprehensionGenerator(ast::ListComprehensionExpr const& node);

    // Pattern printing helper
    void emitPattern(pattern::Pattern const& pat);

    // Let-binding parameter helper
    void emitParameters(std::vector<ast::TypedParameter> const& params);

    /// Checks whether an expression is too complex to sit alongside a keyword on the same line.
    [[nodiscard]] static bool isCompoundExpr(ast::Expr const& expr);

    FormatConfig _config;
    std::string _result;
    int _indentLevel = 0;
    bool _atLineStart = true;
    std::vector<CommentTrivia> const& _comments;
    size_t _nextCommentIndex = 0; ///< Index of next un-emitted comment
};

} // namespace endo::format
