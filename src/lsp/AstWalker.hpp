// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <endo-language/ast/AST.hpp>

namespace endo::lsp
{

/// Controls which AST nodes the visitor is invoked on.
enum class WalkMode
{
    All,          ///< Visit every node (for selection ranges).
    FoldableOnly, ///< Skip pass-through and leaf-only nodes (for folding ranges).
};

template <WalkMode Mode = WalkMode::All, typename Visitor>
void walkExpr(ast::Expr const& expr, Visitor&& visitor);

/// Walks an AST statement tree, calling visitor(node.location) at each node.
/// In FoldableOnly mode, only foldable constructs (LetBinding, ForIn, While, type defs) are visited.
/// @param node    The statement node to walk.
/// @param visitor Callable accepting std::optional<SourceLocationRange> const&.
template <WalkMode Mode = WalkMode::All, typename Visitor>
void walkStatement(ast::Node const& node, Visitor&& visitor)
{
    if constexpr (Mode == WalkMode::All)
        visitor(node.location);

    if (auto const* compound = dynamic_cast<ast::CompoundStmt const*>(&node))
    {
        for (auto const& stmt: compound->statements)
            walkStatement<Mode>(*stmt, visitor);
    }
    else if (auto const* letStmt = dynamic_cast<ast::LetBindingStmt const*>(&node))
    {
        if constexpr (Mode == WalkMode::FoldableOnly)
            visitor(node.location);
        if (letStmt->value)
            walkExpr<Mode>(*letStmt->value, visitor);
        for (auto const& andBind: letStmt->andBindings)
        {
            if (andBind.value)
                walkExpr<Mode>(*andBind.value, visitor);
        }
    }
    else if (auto const* exprStmt = dynamic_cast<ast::ExprStmt const*>(&node))
    {
        walkExpr<Mode>(*exprStmt->expr, visitor);
    }
    else if (auto const* forIn = dynamic_cast<ast::ForInStmt const*>(&node))
    {
        if constexpr (Mode == WalkMode::FoldableOnly)
            visitor(node.location);
        walkExpr<Mode>(*forIn->source, visitor);
        walkStatement<Mode>(*forIn->body, visitor);
    }
    else if (auto const* whileStmt = dynamic_cast<ast::WhileStmt const*>(&node))
    {
        if constexpr (Mode == WalkMode::FoldableOnly)
            visitor(node.location);
        walkExpr<Mode>(*whileStmt->condition, visitor);
        walkStatement<Mode>(*whileStmt->body, visitor);
    }
    else if (dynamic_cast<ast::RecordTypeDefStmt const*>(&node)
             || dynamic_cast<ast::UnionTypeDefStmt const*>(&node))
    {
        if constexpr (Mode == WalkMode::FoldableOnly)
            visitor(node.location);
    }
}

/// Walks an AST expression tree, calling visitor(expr.location) at each node.
/// In FoldableOnly mode, pass-through nodes (Pipeline, Application, Binary, Paren) are skipped.
/// @param expr    The expression node to walk.
/// @param visitor Callable accepting std::optional<SourceLocationRange> const&.
template <WalkMode Mode, typename Visitor>
void walkExpr(ast::Expr const& expr, Visitor&& visitor)
{
    if constexpr (Mode == WalkMode::All)
        visitor(expr.location);

    if (auto const* matchExpr = dynamic_cast<ast::MatchExpr const*>(&expr))
    {
        if constexpr (Mode == WalkMode::FoldableOnly)
            visitor(expr.location);
        walkExpr<Mode>(*matchExpr->scrutinee, visitor);
        for (auto const& arm: matchExpr->arms)
        {
            if (arm.guard)
                walkExpr<Mode>(*arm.guard, visitor);
            walkExpr<Mode>(*arm.body, visitor);
        }
    }
    else if (auto const* ifExpr = dynamic_cast<ast::IfExpr const*>(&expr))
    {
        if constexpr (Mode == WalkMode::FoldableOnly)
            visitor(expr.location);
        walkExpr<Mode>(*ifExpr->condition, visitor);
        walkExpr<Mode>(*ifExpr->thenExpr, visitor);
        if (ifExpr->elseExpr)
            walkExpr<Mode>(*ifExpr->elseExpr, visitor);
    }
    else if (auto const* block = dynamic_cast<ast::BlockExpr const*>(&expr))
    {
        if constexpr (Mode == WalkMode::FoldableOnly)
            visitor(expr.location);
        for (auto const& stmt: block->statements)
            walkStatement<Mode>(*stmt, visitor);
        if (block->result)
            walkExpr<Mode>(*block->result, visitor);
    }
    else if (auto const* tryWith = dynamic_cast<ast::TryWithExpr const*>(&expr))
    {
        if constexpr (Mode == WalkMode::FoldableOnly)
            visitor(expr.location);
        walkExpr<Mode>(*tryWith->body, visitor);
        for (auto const& handler: tryWith->handlers)
        {
            if (handler.guard)
                walkExpr<Mode>(*handler.guard, visitor);
            walkExpr<Mode>(*handler.body, visitor);
        }
    }
    else if (auto const* tryFinally = dynamic_cast<ast::TryFinallyExpr const*>(&expr))
    {
        if constexpr (Mode == WalkMode::FoldableOnly)
            visitor(expr.location);
        walkExpr<Mode>(*tryFinally->body, visitor);
        walkExpr<Mode>(*tryFinally->finallyExpr, visitor);
    }
    else if (auto const* lambda = dynamic_cast<ast::LambdaExpr const*>(&expr))
    {
        if constexpr (Mode == WalkMode::FoldableOnly)
            visitor(expr.location);
        if (lambda->body)
            walkExpr<Mode>(*lambda->body, visitor);
    }
    else if (auto const* list = dynamic_cast<ast::ListExpr const*>(&expr))
    {
        if constexpr (Mode == WalkMode::FoldableOnly)
            visitor(expr.location);
        for (auto const& elem: list->elements)
            walkExpr<Mode>(*elem, visitor);
    }
    else if (auto const* letIn = dynamic_cast<ast::LetInExpr const*>(&expr))
    {
        if constexpr (Mode == WalkMode::FoldableOnly)
            visitor(expr.location);
        if (letIn->value)
            walkExpr<Mode>(*letIn->value, visitor);
        if (letIn->body)
            walkExpr<Mode>(*letIn->body, visitor);
    }
    else if (auto const* pipe = dynamic_cast<ast::PipelineExpr const*>(&expr))
    {
        walkExpr<Mode>(*pipe->value, visitor);
        walkExpr<Mode>(*pipe->function, visitor);
    }
    else if (auto const* app = dynamic_cast<ast::ApplicationExpr const*>(&expr))
    {
        walkExpr<Mode>(*app->function, visitor);
        walkExpr<Mode>(*app->argument, visitor);
    }
    else if (auto const* bin = dynamic_cast<ast::BinaryExpr const*>(&expr))
    {
        walkExpr<Mode>(*bin->left, visitor);
        walkExpr<Mode>(*bin->right, visitor);
    }
    else if (auto const* paren = dynamic_cast<ast::ParenExpr const*>(&expr))
    {
        walkExpr<Mode>(*paren->inner, visitor);
    }
    else if (dynamic_cast<ast::SeqExpr const*>(&expr) || dynamic_cast<ast::RecordExpr const*>(&expr))
    {
        if constexpr (Mode == WalkMode::FoldableOnly)
            visitor(expr.location);
    }
}

} // namespace endo::lsp
