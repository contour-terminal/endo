// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <endo-language/ast/AST.hpp>
#include <endo-language/ast/Pattern.hpp>
#include <endo-language/ast/Visitor.hpp>
#include <endo-language/format/FormatConfig.hpp>
#include <endo-language/lexer/CommentTrivia.hpp>

#include <functional>
#include <optional>
#include <set>
#include <string>
#include <string_view>
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
                                            FormatConfig const& config = {},
                                            std::set<int> blankLines = {});

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
    void visit(ast::CompositionExpr const& node) override;
    void visit(ast::PlaceholderLambdaExpr const& node) override;
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
    void visit(ast::TimeSpanLiteralExpr const& node) override;
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
    void visit(ast::LazyExpr const& node) override;
    void visit(ast::RefExpr const& node) override;
    void visit(ast::SeqExpr const& node) override;
    void visit(ast::FStringExpr const& node) override;
    void visit(ast::RecordTypeDefStmt const& node) override;
    void visit(ast::RecordExpr const& node) override;
    void visit(ast::RecordUpdateExpr const& node) override;
    void visit(ast::FieldAccessExpr const& node) override;
    void visit(ast::OptionalChainExpr const& node) override;
    void visit(ast::UnionTypeDefStmt const& node) override;
    void visit(ast::UnionConstructorExpr const& node) override;
    void visit(ast::ExecPipelineExpr const& node) override;

    // Module system
    void visit(ast::ImportStmt const& node) override;
    void visit(ast::OpenStmt const& node) override;
    void visit(ast::ModuleDeclStmt const& node) override;

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
    /// Formats an if-expression, using the given keyword ("if" or "elif") as the leading keyword.
    void formatIfExpr(ast::IfExpr const& node, std::string_view keyword);

    explicit SourceFormatter(FormatConfig config,
                             std::vector<CommentTrivia> const& comments,
                             std::set<int> blankLines = {},
                             std::string_view originalSource = {});

    /// Checks whether any blank line exists in the original source between two line numbers.
    ///
    /// @param afterLine  0-based line number of the previous statement's last line.
    /// @param beforeLine 0-based line number of the next statement's first line.
    /// @return true if at least one blank line exists in the exclusive range (afterLine, beforeLine).
    [[nodiscard]] bool hasBlankLineBetween(int afterLine, int beforeLine) const;

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

    /// Emits inline comments that appear on the given line before the given column.
    /// Used for mid-line block comments like `let x = (* inline *) 42`.
    void emitInlineComments(int line, int beforeColumn);

    /// Emits non-trailing comments that fall between the last body statement
    /// and a block's closing keyword (e.g., `end`).
    void emitDanglingBodyComments(ast::Node const& blockParent);

    /// Finds the first source line in a node's subtree (for nodes without locations).
    [[nodiscard]] static std::optional<int> findFirstLine(ast::Node const& node);

    /// A contiguous range of original source lines to reproduce verbatim during formatting.
    /// Delimited by `# endo format off` / `# endo format on` directive comments. Both line
    /// numbers are 0-based and inclusive; @ref beginLine is the `off` directive line and
    /// @ref endLine is the `on` directive line (or the last source line if `on` is absent).
    struct VerbatimRegion
    {
        int beginLine; ///< 0-based line of the `# endo format off` directive (inclusive).
        int endLine;   ///< 0-based line of the `# endo format on` directive (inclusive).
    };

    /// Scans collected comments for `# endo format off` / `# endo format on` directives and
    /// builds the list of verbatim regions. Nested `off` directives are ignored (the first
    /// `off` wins until the next `on`); an `off` without a matching `on` extends to EOF.
    /// @param comments The collected comment trivia.
    /// @param totalLines Total number of lines in the original source (for unterminated regions).
    /// @return Sorted, non-overlapping list of verbatim regions.
    [[nodiscard]] static std::vector<VerbatimRegion> collectVerbatimRegions(
        std::vector<CommentTrivia> const& comments, int totalLines);

    /// Returns the verbatim region containing the given 0-based line, if any.
    [[nodiscard]] std::optional<VerbatimRegion> verbatimRegionAt(int line) const;

    /// Emits the original source lines in [beginLine, endLine] (0-based, inclusive) verbatim,
    /// and advances the comment cursor past every comment that falls within the range so the
    /// normal comment-interleaving machinery does not re-emit them.
    void emitVerbatimRegion(VerbatimRegion const& region);

    /// Finds the last source line in a node's subtree (for trailing comment detection).
    [[nodiscard]] static std::optional<int> findLastLine(ast::Node const& node);

    // Width estimation for line-break decisions
    [[nodiscard]] static size_t estimateWidth(ast::Node const& node);

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

    /// Checks whether a LambdaExpr is eligible for simplification to placeholder syntax (`_`).
    [[nodiscard]] static bool canSimplifyToPlaceholder(ast::LambdaExpr const& node);

    /// Checks whether an expression would produce multiline output when formatted.
    [[nodiscard]] bool wouldFormatMultiline(ast::Expr const& expr) const;

    /// Returns the current column position (0-based) in the output.
    [[nodiscard]] size_t currentColumn() const;

    /// Emits a list of expression elements with adaptive wrapping.
    ///
    /// When the inline form fits within maxLineWidth, emits all elements on one line.
    /// Otherwise, uses bin-packing for simple elements or one-per-line for complex ones.
    ///
    /// @param elements  The expressions to emit.
    /// @param separator Separator between elements (e.g., "; " or ", ").
    /// @param open      Opening delimiter (e.g., "[", "(").
    /// @param close     Closing delimiter (e.g., "]", ")").
    void emitWrappedElements(std::vector<std::unique_ptr<ast::Expr>> const& elements,
                             std::string_view separator,
                             std::string_view open,
                             std::string_view close);

    /// Emits a list of items with adaptive wrapping using a callback for each item.
    ///
    /// @param count          Number of items.
    /// @param separator      Separator between items.
    /// @param open           Opening delimiter.
    /// @param close          Closing delimiter.
    /// @param emitItem       Callback that emits item at given index.
    /// @param estimateItem   Callback that returns estimated width of item at given index.
    void emitWrappedWith(size_t count,
                         std::string_view separator,
                         std::string_view open,
                         std::string_view close,
                         std::function<void(size_t)> const& emitItem,
                         std::function<size_t(size_t)> const& estimateItem,
                         bool forceOnePerLine = false);

    /// Emits a flattened operator chain (e.g., `|>` pipelines or `@` concat) with adaptive formatting.
    /// Keeps the chain inline if it fits within maxLineWidth; otherwise wraps with the operator
    /// at the start of each continuation line.
    ///
    /// @param chain            Flattened chain of expressions (source first, then stages).
    /// @param node             The original AST node (for width estimation).
    /// @param inlineSeparator  Separator when inline (e.g., " |> ", " @ ").
    /// @param wrappingPrefix   Operator prefix on continuation lines (e.g., "|> ", "@ ").
    void emitChainFormatted(std::vector<ast::Expr const*> const& chain,
                            ast::Node const& node,
                            std::string_view inlineSeparator,
                            std::string_view wrappingPrefix);

    /// Checks whether any element in the expression vector is complex (compound or multiline).
    [[nodiscard]] bool hasComplexElement(std::vector<std::unique_ptr<ast::Expr>> const& elements) const;

    /// Collects a left-associative pipeline chain into a linear sequence.
    /// @return [source, step1, step2, ...] where source is the initial value.
    [[nodiscard]] static std::vector<ast::Expr const*> collectPipelineChain(ast::PipelineExpr const& node);

    /// Collects a right-associative concat chain into a linear sequence.
    /// @return [list1, list2, list3, ...].
    [[nodiscard]] static std::vector<ast::Expr const*> collectConcatChain(ast::ConcatListExpr const& node);

    FormatConfig _config;
    std::string _result;
    int _indentLevel = 0;
    bool _atLineStart = true;
    std::string _placeholderParamName;    ///< When non-empty, replace this identifier with `_`
    int _placeholderReplacementCount = 0; ///< Tracks replacements during placeholder emission
    int _placeholderParenDepth = -1;      ///< -1 = not tracking; 0+ = paren nesting depth during replacement
    bool _placeholderUnsafe = false; ///< True if param was found inside nested parens (replacement invalid)
    std::vector<CommentTrivia> const& _comments;
    size_t _nextCommentIndex = 0; ///< Index of next un-emitted comment
    std::set<int> _blankLines;    ///< 0-based line numbers of blank lines in original source

    /// Original source split into lines (0-based), used to reproduce verbatim regions exactly.
    /// Empty when the formatter is invoked without source (AST-only overload) — in that case
    /// the `format off`/`on` feature is inert.
    std::vector<std::string_view> _sourceLines;
    std::vector<VerbatimRegion> _verbatimRegions; ///< Regions to emit verbatim, sorted by line.
};

} // namespace endo::format
