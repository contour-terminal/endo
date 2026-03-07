// SPDX-License-Identifier: Apache-2.0
#include "SelectionRangeProvider.hpp"

#include <endo-language/ast/AST.hpp>
#include <endo-language/parser/Parser.hpp>

#include <editor-protocol/StubRuntime.hpp>

#include <algorithm>
#include <ranges>
#include <string>
#include <vector>

namespace endo::lsp
{

namespace
{

    /// Checks if a position is contained within a source location range (inclusive).
    [[nodiscard]] bool containsPosition(SourceLocationRange const& loc, Position pos)
    {
        // Before start?
        if (pos.line < loc.begin.line || (pos.line == loc.begin.line && pos.character < loc.begin.column))
            return false;
        // After end?
        if (pos.line > loc.end.line || (pos.line == loc.end.line && pos.character > loc.end.column))
            return false;
        return true;
    }

    /// Computes the "size" of a range for sorting (smaller = more specific).
    [[nodiscard]] int rangeSize(SourceLocationRange const& loc)
    {
        auto const lines = loc.end.line - loc.begin.line;
        auto const cols = (lines == 0) ? (loc.end.column - loc.begin.column) : loc.end.column;
        return (lines * 10000) + cols;
    }

    /// Collected node range for selection range building.
    struct NodeRange
    {
        SourceLocationRange location;
    };

    /// AST walker that collects all nodes containing a given cursor position.
    class SelectionWalker
    {
      public:
        explicit SelectionWalker(Position cursor): _cursor(cursor) {}

        void walkStatement(ast::Node const& node)
        {
            addIfContains(node.location);

            if (auto const* compound = dynamic_cast<ast::CompoundStmt const*>(&node))
            {
                for (auto const& stmt: compound->statements)
                    walkStatement(*stmt);
            }
            else if (auto const* letStmt = dynamic_cast<ast::LetBindingStmt const*>(&node))
            {
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
                walkExpr(*forIn->source);
                walkStatement(*forIn->body);
            }
            else if (auto const* whileStmt = dynamic_cast<ast::WhileStmt const*>(&node))
            {
                walkExpr(*whileStmt->condition);
                walkStatement(*whileStmt->body);
            }
            else if (auto const* recordDef = dynamic_cast<ast::RecordTypeDefStmt const*>(&node))
            {
                (void) recordDef; // leaf node
            }
            else if (auto const* unionDef = dynamic_cast<ast::UnionTypeDefStmt const*>(&node))
            {
                (void) unionDef; // leaf node
            }
        }

        void walkExpr(ast::Expr const& expr)
        {
            addIfContains(expr.location);

            if (auto const* matchExpr = dynamic_cast<ast::MatchExpr const*>(&expr))
            {
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
                walkExpr(*ifExpr->condition);
                walkExpr(*ifExpr->thenExpr);
                if (ifExpr->elseExpr)
                    walkExpr(*ifExpr->elseExpr);
            }
            else if (auto const* block = dynamic_cast<ast::BlockExpr const*>(&expr))
            {
                for (auto const& stmt: block->statements)
                    walkStatement(*stmt);
                if (block->result)
                    walkExpr(*block->result);
            }
            else if (auto const* tryWith = dynamic_cast<ast::TryWithExpr const*>(&expr))
            {
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
                walkExpr(*tryFinally->body);
                walkExpr(*tryFinally->finallyExpr);
            }
            else if (auto const* lambda = dynamic_cast<ast::LambdaExpr const*>(&expr))
            {
                if (lambda->body)
                    walkExpr(*lambda->body);
            }
            else if (auto const* list = dynamic_cast<ast::ListExpr const*>(&expr))
            {
                for (auto const& elem: list->elements)
                    walkExpr(*elem);
            }
            else if (auto const* letIn = dynamic_cast<ast::LetInExpr const*>(&expr))
            {
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
            else if (auto const* record = dynamic_cast<ast::RecordExpr const*>(&expr))
            {
                (void) record; // leaf node
            }
        }

        [[nodiscard]] std::vector<NodeRange> const& ranges() const { return _ranges; }

      private:
        void addIfContains(std::optional<SourceLocationRange> const& loc)
        {
            if (loc && containsPosition(*loc, _cursor))
                _ranges.push_back(NodeRange { .location = *loc });
        }

        Position _cursor;
        std::vector<NodeRange> _ranges;
    };

    /// Builds a linked SelectionRange chain from a sorted (smallest-first) vector of ranges.
    [[nodiscard]] SelectionRange buildChain(std::vector<NodeRange> const& sorted)
    {
        if (sorted.empty())
            return SelectionRange { .range = Range {}, .parent = nullptr };

        // Start from the outermost (last) and build inward
        std::unique_ptr<SelectionRange> current;
        for (auto const& entry: std::ranges::reverse_view(sorted))
        {
            auto node = std::make_unique<SelectionRange>();
            node->range = toRange(entry.location);
            node->parent = std::move(current);
            current = std::move(node);
        }

        // Move the innermost out of the unique_ptr
        auto result = std::move(*current);
        return result;
    }

} // namespace

std::vector<SelectionRange> computeSelectionRanges(std::string const& source,
                                                   std::vector<Position> const& positions)
{
    // Parse source into AST
    CoreVM::Runtime runtime;
    endo::registerStubRuntime(runtime);

    CoreVM::diagnostics::BufferedReport report;
    Parser parser(runtime, report, std::make_unique<StringSource>(source));
    auto astRoot = parser.parse();
    if (!astRoot)
        return {};

    std::vector<SelectionRange> results;
    results.reserve(positions.size());

    for (auto const& cursor: positions)
    {
        SelectionWalker walker(cursor);
        walker.walkStatement(*astRoot);

        auto ranges = walker.ranges();

        // Sort by range size (smallest first), deduplicate identical ranges
        std::ranges::sort(ranges, [](auto const& a, auto const& b) {
            return rangeSize(a.location) < rangeSize(b.location);
        });

        // Deduplicate identical ranges
        auto last = std::ranges::unique(ranges, [](auto const& a, auto const& b) {
            return a.location.begin.line == b.location.begin.line
                   && a.location.begin.column == b.location.begin.column
                   && a.location.end.line == b.location.end.line
                   && a.location.end.column == b.location.end.column;
        });
        ranges.erase(last.begin(), last.end());

        results.push_back(buildChain(ranges));
    }

    return results;
}

} // namespace endo::lsp
