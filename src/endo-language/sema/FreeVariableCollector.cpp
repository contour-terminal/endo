// SPDX-License-Identifier: Apache-2.0
#include <endo-language/ast/Pattern.hpp>
#include <endo-language/sema/FreeVariableCollector.hpp>

#include <algorithm>
#include <functional>

namespace endo
{

std::unordered_set<std::string> collectFreeVariableNames(ast::Expr const* body,
                                                         std::vector<std::string> const& boundNames,
                                                         ScopeQuery const& isInScope,
                                                         FunctionQuery const& isKnownFunction,
                                                         std::unordered_set<std::string>* referencedFunctions)
{
    std::unordered_set<std::string> freeVars;

    // Recursive walker as a lambda (avoids needing a full Visitor subclass)
    std::function<void(ast::Expr const*, std::vector<std::string> const&)> walk =
        [&](ast::Expr const* expr, std::vector<std::string> const& bound) {
            if (!expr)
                return;

            if (auto const* ident = dynamic_cast<ast::IdentifierExpr const*>(expr))
            {
                // Check if the identifier is already bound (parameter or locally-scoped)
                if (std::ranges::find(bound, ident->name) != bound.end())
                    return;
                // Check if it's a registered function name
                if (isKnownFunction(ident->name))
                {
                    // Record the reference so callers can pull in this function's own captures
                    // transitively (needed to forward them at the call site).
                    if (referencedFunctions)
                        referencedFunctions->insert(ident->name);
                    return;
                }
                // Check if it's accessible in the current variable scope
                if (isInScope(ident->name))
                    freeVars.insert(ident->name);
                return;
            }

            if (auto const* bin = dynamic_cast<ast::BinaryExpr const*>(expr))
            {
                walk(bin->left.get(), bound);
                walk(bin->right.get(), bound);
                return;
            }

            if (auto const* unary = dynamic_cast<ast::UnaryExpr const*>(expr))
            {
                walk(unary->operand.get(), bound);
                return;
            }

            if (auto const* paren = dynamic_cast<ast::ParenExpr const*>(expr))
            {
                walk(paren->inner.get(), bound);
                return;
            }

            if (auto const* app = dynamic_cast<ast::ApplicationExpr const*>(expr))
            {
                walk(app->function.get(), bound);
                walk(app->argument.get(), bound);
                return;
            }

            if (auto const* pipe = dynamic_cast<ast::PipelineExpr const*>(expr))
            {
                walk(pipe->value.get(), bound);
                walk(pipe->function.get(), bound);
                return;
            }

            if (auto const* lambda = dynamic_cast<ast::LambdaExpr const*>(expr))
            {
                // Lambda parameters shadow outer bindings within the lambda body
                auto innerBound = bound;
                auto const names = ast::extractParameterNames(lambda->parameters);
                innerBound.insert(innerBound.end(), names.begin(), names.end());
                walk(lambda->body.get(), innerBound);
                return;
            }

            if (auto const* match = dynamic_cast<ast::MatchExpr const*>(expr))
            {
                walk(match->scrutinee.get(), bound);
                for (auto const& arm: match->arms)
                {
                    // Pattern bindings shadow outer names within the arm body and guard
                    auto armBound = bound;
                    auto bindings = pattern::collectBindings(*arm.pattern);
                    armBound.insert(armBound.end(), bindings.begin(), bindings.end());
                    if (arm.guard)
                        walk(arm.guard.get(), armBound);
                    walk(arm.body.get(), armBound);
                }
                return;
            }

            if (auto const* opt = dynamic_cast<ast::OptionExpr const*>(expr))
            {
                if (opt->value)
                    walk(opt->value.get(), bound);
                return;
            }

            if (auto const* res = dynamic_cast<ast::ResultExpr const*>(expr))
            {
                if (res->payload)
                    walk(res->payload.get(), bound);
                return;
            }

            if (auto const* tryExpr = dynamic_cast<ast::TryExpr const*>(expr))
            {
                walk(tryExpr->operand.get(), bound);
                return;
            }

            if (auto const* lazyExpr = dynamic_cast<ast::LazyExpr const*>(expr))
            {
                walk(lazyExpr->body.get(), bound);
                return;
            }

            if (auto const* refExpr = dynamic_cast<ast::RefExpr const*>(expr))
            {
                walk(refExpr->value.get(), bound);
                return;
            }

            if (auto const* seqExpr = dynamic_cast<ast::SeqExpr const*>(expr))
            {
                for (auto const& yield: seqExpr->yields)
                    walk(yield.value.get(), bound);
                return;
            }

            if (auto const* optDefault = dynamic_cast<ast::OptionDefaultExpr const*>(expr))
            {
                walk(optDefault->option.get(), bound);
                walk(optDefault->defaultValue.get(), bound);
                return;
            }

            if (auto const* tryWith = dynamic_cast<ast::TryWithExpr const*>(expr))
            {
                walk(tryWith->body.get(), bound);
                for (auto const& handler: tryWith->handlers)
                {
                    auto handlerBound = bound;
                    auto bindings = pattern::collectBindings(*handler.pattern);
                    handlerBound.insert(handlerBound.end(), bindings.begin(), bindings.end());
                    if (handler.guard)
                        walk(handler.guard.get(), handlerBound);
                    walk(handler.body.get(), handlerBound);
                }
                return;
            }

            if (auto const* list = dynamic_cast<ast::ListExpr const*>(expr))
            {
                for (auto const& elem: list->elements)
                    walk(elem.get(), bound);
                return;
            }

            if (auto const* range = dynamic_cast<ast::ListRangeExpr const*>(expr))
            {
                walk(range->start.get(), bound);
                if (range->step)
                    walk(range->step.get(), bound);
                walk(range->end.get(), bound);
                return;
            }

            if (auto const* comp = dynamic_cast<ast::ListComprehensionExpr const*>(expr))
            {
                walk(comp->source.get(), bound);
                // The iteration variable is bound within filter and body
                auto innerBound = bound;
                innerBound.push_back(comp->variable);
                if (comp->filter)
                    walk(comp->filter.get(), innerBound);
                walk(comp->body.get(), innerBound);
                return;
            }

            if (auto const* ifExpr = dynamic_cast<ast::IfExpr const*>(expr))
            {
                walk(ifExpr->condition.get(), bound);
                walk(ifExpr->thenExpr.get(), bound);
                if (ifExpr->elseExpr)
                    walk(ifExpr->elseExpr.get(), bound);
                return;
            }

            if (auto const* tupleExpr = dynamic_cast<ast::TupleExpr const*>(expr))
            {
                for (auto const& elem: tupleExpr->elements)
                    walk(elem.get(), bound);
                return;
            }

            if (auto const* mutExpr = dynamic_cast<ast::MutAssignExpr const*>(expr))
            {
                // The assigned variable itself is a free variable reference
                if (std::ranges::find(bound, mutExpr->name) == bound.end())
                    if (isInScope(mutExpr->name))
                        freeVars.insert(mutExpr->name);
                walk(mutExpr->value.get(), bound);
                return;
            }

            if (auto const* block = dynamic_cast<ast::BlockExpr const*>(expr))
            {
                auto innerBound = bound;
                for (auto const& stmt: block->statements)
                {
                    if (auto const* letStmt = dynamic_cast<ast::LetBindingStmt const*>(stmt.get()))
                    {
                        walk(letStmt->value.get(), innerBound);
                        innerBound.push_back(letStmt->name);
                    }
                    else if (auto const* exprStmt = dynamic_cast<ast::ExprStmt const*>(stmt.get()))
                    {
                        walk(exprStmt->expr.get(), innerBound);
                    }
                }
                walk(block->result.get(), innerBound);
                return;
            }

            if (auto const* compose = dynamic_cast<ast::CompositionExpr const*>(expr))
            {
                walk(compose->left.get(), bound);
                walk(compose->right.get(), bound);
                return;
            }

            if (auto const* placeholder = dynamic_cast<ast::PlaceholderLambdaExpr const*>(expr))
            {
                // __x is the placeholder parameter — bound within the body
                auto innerBound = bound;
                innerBound.emplace_back(ast::PlaceholderParamName);
                walk(placeholder->body.get(), innerBound);
                return;
            }

            // Interpolated / concatenated strings: `$"...{x}..."` and adjacent-string
            // concatenation. A variable referenced only inside one of these parts is still
            // a free variable of an enclosing closure; failing to detect it here means the
            // closure never captures it, and the body then loads a slot from a different
            // function's frame at runtime (an out-of-bounds LOAD).
            if (auto const* fstr = dynamic_cast<ast::FStringExpr const*>(expr))
            {
                for (auto const& part: fstr->parts)
                    walk(part.get(), bound);
                return;
            }

            if (auto const* concat = dynamic_cast<ast::ConcatExpr const*>(expr))
            {
                for (auto const& part: concat->parts)
                    walk(part.get(), bound);
                return;
            }

            if (auto const* cons = dynamic_cast<ast::ConsExpr const*>(expr))
            {
                walk(cons->head.get(), bound);
                walk(cons->tail.get(), bound);
                return;
            }

            if (auto const* concatList = dynamic_cast<ast::ConcatListExpr const*>(expr))
            {
                walk(concatList->left.get(), bound);
                walk(concatList->right.get(), bound);
                return;
            }

            if (auto const* unionCtor = dynamic_cast<ast::UnionConstructorExpr const*>(expr))
            {
                for (auto const& arg: unionCtor->arguments)
                    walk(arg.get(), bound);
                return;
            }

            if (auto const* record = dynamic_cast<ast::RecordExpr const*>(expr))
            {
                for (auto const& field: record->fields)
                    walk(field.value.get(), bound);
                return;
            }

            if (auto const* recordUpdate = dynamic_cast<ast::RecordUpdateExpr const*>(expr))
            {
                walk(recordUpdate->base.get(), bound);
                for (auto const& field: recordUpdate->updates)
                    walk(field.value.get(), bound);
                return;
            }

            if (auto const* fieldAccess = dynamic_cast<ast::FieldAccessExpr const*>(expr))
            {
                walk(fieldAccess->object.get(), bound);
                return;
            }

            if (auto const* optChain = dynamic_cast<ast::OptionalChainExpr const*>(expr))
            {
                walk(optChain->object.get(), bound);
                return;
            }

            if (auto const* tryFinally = dynamic_cast<ast::TryFinallyExpr const*>(expr))
            {
                walk(tryFinally->body.get(), bound);
                walk(tryFinally->finallyExpr.get(), bound);
                return;
            }

            if (auto const* exec = dynamic_cast<ast::ExecPipelineExpr const*>(expr))
            {
                for (auto const& cmd: exec->commands)
                {
                    walk(cmd.program.get(), bound);
                    for (auto const& arg: cmd.arguments)
                        walk(arg.get(), bound);
                }
                return;
            }

            if (auto const* splat = dynamic_cast<ast::SplatExpr const*>(expr))
            {
                // `...name` splats a variable into shell arguments; the name itself is a
                // free variable reference when not locally bound.
                if (std::ranges::find(bound, splat->name) == bound.end() && isInScope(splat->name))
                    freeVars.insert(splat->name);
                return;
            }

            if (auto const* letIn = dynamic_cast<ast::LetInExpr const*>(expr))
            {
                // `let [rec] name [params] = value in body`. The binding name is in scope for
                // the value only when recursive; function parameters are in scope for the
                // value; the binding (or destructured names) is always in scope for the body.
                auto valueBound = bound;
                if (letIn->isRecursive())
                    valueBound.push_back(letIn->name);
                auto const paramNames = ast::extractParameterNames(letIn->parameters);
                valueBound.insert(valueBound.end(), paramNames.begin(), paramNames.end());
                walk(letIn->value.get(), valueBound);

                auto bodyBound = bound;
                if (letIn->isDestructuring())
                {
                    auto bindings = pattern::collectBindings(*letIn->destructurePattern);
                    bodyBound.insert(bodyBound.end(), bindings.begin(), bindings.end());
                }
                else
                {
                    bodyBound.push_back(letIn->name);
                }
                walk(letIn->body.get(), bodyBound);
                return;
            }

            // Literal types (IntLiteralExpr, FloatLiteralExpr, BoolLiteralExpr) have no free variables.
            // ShellCommandExpr has no F# free variables.
        };

    walk(body, boundNames);
    return freeVars;
}

} // namespace endo
