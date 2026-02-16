// SPDX-License-Identifier: Apache-2.0
#include "TypeInferencer.hpp"

#include <format>

namespace endo
{

TypeInferencer::TypeInferencer(TypeEnvPtr env): _env(std::move(env))
{
}

InferenceResult TypeInferencer::inferProgram(ast::Statement const& root)
{
    _result = {};
    auto subst = inferStmt(root, _env, {});
    if (!subst)
        recordError(subst.error());
    return std::move(_result);
}

// ============================================================================
// Helpers
// ============================================================================

void TypeInferencer::recordFunction(std::string const& name, InferredFunctionType type)
{
    _result.functions[name] = std::move(type);
}

void TypeInferencer::recordError(std::string error)
{
    _result.errors.push_back(std::move(error));
}

std::expected<Substitution, std::string> TypeInferencer::unifyAndCompose(TypePtr const& t1,
                                                                         TypePtr const& t2,
                                                                         Substitution const& subst)
{
    auto result = unify(subst.apply(t1), subst.apply(t2));
    if (!result)
        return std::unexpected(result.error().message);
    return result->compose(subst);
}

bool TypeInferencer::containsFloatLiteral(ast::Expr const& expr)
{
    if (dynamic_cast<ast::FloatLiteralExpr const*>(&expr))
        return true;
    if (auto const* paren = dynamic_cast<ast::ParenExpr const*>(&expr))
        return containsFloatLiteral(*paren->inner);
    return false;
}

bool TypeInferencer::containsStringLiteral(ast::Expr const& expr)
{
    if (dynamic_cast<ast::LiteralExpr const*>(&expr))
        return true;
    if (dynamic_cast<ast::FStringExpr const*>(&expr))
        return true;
    if (auto const* paren = dynamic_cast<ast::ParenExpr const*>(&expr))
        return containsStringLiteral(*paren->inner);
    return false;
}

// ============================================================================
// Expression Inference (Algorithm W)
// ============================================================================

TypeInferencer::InferResult TypeInferencer::inferExpr(ast::Expr const& expr,
                                                      TypeEnvPtr const& env,
                                                      Substitution subst)
{
    // --- Literals ---
    if (dynamic_cast<ast::IntLiteralExpr const*>(&expr))
        return std::pair { types::intType(), subst };

    if (dynamic_cast<ast::FloatLiteralExpr const*>(&expr))
        return std::pair { types::floatType(), subst };

    if (dynamic_cast<ast::BoolLiteralExpr const*>(&expr))
        return std::pair { types::boolType(), subst };

    if (dynamic_cast<ast::LiteralExpr const*>(&expr))
        return std::pair { types::strType(), subst };

    if (dynamic_cast<ast::FStringExpr const*>(&expr))
        return std::pair { types::strType(), subst };

    if (dynamic_cast<ast::UnitExpr const*>(&expr))
        return std::pair { types::unitType(), subst };

    // --- Identifier ---
    if (auto const* ident = dynamic_cast<ast::IdentifierExpr const*>(&expr))
    {
        auto scheme = env->lookup(ident->name);
        if (!scheme)
            return std::unexpected(std::format("Unbound variable: {}", ident->name));
        auto instantiated = env->instantiate(*scheme);
        return std::pair { instantiated, subst };
    }

    // --- Parenthesized expression ---
    if (auto const* paren = dynamic_cast<ast::ParenExpr const*>(&expr))
        return inferExpr(*paren->inner, env, subst);

    // --- Binary expression ---
    if (auto const* bin = dynamic_cast<ast::BinaryExpr const*>(&expr))
        return inferBinaryOp(bin->op, *bin->left, *bin->right, env, subst);

    // --- Unary expression ---
    if (auto const* unary = dynamic_cast<ast::UnaryExpr const*>(&expr))
    {
        auto operandResult = inferExpr(*unary->operand, env, subst);
        if (!operandResult)
            return operandResult;
        auto [operandType, s1] = *operandResult;

        switch (unary->op)
        {
            case ast::UnaryOp::Neg: {
                // Neg works on int or float; default to int
                if (containsFloatLiteral(*unary->operand))
                {
                    auto s2 = unifyAndCompose(operandType, types::floatType(), s1);
                    if (!s2)
                        return std::unexpected(s2.error());
                    return std::pair { types::floatType(), *s2 };
                }
                auto s2 = unifyAndCompose(operandType, types::intType(), s1);
                if (!s2)
                    return std::unexpected(s2.error());
                return std::pair { types::intType(), *s2 };
            }
            case ast::UnaryOp::Not: {
                auto s2 = unifyAndCompose(operandType, types::boolType(), s1);
                if (!s2)
                    return std::unexpected(s2.error());
                return std::pair { types::boolType(), *s2 };
            }
        }
        return std::unexpected("Unknown unary operator");
    }

    // --- If-then-else ---
    if (auto const* ifExpr = dynamic_cast<ast::IfExpr const*>(&expr))
    {
        auto condResult = inferExpr(*ifExpr->condition, env, subst);
        if (!condResult)
            return condResult;
        auto [condType, s1] = *condResult;

        auto s2 = unifyAndCompose(condType, types::boolType(), s1);
        if (!s2)
            return std::unexpected(s2.error());

        auto thenResult = inferExpr(*ifExpr->thenExpr, env, *s2);
        if (!thenResult)
            return thenResult;
        auto [thenType, s3] = *thenResult;

        if (!ifExpr->elseExpr)
        {
            // No else branch — expression returns unit
            return std::pair { thenType, s3 };
        }

        auto elseResult = inferExpr(*ifExpr->elseExpr, env, s3);
        if (!elseResult)
            return elseResult;
        auto [elseType, s4] = *elseResult;

        auto s5 = unifyAndCompose(thenType, elseType, s4);
        if (!s5)
            return std::unexpected(s5.error());
        return std::pair { s5->apply(thenType), *s5 };
    }

    // --- Lambda expression ---
    if (auto const* lambda = dynamic_cast<ast::LambdaExpr const*>(&expr))
    {
        auto bodyEnv = env->childScope();
        std::vector<TypePtr> paramTypes;

        for (auto const& param: lambda->parameters)
        {
            TypePtr paramType;
            if (param.typeAnnotation)
                paramType = *param.typeAnnotation;
            else
                paramType = bodyEnv->freshTypeVarType();
            paramTypes.push_back(paramType);
            bodyEnv->bindMono(param.name, paramType);
        }

        auto bodyResult = inferExpr(*lambda->body, bodyEnv, subst);
        if (!bodyResult)
            return bodyResult;
        auto [bodyType, s1] = *bodyResult;

        // Build curried function type: p1 -> p2 -> ... -> bodyType
        auto resultType = s1.apply(bodyType);
        for (auto it = paramTypes.rbegin(); it != paramTypes.rend(); ++it)
            resultType = types::function(s1.apply(*it), resultType);

        return std::pair { resultType, s1 };
    }

    // --- Function application ---
    if (auto const* app = dynamic_cast<ast::ApplicationExpr const*>(&expr))
    {
        auto funcResult = inferExpr(*app->function, env, subst);
        if (!funcResult)
            return funcResult;
        auto [funcType, s1] = *funcResult;

        auto argResult = inferExpr(*app->argument, env, s1);
        if (!argResult)
            return argResult;
        auto [argType, s2] = *argResult;

        auto resultVar = env->freshTypeVarType();
        auto expectedFuncType = types::function(argType, resultVar);

        auto s3 = unifyAndCompose(funcType, expectedFuncType, s2);
        if (!s3)
            return std::unexpected(s3.error());
        return std::pair { s3->apply(resultVar), *s3 };
    }

    // --- Pipeline expression: value |> func  ≡  func value ---
    if (auto const* pipe = dynamic_cast<ast::PipelineExpr const*>(&expr))
    {
        auto valResult = inferExpr(*pipe->value, env, subst);
        if (!valResult)
            return valResult;
        auto [valType, s1] = *valResult;

        auto funcResult = inferExpr(*pipe->function, env, s1);
        if (!funcResult)
            return funcResult;
        auto [funcType, s2] = *funcResult;

        auto resultVar = env->freshTypeVarType();
        auto expectedFuncType = types::function(valType, resultVar);

        auto s3 = unifyAndCompose(funcType, expectedFuncType, s2);
        if (!s3)
            return std::unexpected(s3.error());
        return std::pair { s3->apply(resultVar), *s3 };
    }

    // --- Let-in expression ---
    if (auto const* letIn = dynamic_cast<ast::LetInExpr const*>(&expr))
    {
        if (letIn->isFunction())
        {
            // Function definition in let-in
            auto innerEnv = env->childScope();
            std::vector<TypePtr> paramTypes;

            if (letIn->isRecursive)
            {
                // Pre-bind with fresh type for recursive reference
                auto freshParams = std::vector<TypePtr> {};
                for (auto const& param: letIn->parameters)
                {
                    auto t = param.typeAnnotation ? *param.typeAnnotation : innerEnv->freshTypeVarType();
                    freshParams.push_back(t);
                }
                auto freshRet = innerEnv->freshTypeVarType();
                auto funcType = freshRet;
                for (auto it = freshParams.rbegin(); it != freshParams.rend(); ++it)
                    funcType = types::function(*it, funcType);
                innerEnv->bindMono(letIn->name, funcType);
                paramTypes = freshParams;
            }

            auto bodyEnv = innerEnv->childScope();
            if (!letIn->isRecursive)
            {
                for (auto const& param: letIn->parameters)
                {
                    auto t = param.typeAnnotation ? *param.typeAnnotation : bodyEnv->freshTypeVarType();
                    paramTypes.push_back(t);
                }
            }

            for (size_t i = 0; i < letIn->parameters.size(); ++i)
                bodyEnv->bindMono(letIn->parameters[i].name, paramTypes[i]);

            auto valResult = inferExpr(*letIn->value, bodyEnv, subst);
            if (!valResult)
                return valResult;
            auto [valType, s1] = *valResult;

            // Build function type
            auto retType = s1.apply(valType);
            auto funcType = retType;
            for (auto it = paramTypes.rbegin(); it != paramTypes.rend(); ++it)
                funcType = types::function(s1.apply(*it), funcType);

            if (letIn->isRecursive)
            {
                // Unify with pre-bound type
                auto scheme = innerEnv->lookup(letIn->name);
                if (scheme)
                {
                    auto preBound = innerEnv->instantiate(*scheme);
                    auto s2 = unifyAndCompose(funcType, preBound, s1);
                    if (!s2)
                        return std::unexpected(s2.error());
                    s1 = *s2;
                    funcType = s1.apply(funcType);
                }
            }

            // Generalize and bind in outer scope for the body
            auto outerEnv = env->childScope();
            auto generalizedScheme = outerEnv->generalize(s1.apply(funcType));
            outerEnv->bind(letIn->name, generalizedScheme);

            // Record inferred function type
            InferredFunctionType inferred;
            for (auto const& pt: paramTypes)
                inferred.paramTypes.push_back(s1.apply(pt));
            inferred.returnType = s1.apply(valType);
            recordFunction(letIn->name, std::move(inferred));

            // Infer body expression
            auto bodyResult = inferExpr(*letIn->body, outerEnv, s1);
            if (!bodyResult)
                return bodyResult;
            return bodyResult;
        }
        else if (letIn->isDestructuring())
        {
            // Destructuring let-in: let (x, y) = expr in body
            auto valResult = inferExpr(*letIn->value, env, subst);
            if (!valResult)
                return valResult;
            auto [valType, s1] = *valResult;

            auto patResult = inferPattern(*letIn->destructurePattern, env, s1);
            if (!patResult)
                return std::unexpected(patResult.error());

            auto s2 = unifyAndCompose(valType, patResult->type, patResult->subst);
            if (!s2)
                return std::unexpected(s2.error());

            auto bodyEnv = env->childScope();
            for (auto const& [name, type]: patResult->bindings)
                bodyEnv->bindMono(name, s2->apply(type));

            return inferExpr(*letIn->body, bodyEnv, *s2);
        }
        else
        {
            // Simple value binding
            auto valResult = inferExpr(*letIn->value, env, subst);
            if (!valResult)
                return valResult;
            auto [valType, s1] = *valResult;

            auto bodyEnv = env->childScope();
            auto scheme = bodyEnv->generalize(s1.apply(valType));
            bodyEnv->bind(letIn->name, scheme);

            return inferExpr(*letIn->body, bodyEnv, s1);
        }
    }

    // --- Match expression ---
    if (auto const* matchExpr = dynamic_cast<ast::MatchExpr const*>(&expr))
    {
        auto scrutResult = inferExpr(*matchExpr->scrutinee, env, subst);
        if (!scrutResult)
            return scrutResult;
        auto [scrutType, s1] = *scrutResult;

        auto resultTypeVar = env->freshTypeVarType();
        auto currentSubst = s1;

        for (auto const& arm: matchExpr->arms)
        {
            auto patResult = inferPattern(*arm.pattern, env, currentSubst);
            if (!patResult)
                return std::unexpected(patResult.error());

            auto s2 = unifyAndCompose(scrutType, patResult->type, patResult->subst);
            if (!s2)
                return std::unexpected(s2.error());

            auto armEnv = env->childScope();
            for (auto const& [name, type]: patResult->bindings)
                armEnv->bindMono(name, s2->apply(type));

            // Check guard if present
            if (arm.guard)
            {
                auto guardResult = inferExpr(*arm.guard, armEnv, *s2);
                if (!guardResult)
                    return guardResult;
                auto [guardType, s3] = *guardResult;
                auto s4 = unifyAndCompose(guardType, types::boolType(), s3);
                if (!s4)
                    return std::unexpected(s4.error());
                s2 = s4;
            }

            auto bodyResult = inferExpr(*arm.body, armEnv, *s2);
            if (!bodyResult)
                return bodyResult;
            auto [bodyType, s3] = *bodyResult;

            auto s4 = unifyAndCompose(resultTypeVar, bodyType, s3);
            if (!s4)
                return std::unexpected(s4.error());
            currentSubst = *s4;
        }

        return std::pair { currentSubst.apply(resultTypeVar), currentSubst };
    }

    // --- Block expression ---
    if (auto const* block = dynamic_cast<ast::BlockExpr const*>(&expr))
    {
        auto blockEnv = env->childScope();
        auto currentSubst = subst;

        for (auto const& stmt: block->statements)
        {
            auto stmtResult = inferStmt(*stmt, blockEnv, currentSubst);
            if (!stmtResult)
                return std::unexpected(stmtResult.error());
            currentSubst = *stmtResult;
        }

        return inferExpr(*block->result, blockEnv, currentSubst);
    }

    // --- List expression ---
    if (auto const* listExpr = dynamic_cast<ast::ListExpr const*>(&expr))
    {
        if (listExpr->elements.empty())
        {
            auto elemVar = env->freshTypeVarType();
            return std::pair { types::list(elemVar), subst };
        }

        auto firstResult = inferExpr(*listExpr->elements[0], env, subst);
        if (!firstResult)
            return firstResult;
        auto [elemType, currentSubst] = *firstResult;

        for (size_t i = 1; i < listExpr->elements.size(); ++i)
        {
            auto elemResult = inferExpr(*listExpr->elements[i], env, currentSubst);
            if (!elemResult)
                return elemResult;
            auto [nextType, s] = *elemResult;
            auto unified = unifyAndCompose(elemType, nextType, s);
            if (!unified)
                return std::unexpected(unified.error());
            currentSubst = *unified;
            elemType = currentSubst.apply(elemType);
        }

        return std::pair { types::list(currentSubst.apply(elemType)), currentSubst };
    }

    // --- Cons expression ---
    if (auto const* consExpr = dynamic_cast<ast::ConsExpr const*>(&expr))
    {
        auto headResult = inferExpr(*consExpr->head, env, subst);
        if (!headResult)
            return headResult;
        auto [headType, s1] = *headResult;

        auto tailResult = inferExpr(*consExpr->tail, env, s1);
        if (!tailResult)
            return tailResult;
        auto [tailType, s2] = *tailResult;

        auto listType = types::list(headType);
        auto s3 = unifyAndCompose(tailType, listType, s2);
        if (!s3)
            return std::unexpected(s3.error());
        return std::pair { s3->apply(listType), *s3 };
    }

    // --- List concatenation ---
    if (auto const* concat = dynamic_cast<ast::ConcatListExpr const*>(&expr))
    {
        auto leftResult = inferExpr(*concat->left, env, subst);
        if (!leftResult)
            return leftResult;
        auto [leftType, s1] = *leftResult;

        auto rightResult = inferExpr(*concat->right, env, s1);
        if (!rightResult)
            return rightResult;
        auto [rightType, s2] = *rightResult;

        auto s3 = unifyAndCompose(leftType, rightType, s2);
        if (!s3)
            return std::unexpected(s3.error());
        return std::pair { s3->apply(leftType), *s3 };
    }

    // --- List range ---
    if (dynamic_cast<ast::ListRangeExpr const*>(&expr))
        return std::pair { types::list(types::intType()), subst };

    // --- Tuple expression ---
    if (auto const* tupleExpr = dynamic_cast<ast::TupleExpr const*>(&expr))
    {
        std::vector<TypePtr> elemTypes;
        auto currentSubst = subst;

        for (auto const& elem: tupleExpr->elements)
        {
            auto elemResult = inferExpr(*elem, env, currentSubst);
            if (!elemResult)
                return elemResult;
            auto [elemType, s] = *elemResult;
            elemTypes.push_back(elemType);
            currentSubst = s;
        }

        return std::pair { types::tuple(elemTypes), currentSubst };
    }

    // --- Option expression ---
    if (auto const* optExpr = dynamic_cast<ast::OptionExpr const*>(&expr))
    {
        if (optExpr->isSome)
        {
            auto valResult = inferExpr(*optExpr->value, env, subst);
            if (!valResult)
                return valResult;
            auto [valType, s1] = *valResult;
            return std::pair { types::option(valType), s1 };
        }
        else
        {
            auto innerVar = env->freshTypeVarType();
            return std::pair { types::option(innerVar), subst };
        }
    }

    // --- Result expression ---
    if (auto const* resExpr = dynamic_cast<ast::ResultExpr const*>(&expr))
    {
        auto payloadResult = inferExpr(*resExpr->payload, env, subst);
        if (!payloadResult)
            return payloadResult;
        auto [payloadType, s1] = *payloadResult;

        if (resExpr->isOk)
        {
            auto errVar = env->freshTypeVarType();
            return std::pair { types::result(payloadType, errVar), s1 };
        }
        else
        {
            auto okVar = env->freshTypeVarType();
            return std::pair { types::result(okVar, payloadType), s1 };
        }
    }

    // --- Option default expression (?|) ---
    if (auto const* optDefault = dynamic_cast<ast::OptionDefaultExpr const*>(&expr))
    {
        auto optResult = inferExpr(*optDefault->option, env, subst);
        if (!optResult)
            return optResult;
        auto [optType, s1] = *optResult;

        // Option operand should be option<T>, extract T
        auto innerVar = env->freshTypeVarType();
        auto s2 = unifyAndCompose(optType, types::option(innerVar), s1);
        if (!s2)
            return std::unexpected(s2.error());

        auto defResult = inferExpr(*optDefault->defaultValue, env, *s2);
        if (!defResult)
            return defResult;
        auto [defType, s3] = *defResult;

        // Default value type must match inner type T
        auto s4 = unifyAndCompose(s3.apply(innerVar), defType, s3);
        if (!s4)
            return std::unexpected(s4.error());

        return std::pair { s4->apply(innerVar), *s4 };
    }

    // --- Optional chaining expression (?.) ---
    if (auto const* optChain = dynamic_cast<ast::OptionalChainExpr const*>(&expr))
    {
        auto objResult = inferExpr(*optChain->object, env, subst);
        if (!objResult)
            return objResult;
        auto [objType, s1] = *objResult;

        // Object should be option<T>, extract T
        auto innerVar = env->freshTypeVarType();
        auto s2 = unifyAndCompose(objType, types::option(innerVar), s1);
        if (!s2)
            return std::unexpected(s2.error());

        // Field access on the inner type yields some result type;
        // wrap it back in option since ?. always returns option<U>
        auto fieldVar = env->freshTypeVarType();
        return std::pair { types::option(fieldVar), *s2 };
    }

    // --- Try expression (?) ---
    if (auto const* tryExpr = dynamic_cast<ast::TryExpr const*>(&expr))
    {
        auto operandResult = inferExpr(*tryExpr->operand, env, subst);
        if (!operandResult)
            return operandResult;
        auto [operandType, s1] = *operandResult;

        // Try to unify with option<T> or result<T, E>
        auto innerVar = env->freshTypeVarType();

        // Try option first
        auto optResult = unify(s1.apply(operandType), types::option(innerVar));
        if (optResult)
        {
            auto s2 = optResult->compose(s1);
            return std::pair { s2.apply(innerVar), s2 };
        }

        // Try result
        auto errVar = env->freshTypeVarType();
        auto resResult = unify(s1.apply(operandType), types::result(innerVar, errVar));
        if (resResult)
        {
            auto s2 = resResult->compose(s1);
            return std::pair { s2.apply(innerVar), s2 };
        }

        return std::unexpected(std::format("Cannot apply ? operator to type {}", toString(operandType)));
    }

    // --- Try-with expression ---
    if (auto const* tryWith = dynamic_cast<ast::TryWithExpr const*>(&expr))
    {
        auto bodyResult = inferExpr(*tryWith->body, env, subst);
        if (!bodyResult)
            return bodyResult;
        auto [bodyType, s1] = *bodyResult;

        auto resultTypeVar = env->freshTypeVarType();
        auto s2 = unifyAndCompose(resultTypeVar, bodyType, s1);
        if (!s2)
            return std::unexpected(s2.error());

        auto currentSubst = *s2;
        for (auto const& arm: tryWith->handlers)
        {
            auto patResult = inferPattern(*arm.pattern, env, currentSubst);
            if (!patResult)
                return std::unexpected(patResult.error());

            auto armEnv = env->childScope();
            for (auto const& [name, type]: patResult->bindings)
                armEnv->bindMono(name, patResult->subst.apply(type));

            auto handlerResult = inferExpr(*arm.body, armEnv, patResult->subst);
            if (!handlerResult)
                return handlerResult;
            auto [handlerType, s3] = *handlerResult;

            auto s4 = unifyAndCompose(resultTypeVar, handlerType, s3);
            if (!s4)
                return std::unexpected(s4.error());
            currentSubst = *s4;
        }

        return std::pair { currentSubst.apply(resultTypeVar), currentSubst };
    }

    // --- Try-finally expression ---
    if (auto const* tryFinally = dynamic_cast<ast::TryFinallyExpr const*>(&expr))
    {
        auto bodyResult = inferExpr(*tryFinally->body, env, subst);
        if (!bodyResult)
            return bodyResult;
        auto [bodyType, s1] = *bodyResult;

        // Finally expression is evaluated for side effects only
        auto finallyResult = inferExpr(*tryFinally->finallyExpr, env, s1);
        if (!finallyResult)
            return finallyResult;
        auto [_finallyType, s2] = *finallyResult;

        return std::pair { s2.apply(bodyType), s2 };
    }

    // --- List comprehension ---
    if (auto const* comp = dynamic_cast<ast::ListComprehensionExpr const*>(&expr))
    {
        auto srcResult = inferExpr(*comp->source, env, subst);
        if (!srcResult)
            return srcResult;
        auto [srcType, s1] = *srcResult;

        auto elemVar = env->freshTypeVarType();
        auto s2 = unifyAndCompose(srcType, types::list(elemVar), s1);
        if (!s2)
            return std::unexpected(s2.error());

        auto compEnv = env->childScope();
        compEnv->bindMono(comp->variable, s2->apply(elemVar));

        auto currentSubst = *s2;

        if (comp->filter)
        {
            auto filterResult = inferExpr(*comp->filter, compEnv, currentSubst);
            if (!filterResult)
                return filterResult;
            auto [filterType, s3] = *filterResult;
            auto s4 = unifyAndCompose(filterType, types::boolType(), s3);
            if (!s4)
                return std::unexpected(s4.error());
            currentSubst = *s4;
        }

        auto bodyResult = inferExpr(*comp->body, compEnv, currentSubst);
        if (!bodyResult)
            return bodyResult;
        auto [bodyType, s3] = *bodyResult;

        // Nested comprehension body already returns list(T) — don't double-wrap
        if (dynamic_cast<ast::ListComprehensionExpr const*>(comp->body.get()))
            return std::pair { bodyType, s3 };

        return std::pair { types::list(s3.apply(bodyType)), s3 };
    }

    // --- Shell expressions: treat as string ---
    if (dynamic_cast<ast::ShellCommandExpr const*>(&expr))
        return std::pair { types::strType(), subst };

    if (dynamic_cast<ast::VariableExpr const*>(&expr))
        return std::pair { types::strType(), subst };

    if (dynamic_cast<ast::SubstitutionExpr const*>(&expr))
        return std::pair { types::strType(), subst };

    if (dynamic_cast<ast::ConcatExpr const*>(&expr))
        return std::pair { types::strType(), subst };

    if (dynamic_cast<ast::TildeExpr const*>(&expr))
        return std::pair { types::strType(), subst };

    if (dynamic_cast<ast::GlobExpr const*>(&expr))
        return std::pair { types::strType(), subst };

    if (dynamic_cast<ast::ParamExpansionExpr const*>(&expr))
        return std::pair { types::strType(), subst };

    if (dynamic_cast<ast::ArithExpansionExpr const*>(&expr))
        return std::pair { types::intType(), subst };

    // Mutable assignment expression: `name <- value` returns unit
    if (auto const* mutExpr = dynamic_cast<ast::MutAssignExpr const*>(&expr))
    {
        auto valResult = inferExpr(*mutExpr->value, env, subst);
        if (!valResult)
            return valResult;
        auto [valType, s1] = *valResult;

        // Unify with existing binding type if available
        if (auto scheme = env->lookup(mutExpr->name))
        {
            auto existingType = env->instantiate(*scheme);
            auto s2 = unifyAndCompose(valType, existingType, s1);
            if (!s2)
                return std::unexpected(s2.error());
            return std::pair { types::unitType(), *s2 };
        }

        return std::pair { types::unitType(), s1 };
    }

    // Unknown expression type — use fresh type variable to avoid blocking inference
    return std::pair { env->freshTypeVarType(), subst };
}

// ============================================================================
// Binary Operator Inference
// ============================================================================

TypeInferencer::InferResult TypeInferencer::inferBinaryOp(ast::BinaryOp op,
                                                          ast::Expr const& left,
                                                          ast::Expr const& right,
                                                          TypeEnvPtr const& env,
                                                          Substitution subst)
{
    auto leftResult = inferExpr(left, env, subst);
    if (!leftResult)
        return leftResult;
    auto [leftType, s1] = *leftResult;

    auto rightResult = inferExpr(right, env, s1);
    if (!rightResult)
        return rightResult;
    auto [rightType, s2] = *rightResult;

    switch (op)
    {
        case ast::BinaryOp::Add:
        case ast::BinaryOp::Sub:
        case ast::BinaryOp::Mul:
        case ast::BinaryOp::Div:
        case ast::BinaryOp::Mod:
        case ast::BinaryOp::Pow: {
            // Determine numeric type based on context
            auto numericType = types::intType();
            if (op == ast::BinaryOp::Add && (containsStringLiteral(left) || containsStringLiteral(right)))
            {
                // String concatenation via +
                auto s3 = unifyAndCompose(leftType, types::strType(), s2);
                if (!s3)
                    return std::unexpected(s3.error());
                auto s4 = unifyAndCompose(rightType, types::strType(), *s3);
                if (!s4)
                    return std::unexpected(s4.error());
                return std::pair { types::strType(), *s4 };
            }

            if (containsFloatLiteral(left) || containsFloatLiteral(right))
                numericType = types::floatType();

            auto s3 = unifyAndCompose(leftType, numericType, s2);
            if (!s3)
                return std::unexpected(s3.error());
            auto s4 = unifyAndCompose(rightType, numericType, *s3);
            if (!s4)
                return std::unexpected(s4.error());
            return std::pair { numericType, *s4 };
        }

        case ast::BinaryOp::Eq:
        case ast::BinaryOp::Ne: {
            // Polymorphic equality: both sides must be the same type
            auto s3 = unifyAndCompose(leftType, rightType, s2);
            if (!s3)
                return std::unexpected(s3.error());
            return std::pair { types::boolType(), *s3 };
        }

        case ast::BinaryOp::Lt:
        case ast::BinaryOp::Le:
        case ast::BinaryOp::Gt:
        case ast::BinaryOp::Ge: {
            // Ordering: both int
            auto numericType = types::intType();
            if (containsFloatLiteral(left) || containsFloatLiteral(right))
                numericType = types::floatType();

            auto s3 = unifyAndCompose(leftType, numericType, s2);
            if (!s3)
                return std::unexpected(s3.error());
            auto s4 = unifyAndCompose(rightType, numericType, *s3);
            if (!s4)
                return std::unexpected(s4.error());
            return std::pair { types::boolType(), *s4 };
        }

        case ast::BinaryOp::And:
        case ast::BinaryOp::Or: {
            auto s3 = unifyAndCompose(leftType, types::boolType(), s2);
            if (!s3)
                return std::unexpected(s3.error());
            auto s4 = unifyAndCompose(rightType, types::boolType(), *s3);
            if (!s4)
                return std::unexpected(s4.error());
            return std::pair { types::boolType(), *s4 };
        }
    }

    return std::unexpected("Unknown binary operator");
}

// ============================================================================
// Pattern Inference
// ============================================================================

std::expected<TypeInferencer::PatternResult, std::string> TypeInferencer::inferPattern(
    pattern::Pattern const& pat, TypeEnvPtr const& env, Substitution subst)
{
    // --- Literal pattern ---
    if (auto const* lit = dynamic_cast<pattern::LiteralPattern const*>(&pat))
    {
        TypePtr type;
        std::visit(
            [&](auto const& val) {
                using T = std::decay_t<decltype(val)>;
                if constexpr (std::is_same_v<T, int64_t>)
                    type = types::intType();
                else if constexpr (std::is_same_v<T, double>)
                    type = types::floatType();
                else if constexpr (std::is_same_v<T, bool>)
                    type = types::boolType();
                else if constexpr (std::is_same_v<T, std::string>)
                    type = types::strType();
            },
            lit->value);
        return PatternResult { type, {}, subst };
    }

    // --- Variable pattern ---
    if (auto const* var = dynamic_cast<pattern::VariablePattern const*>(&pat))
    {
        auto freshType = env->freshTypeVarType();
        return PatternResult { freshType, { { var->name, freshType } }, subst };
    }

    // --- Wildcard pattern ---
    if (dynamic_cast<pattern::WildcardPattern const*>(&pat))
    {
        auto freshType = env->freshTypeVarType();
        return PatternResult { freshType, {}, subst };
    }

    // --- Tuple pattern ---
    if (auto const* tuplePat = dynamic_cast<pattern::TuplePattern const*>(&pat))
    {
        std::vector<TypePtr> elemTypes;
        std::vector<std::pair<std::string, TypePtr>> allBindings;
        auto currentSubst = subst;

        for (auto const& elem: tuplePat->elements)
        {
            auto elemResult = inferPattern(*elem, env, currentSubst);
            if (!elemResult)
                return elemResult;
            elemTypes.push_back(elemResult->type);
            for (auto& b: elemResult->bindings)
                allBindings.push_back(std::move(b));
            currentSubst = elemResult->subst;
        }

        return PatternResult { types::tuple(elemTypes), std::move(allBindings), currentSubst };
    }

    // --- List pattern ---
    if (auto const* listPat = dynamic_cast<pattern::ListPattern const*>(&pat))
    {
        auto elemVar = env->freshTypeVarType();
        std::vector<std::pair<std::string, TypePtr>> allBindings;
        auto currentSubst = subst;

        for (auto const& elem: listPat->elements)
        {
            auto elemResult = inferPattern(*elem, env, currentSubst);
            if (!elemResult)
                return elemResult;

            auto s = unifyAndCompose(elemVar, elemResult->type, elemResult->subst);
            if (!s)
                return std::unexpected(s.error());

            for (auto& b: elemResult->bindings)
                allBindings.push_back(std::move(b));
            currentSubst = *s;
            elemVar = currentSubst.apply(elemVar);
        }

        if (listPat->restBinding)
            allBindings.emplace_back(*listPat->restBinding, types::list(elemVar));

        return PatternResult { types::list(elemVar), std::move(allBindings), currentSubst };
    }

    // --- Cons pattern ---
    if (auto const* consPat = dynamic_cast<pattern::ConsPattern const*>(&pat))
    {
        auto headResult = inferPattern(*consPat->head, env, subst);
        if (!headResult)
            return headResult;

        auto tailResult = inferPattern(*consPat->tail, env, headResult->subst);
        if (!tailResult)
            return tailResult;

        auto listType = types::list(headResult->type);
        auto s = unifyAndCompose(tailResult->type, listType, tailResult->subst);
        if (!s)
            return std::unexpected(s.error());

        auto allBindings = headResult->bindings;
        for (auto& b: tailResult->bindings)
            allBindings.push_back(std::move(b));

        return PatternResult { s->apply(listType), std::move(allBindings), *s };
    }

    // --- Constructor pattern (Some, None, Ok, Error) ---
    if (auto const* ctorPat = dynamic_cast<pattern::ConstructorPattern const*>(&pat))
    {
        if (ctorPat->name == "Some")
        {
            if (ctorPat->payload)
            {
                auto payloadResult = inferPattern(**ctorPat->payload, env, subst);
                if (!payloadResult)
                    return payloadResult;
                return PatternResult { types::option(payloadResult->type),
                                       std::move(payloadResult->bindings),
                                       payloadResult->subst };
            }
            auto innerVar = env->freshTypeVarType();
            return PatternResult { types::option(innerVar), {}, subst };
        }
        else if (ctorPat->name == "None")
        {
            auto innerVar = env->freshTypeVarType();
            return PatternResult { types::option(innerVar), {}, subst };
        }
        else if (ctorPat->name == "Ok")
        {
            auto errVar = env->freshTypeVarType();
            if (ctorPat->payload)
            {
                auto payloadResult = inferPattern(**ctorPat->payload, env, subst);
                if (!payloadResult)
                    return payloadResult;
                return PatternResult { types::result(payloadResult->type, errVar),
                                       std::move(payloadResult->bindings),
                                       payloadResult->subst };
            }
            auto okVar = env->freshTypeVarType();
            return PatternResult { types::result(okVar, errVar), {}, subst };
        }
        else if (ctorPat->name == "Error")
        {
            auto okVar = env->freshTypeVarType();
            if (ctorPat->payload)
            {
                auto payloadResult = inferPattern(**ctorPat->payload, env, subst);
                if (!payloadResult)
                    return payloadResult;
                return PatternResult { types::result(okVar, payloadResult->type),
                                       std::move(payloadResult->bindings),
                                       payloadResult->subst };
            }
            auto errVar = env->freshTypeVarType();
            return PatternResult { types::result(okVar, errVar), {}, subst };
        }

        // Unknown constructor — use fresh type
        auto freshType = env->freshTypeVarType();
        std::vector<std::pair<std::string, TypePtr>> bindings;
        if (ctorPat->payload)
        {
            auto payloadResult = inferPattern(**ctorPat->payload, env, subst);
            if (!payloadResult)
                return payloadResult;
            bindings = std::move(payloadResult->bindings);
            subst = payloadResult->subst;
        }
        return PatternResult { freshType, std::move(bindings), subst };
    }

    // --- As pattern ---
    if (auto const* asPat = dynamic_cast<pattern::AsPattern const*>(&pat))
    {
        auto innerResult = inferPattern(*asPat->inner, env, subst);
        if (!innerResult)
            return innerResult;
        auto bindings = innerResult->bindings;
        bindings.emplace_back(asPat->name, innerResult->type);
        return PatternResult { innerResult->type, std::move(bindings), innerResult->subst };
    }

    // --- Or pattern ---
    if (auto const* orPat = dynamic_cast<pattern::OrPattern const*>(&pat))
    {
        if (orPat->alternatives.empty())
            return PatternResult { env->freshTypeVarType(), {}, subst };

        auto firstResult = inferPattern(*orPat->alternatives[0], env, subst);
        if (!firstResult)
            return firstResult;

        auto currentSubst = firstResult->subst;
        auto currentType = firstResult->type;

        for (size_t i = 1; i < orPat->alternatives.size(); ++i)
        {
            auto altResult = inferPattern(*orPat->alternatives[i], env, currentSubst);
            if (!altResult)
                return altResult;
            auto s = unifyAndCompose(currentType, altResult->type, altResult->subst);
            if (!s)
                return std::unexpected(s.error());
            currentSubst = *s;
            currentType = currentSubst.apply(currentType);
        }

        return PatternResult { currentType, firstResult->bindings, currentSubst };
    }

    // --- Guarded pattern ---
    if (auto const* guardPat = dynamic_cast<pattern::GuardedPattern const*>(&pat))
    {
        auto innerResult = inferPattern(*guardPat->pattern, env, subst);
        if (!innerResult)
            return innerResult;
        // Guard is checked at arm level in match inference, skip here
        return innerResult;
    }

    // --- Record pattern ---
    if (auto const* recPat = dynamic_cast<pattern::RecordPattern const*>(&pat))
    {
        std::vector<std::pair<std::string, TypePtr>> allBindings;
        auto currentSubst = subst;

        for (auto const& field: recPat->fields)
        {
            if (field.pattern)
            {
                auto fieldResult = inferPattern(*field.pattern, env, currentSubst);
                if (!fieldResult)
                    return fieldResult;
                for (auto& b: fieldResult->bindings)
                    allBindings.push_back(std::move(b));
                currentSubst = fieldResult->subst;
            }
            else
            {
                // Punning: { name } means { name = name }
                auto freshType = env->freshTypeVarType();
                allBindings.emplace_back(field.name, freshType);
            }
        }

        // Use a fresh type variable for the record — we don't have enough info to construct the record type
        return PatternResult { env->freshTypeVarType(), std::move(allBindings), currentSubst };
    }

    // Unknown pattern type
    return PatternResult { env->freshTypeVarType(), {}, subst };
}

// ============================================================================
// Statement Inference
// ============================================================================

std::expected<Substitution, std::string> TypeInferencer::inferStmt(ast::Statement const& stmt,
                                                                   TypeEnvPtr const& env,
                                                                   Substitution subst)
{
    // --- Compound statement ---
    if (auto const* compound = dynamic_cast<ast::CompoundStmt const*>(&stmt))
    {
        auto currentSubst = subst;
        for (auto const& child: compound->statements)
        {
            // CompoundStmt children are Node*, could be Statement or Expr
            if (auto const* childStmt = dynamic_cast<ast::Statement const*>(child.get()))
            {
                auto result = inferStmt(*childStmt, env, currentSubst);
                if (!result)
                    return result;
                currentSubst = *result;
            }
            else if (auto const* childExpr = dynamic_cast<ast::Expr const*>(child.get()))
            {
                auto result = inferExpr(*childExpr, env, currentSubst);
                if (!result)
                    return std::unexpected(result.error());
                currentSubst = result->second;
            }
        }
        return currentSubst;
    }

    // --- Expression statement ---
    if (auto const* exprStmt = dynamic_cast<ast::ExprStmt const*>(&stmt))
    {
        auto result = inferExpr(*exprStmt->expr, env, subst);
        if (!result)
        {
            // Don't fail the whole inference for an expression statement error
            recordError(result.error());
            return subst;
        }
        return result->second;
    }

    // --- Let binding statement ---
    if (auto const* letStmt = dynamic_cast<ast::LetBindingStmt const*>(&stmt))
    {
        if (letStmt->isFunction())
        {
            // Function definition
            auto funcEnv = env->childScope();
            std::vector<TypePtr> paramTypes;

            // Collect param types (use annotations where available, fresh vars otherwise)
            for (auto const& param: letStmt->parameters)
            {
                auto t = param.typeAnnotation ? *param.typeAnnotation : funcEnv->freshTypeVarType();
                paramTypes.push_back(t);
            }

            if (letStmt->isRecursive)
            {
                // Pre-bind with fresh function type for recursive reference
                auto freshRet = funcEnv->freshTypeVarType();
                auto funcType = freshRet;
                for (auto it = paramTypes.rbegin(); it != paramTypes.rend(); ++it)
                    funcType = types::function(*it, funcType);
                funcEnv->bindMono(letStmt->name, funcType);
            }

            // Bind parameters in body scope
            auto bodyEnv = funcEnv->childScope();
            for (size_t i = 0; i < letStmt->parameters.size(); ++i)
                bodyEnv->bindMono(letStmt->parameters[i].name, paramTypes[i]);

            auto valResult = inferExpr(*letStmt->value, bodyEnv, subst);
            if (!valResult)
            {
                recordError(valResult.error());
                return subst;
            }
            auto [retType, s1] = *valResult;

            // Build curried function type
            auto resolvedRet = s1.apply(retType);
            auto funcType = resolvedRet;
            for (auto it = paramTypes.rbegin(); it != paramTypes.rend(); ++it)
                funcType = types::function(s1.apply(*it), funcType);

            if (letStmt->isRecursive)
            {
                // Unify with pre-bound type
                auto scheme = funcEnv->lookup(letStmt->name);
                if (scheme)
                {
                    auto preBound = funcEnv->instantiate(*scheme);
                    auto s2 = unifyAndCompose(funcType, preBound, s1);
                    if (!s2)
                    {
                        recordError(s2.error());
                        return subst;
                    }
                    s1 = *s2;
                    funcType = s1.apply(funcType);
                    resolvedRet = s1.apply(retType);
                }
            }

            // Record inferred function type
            InferredFunctionType inferred;
            for (auto const& pt: paramTypes)
                inferred.paramTypes.push_back(s1.apply(pt));
            inferred.returnType = resolvedRet;
            recordFunction(letStmt->name, std::move(inferred));

            // Generalize and bind in enclosing scope
            auto scheme = env->generalize(s1.apply(funcType));
            env->bind(letStmt->name, scheme);

            // Handle mutual recursion (`and` bindings)
            auto currentSubst = s1;
            for (auto const& andBind: letStmt->andBindings)
            {
                auto andFuncEnv = env->childScope();
                std::vector<TypePtr> andParamTypes;
                for (auto const& param: andBind.parameters)
                {
                    auto t = param.typeAnnotation ? *param.typeAnnotation : andFuncEnv->freshTypeVarType();
                    andParamTypes.push_back(t);
                }

                // Pre-bind for recursive reference
                auto freshRet = andFuncEnv->freshTypeVarType();
                auto andFuncType = freshRet;
                for (auto it = andParamTypes.rbegin(); it != andParamTypes.rend(); ++it)
                    andFuncType = types::function(*it, andFuncType);
                andFuncEnv->bindMono(andBind.name, andFuncType);

                // Also bind the primary function and any previous `and` bindings
                if (auto primaryScheme = env->lookup(letStmt->name))
                    andFuncEnv->bind(letStmt->name, *primaryScheme);

                auto andBodyEnv = andFuncEnv->childScope();
                for (size_t i = 0; i < andBind.parameters.size(); ++i)
                    andBodyEnv->bindMono(andBind.parameters[i].name, andParamTypes[i]);

                auto andValResult = inferExpr(*andBind.value, andBodyEnv, currentSubst);
                if (!andValResult)
                {
                    recordError(andValResult.error());
                    continue;
                }
                auto [andRetType, s2] = *andValResult;

                auto resolvedAndRet = s2.apply(andRetType);
                auto resolvedAndFunc = resolvedAndRet;
                for (auto it = andParamTypes.rbegin(); it != andParamTypes.rend(); ++it)
                    resolvedAndFunc = types::function(s2.apply(*it), resolvedAndFunc);

                // Record inferred type
                InferredFunctionType andInferred;
                for (auto const& pt: andParamTypes)
                    andInferred.paramTypes.push_back(s2.apply(pt));
                andInferred.returnType = resolvedAndRet;
                recordFunction(andBind.name, std::move(andInferred));

                auto andScheme = env->generalize(s2.apply(resolvedAndFunc));
                env->bind(andBind.name, andScheme);
                currentSubst = s2;
            }

            return currentSubst;
        }
        else if (letStmt->isDestructuring())
        {
            // Destructuring let binding
            auto valResult = inferExpr(*letStmt->value, env, subst);
            if (!valResult)
            {
                recordError(valResult.error());
                return subst;
            }
            auto [valType, s1] = *valResult;

            auto patResult = inferPattern(*letStmt->destructurePattern, env, s1);
            if (!patResult)
            {
                recordError(patResult.error());
                return subst;
            }

            auto s2 = unifyAndCompose(valType, patResult->type, patResult->subst);
            if (!s2)
            {
                recordError(s2.error());
                return subst;
            }

            for (auto const& [name, type]: patResult->bindings)
                env->bindMono(name, s2->apply(type));

            return *s2;
        }
        else
        {
            // Simple value binding
            auto valResult = inferExpr(*letStmt->value, env, subst);
            if (!valResult)
            {
                recordError(valResult.error());
                return subst;
            }
            auto [valType, s1] = *valResult;

            // Check if the value is a lambda — treat as function definition
            if (auto const* lambda = dynamic_cast<ast::LambdaExpr const*>(letStmt->value.get()))
            {
                InferredFunctionType inferred;
                // The valType is already a function type from lambda inference
                auto resolved = s1.apply(valType);
                auto current = resolved;
                for (auto const& param: lambda->parameters)
                {
                    (void) param;
                    if (auto const* fn = current->asFunction())
                    {
                        inferred.paramTypes.push_back(fn->paramType);
                        current = fn->returnType;
                    }
                }
                inferred.returnType = current;
                recordFunction(letStmt->name, std::move(inferred));
            }

            auto scheme = env->generalize(s1.apply(valType));
            env->bind(letStmt->name, scheme);
            return s1;
        }
    }

    // --- Mutable assignment statement ---
    if (auto const* mutAssign = dynamic_cast<ast::MutAssignStmt const*>(&stmt))
    {
        auto valResult = inferExpr(*mutAssign->value, env, subst);
        if (!valResult)
        {
            recordError(valResult.error());
            return subst;
        }
        auto [valType, s1] = *valResult;

        // Unify with existing binding type if available
        auto scheme = env->lookup(mutAssign->name);
        if (scheme)
        {
            auto existingType = env->instantiate(*scheme);
            auto s2 = unifyAndCompose(valType, existingType, s1);
            if (!s2)
            {
                recordError(s2.error());
                return s1;
            }
            return *s2;
        }

        return s1;
    }

    // --- Shell statements: skip type inference for these ---
    // They don't participate in F# type inference
    if (dynamic_cast<ast::ProgramCall const*>(&stmt) || dynamic_cast<ast::CallPipeline const*>(&stmt)
        || dynamic_cast<ast::WhileStmt const*>(&stmt) || dynamic_cast<ast::LogicalAndStmt const*>(&stmt)
        || dynamic_cast<ast::LogicalOrStmt const*>(&stmt) || dynamic_cast<ast::ForInStmt const*>(&stmt)
        || dynamic_cast<ast::BreakStmt const*>(&stmt) || dynamic_cast<ast::ContinueStmt const*>(&stmt))
    {
        return subst;
    }

    // Builtin statements: skip
    if (dynamic_cast<ast::BuiltinExitStmt const*>(&stmt) || dynamic_cast<ast::BuiltinExportStmt const*>(&stmt)
        || dynamic_cast<ast::BuiltinReadStmt const*>(&stmt) || dynamic_cast<ast::BuiltinSetStmt const*>(&stmt)
        || dynamic_cast<ast::BuiltinChDirStmt const*>(&stmt)
        || dynamic_cast<ast::BuiltinUnsetStmt const*>(&stmt)
        || dynamic_cast<ast::BuiltinJobsStmt const*>(&stmt) || dynamic_cast<ast::BuiltinFgStmt const*>(&stmt)
        || dynamic_cast<ast::BuiltinBgStmt const*>(&stmt) || dynamic_cast<ast::BuiltinWaitStmt const*>(&stmt)
        || dynamic_cast<ast::BuiltinBindStmt const*>(&stmt)
        || dynamic_cast<ast::BuiltinWhichStmt const*>(&stmt))
    {
        return subst;
    }

    // Unknown statement type — don't fail, just continue
    return subst;
}

} // namespace endo
