// SPDX-License-Identifier: Apache-2.0
#include "FoldingRangeProvider.hpp"

#include <endo-language/ast/AST.hpp>
#include <endo-language/lexer/Lexer.hpp>
#include <endo-language/parser/Parser.hpp>

#include <editor-protocol/StubRuntime.hpp>

#include <string>
#include <vector>

namespace endo::lsp
{

namespace
{

    /// Checks if a source location range spans multiple lines.
    [[nodiscard]] bool isMultiLine(SourceLocationRange const& loc)
    {
        return loc.begin.line < loc.end.line;
    }

    /// Creates a FoldingRange from a SourceLocationRange with the given kind.
    [[nodiscard]] FoldingRange makeFold(SourceLocationRange const& loc,
                                        std::optional<std::string> kind = "region")
    {
        return FoldingRange {
            .startLine = loc.begin.line,
            .startCharacter = loc.begin.column,
            .endLine = loc.end.line,
            .endCharacter = loc.end.column,
            .kind = std::move(kind),
        };
    }

    /// AST walker that collects folding ranges from multi-line constructs.
    class FoldingRangeWalker
    {
      public:
        void walkStatement(ast::Node const& node)
        {
            if (auto const* compound = dynamic_cast<ast::CompoundStmt const*>(&node))
            {
                for (auto const& stmt: compound->statements)
                    walkStatement(*stmt);
            }
            else if (auto const* letStmt = dynamic_cast<ast::LetBindingStmt const*>(&node))
            {
                addIfMultiLine(letStmt->location);
                if (letStmt->value)
                    walkExpr(*letStmt->value);
                for (auto const& andBind: letStmt->andBindings)
                {
                    if (andBind.value)
                        walkExpr(*andBind.value);
                }
            }
            else if (auto const* exprStmt = dynamic_cast<ast::ExprStmt const*>(&node))
            {
                walkExpr(*exprStmt->expr);
            }
            else if (auto const* forIn = dynamic_cast<ast::ForInStmt const*>(&node))
            {
                addIfMultiLine(forIn->location);
                walkExpr(*forIn->source);
                walkStatement(*forIn->body);
            }
            else if (auto const* whileStmt = dynamic_cast<ast::WhileStmt const*>(&node))
            {
                addIfMultiLine(whileStmt->location);
                walkExpr(*whileStmt->condition);
                walkStatement(*whileStmt->body);
            }
            else if (auto const* recordDef = dynamic_cast<ast::RecordTypeDefStmt const*>(&node))
            {
                addIfMultiLine(recordDef->location);
            }
            else if (auto const* unionDef = dynamic_cast<ast::UnionTypeDefStmt const*>(&node))
            {
                addIfMultiLine(unionDef->location);
            }
        }

        void walkExpr(ast::Expr const& expr)
        {
            if (auto const* matchExpr = dynamic_cast<ast::MatchExpr const*>(&expr))
            {
                addIfMultiLine(matchExpr->location);
                walkExpr(*matchExpr->scrutinee);
                for (auto const& arm: matchExpr->arms)
                {
                    if (arm.guard)
                        walkExpr(*arm.guard);
                    walkExpr(*arm.body);
                }
            }
            else if (auto const* ifExpr = dynamic_cast<ast::IfExpr const*>(&expr))
            {
                addIfMultiLine(ifExpr->location);
                walkExpr(*ifExpr->condition);
                walkExpr(*ifExpr->thenExpr);
                if (ifExpr->elseExpr)
                    walkExpr(*ifExpr->elseExpr);
            }
            else if (auto const* block = dynamic_cast<ast::BlockExpr const*>(&expr))
            {
                addIfMultiLine(block->location);
                for (auto const& stmt: block->statements)
                    walkStatement(*stmt);
                if (block->result)
                    walkExpr(*block->result);
            }
            else if (auto const* tryWith = dynamic_cast<ast::TryWithExpr const*>(&expr))
            {
                addIfMultiLine(tryWith->location);
                walkExpr(*tryWith->body);
                for (auto const& handler: tryWith->handlers)
                {
                    if (handler.guard)
                        walkExpr(*handler.guard);
                    walkExpr(*handler.body);
                }
            }
            else if (auto const* tryFinally = dynamic_cast<ast::TryFinallyExpr const*>(&expr))
            {
                addIfMultiLine(tryFinally->location);
                walkExpr(*tryFinally->body);
                walkExpr(*tryFinally->finallyExpr);
            }
            else if (auto const* lambda = dynamic_cast<ast::LambdaExpr const*>(&expr))
            {
                addIfMultiLine(lambda->location);
                if (lambda->body)
                    walkExpr(*lambda->body);
            }
            else if (auto const* list = dynamic_cast<ast::ListExpr const*>(&expr))
            {
                addIfMultiLine(list->location);
                for (auto const& elem: list->elements)
                    walkExpr(*elem);
            }
            else if (auto const* seq = dynamic_cast<ast::SeqExpr const*>(&expr))
            {
                addIfMultiLine(seq->location);
            }
            else if (auto const* record = dynamic_cast<ast::RecordExpr const*>(&expr))
            {
                addIfMultiLine(record->location);
            }
            else if (auto const* letIn = dynamic_cast<ast::LetInExpr const*>(&expr))
            {
                addIfMultiLine(letIn->location);
                if (letIn->value)
                    walkExpr(*letIn->value);
                if (letIn->body)
                    walkExpr(*letIn->body);
            }
            else if (auto const* pipe = dynamic_cast<ast::PipelineExpr const*>(&expr))
            {
                walkExpr(*pipe->value);
                walkExpr(*pipe->function);
            }
            else if (auto const* app = dynamic_cast<ast::ApplicationExpr const*>(&expr))
            {
                walkExpr(*app->function);
                walkExpr(*app->argument);
            }
            else if (auto const* bin = dynamic_cast<ast::BinaryExpr const*>(&expr))
            {
                walkExpr(*bin->left);
                walkExpr(*bin->right);
            }
            else if (auto const* paren = dynamic_cast<ast::ParenExpr const*>(&expr))
            {
                walkExpr(*paren->inner);
            }
        }

        [[nodiscard]] std::vector<FoldingRange> const& ranges() const { return _ranges; }

      private:
        void addIfMultiLine(std::optional<SourceLocationRange> const& loc)
        {
            if (loc && isMultiLine(*loc))
                _ranges.push_back(makeFold(*loc));
        }

        std::vector<FoldingRange> _ranges;
    };

} // namespace

std::vector<FoldingRange> computeFoldingRanges(std::string const& source)
{
    // Parse source into AST
    CoreVM::Runtime runtime;
    endo::registerStubRuntime(runtime);

    CoreVM::diagnostics::BufferedReport report;
    Parser parser(runtime, report, std::make_unique<StringSource>(source));
    auto astRoot = parser.parse();
    if (!astRoot)
        return {};

    // Walk AST to collect folding ranges
    FoldingRangeWalker walker;
    walker.walkStatement(*astRoot);
    auto ranges = walker.ranges();

    // Collect multi-line comments
    auto lexer = Lexer { std::make_unique<StringSource>(source), true };
    while (lexer.currentToken() != Token::EndOfInput)
        lexer.nextToken();

    for (auto const& comment: lexer.comments())
    {
        if (isMultiLine(comment.location))
            ranges.push_back(makeFold(comment.location, "comment"));
    }

    return ranges;
}

} // namespace endo::lsp
