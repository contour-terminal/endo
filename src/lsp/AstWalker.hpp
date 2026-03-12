// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <endo-language/ast/AST.hpp>

namespace endo::lsp
{

/// Walks an AST statement tree, calling visitor(node.location) at each node.
/// Recurses into all child statements and expressions.
/// @param node    The statement node to walk.
/// @param visitor Callable accepting std::optional<SourceLocationRange> const&.
template <typename Visitor>
void walkStatement(ast::Node const& node, Visitor&& visitor)
{
    visitor(node.location);

    if (auto const* compound = dynamic_cast<ast::CompoundStmt const*>(&node))
    {
        for (auto const& stmt: compound->statements)
            walkStatement(*stmt, visitor);
    }
    else if (auto const* letStmt = dynamic_cast<ast::LetBindingStmt const*>(&node))
    {
        if (letStmt->value)
            walkExpr(*letStmt->value, visitor);
        for (auto const& andBind: letStmt->andBindings)
        {
            if (andBind.value)
                walkExpr(*andBind.value, visitor);
        }
    }
    else if (auto const* exprStmt = dynamic_cast<ast::ExprStmt const*>(&node))
    {
        walkExpr(*exprStmt->expr, visitor);
    }
    else if (auto const* forIn = dynamic_cast<ast::ForInStmt const*>(&node))
    {
        walkExpr(*forIn->source, visitor);
        walkStatement(*forIn->body, visitor);
    }
    else if (auto const* whileStmt = dynamic_cast<ast::WhileStmt const*>(&node))
    {
        walkExpr(*whileStmt->condition, visitor);
        walkStatement(*whileStmt->body, visitor);
    }
    // RecordTypeDefStmt and UnionTypeDefStmt are leaf nodes (visitor already called above).
}

/// Walks an AST expression tree, calling visitor(expr.location) at each node.
/// Recurses into all child expressions and statements.
/// @param expr    The expression node to walk.
/// @param visitor Callable accepting std::optional<SourceLocationRange> const&.
template <typename Visitor>
void walkExpr(ast::Expr const& expr, Visitor&& visitor)
{
    visitor(expr.location);

    if (auto const* matchExpr = dynamic_cast<ast::MatchExpr const*>(&expr))
    {
        walkExpr(*matchExpr->scrutinee, visitor);
        for (auto const& arm: matchExpr->arms)
        {
            if (arm.guard)
                walkExpr(*arm.guard, visitor);
            walkExpr(*arm.body, visitor);
        }
    }
    else if (auto const* ifExpr = dynamic_cast<ast::IfExpr const*>(&expr))
    {
        walkExpr(*ifExpr->condition, visitor);
        walkExpr(*ifExpr->thenExpr, visitor);
        if (ifExpr->elseExpr)
            walkExpr(*ifExpr->elseExpr, visitor);
    }
    else if (auto const* block = dynamic_cast<ast::BlockExpr const*>(&expr))
    {
        for (auto const& stmt: block->statements)
            walkStatement(*stmt, visitor);
        if (block->result)
            walkExpr(*block->result, visitor);
    }
    else if (auto const* tryWith = dynamic_cast<ast::TryWithExpr const*>(&expr))
    {
        walkExpr(*tryWith->body, visitor);
        for (auto const& handler: tryWith->handlers)
        {
            if (handler.guard)
                walkExpr(*handler.guard, visitor);
            walkExpr(*handler.body, visitor);
        }
    }
    else if (auto const* tryFinally = dynamic_cast<ast::TryFinallyExpr const*>(&expr))
    {
        walkExpr(*tryFinally->body, visitor);
        walkExpr(*tryFinally->finallyExpr, visitor);
    }
    else if (auto const* lambda = dynamic_cast<ast::LambdaExpr const*>(&expr))
    {
        if (lambda->body)
            walkExpr(*lambda->body, visitor);
    }
    else if (auto const* list = dynamic_cast<ast::ListExpr const*>(&expr))
    {
        for (auto const& elem: list->elements)
            walkExpr(*elem, visitor);
    }
    else if (auto const* letIn = dynamic_cast<ast::LetInExpr const*>(&expr))
    {
        if (letIn->value)
            walkExpr(*letIn->value, visitor);
        if (letIn->body)
            walkExpr(*letIn->body, visitor);
    }
    else if (auto const* pipe = dynamic_cast<ast::PipelineExpr const*>(&expr))
    {
        walkExpr(*pipe->value, visitor);
        walkExpr(*pipe->function, visitor);
    }
    else if (auto const* app = dynamic_cast<ast::ApplicationExpr const*>(&expr))
    {
        walkExpr(*app->function, visitor);
        walkExpr(*app->argument, visitor);
    }
    else if (auto const* bin = dynamic_cast<ast::BinaryExpr const*>(&expr))
    {
        walkExpr(*bin->left, visitor);
        walkExpr(*bin->right, visitor);
    }
    else if (auto const* paren = dynamic_cast<ast::ParenExpr const*>(&expr))
    {
        walkExpr(*paren->inner, visitor);
    }
    // SeqExpr, RecordExpr are leaf nodes (visitor already called above).
}

} // namespace endo::lsp
