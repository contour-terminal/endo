// SPDX-License-Identifier: Apache-2.0
// Option Combinator IR Generators
//
// This file contains the IRGenerator member functions for Option combinators:
// Option.map, Option.bind, Option.defaultValue (both module-qualified and method-style),
// plus the resolveFunctionArgument helper.
//
// These are IRGenerator member functions defined in a separate translation unit
// to reduce the size of IRGenerator.cpp.

#include <endo-language/codegen/IRGenerator.hpp>

#include <CoreVM/CoreVM.hpp>

namespace endo
{

std::optional<IRGenerator::ResolvedFunction> IRGenerator::resolveFunctionArgument(ast::Expr const* expr)
{
    // Unwrap ParenExpr wrappers
    while (auto const* paren = dynamic_cast<ast::ParenExpr const*>(expr))
        expr = paren->inner.get();

    if (auto const* ident = dynamic_cast<ast::IdentifierExpr const*>(expr))
    {
        // Named function
        auto funcName = ident->name;
        if (auto const* func = lookupFSharpFunction(funcName))
            return ResolvedFunction { .func = func, .name = funcName };

        // Fallback: check function reference
        if (auto ref = lookupFSharpFunctionRef(funcName))
        {
            funcName = *ref;
            if (auto const* func = lookupFSharpFunction(funcName))
                return ResolvedFunction { .func = func, .name = funcName };
        }
        return std::nullopt;
    }

    if (auto const* lambda = dynamic_cast<ast::LambdaExpr const*>(expr))
    {
        auto funcName = generateLambdaName();
        FSharpFunction lambdaFunc;
        extractTypedParameters(lambda->parameters, lambdaFunc);
        lambdaFunc.body = lambda->body.get();
        lambdaFunc.returnKind = determineReturnKind(lambdaFunc.body);
        lambdaFunc.capturedBindings = collectFreeVariables(lambdaFunc.body, lambdaFunc.parameters);
        registerFSharpFunction(funcName, std::move(lambdaFunc));
        return ResolvedFunction { .func = lookupFSharpFunction(funcName), .name = funcName };
    }

    if (auto const* placeholder = dynamic_cast<ast::PlaceholderLambdaExpr const*>(expr))
    {
        auto funcName = generateLambdaName();
        registerFSharpFunction(funcName, createFunctionFromPlaceholder(*placeholder));
        return ResolvedFunction { .func = lookupFSharpFunction(funcName), .name = funcName };
    }

    return std::nullopt;
}

bool IRGenerator::tryGenerateOptionCall(std::string const& methodName,
                                        std::vector<ast::Expr const*> const& argExprs)
{
    if (methodName == "map")
    {
        generateOptionMap(argExprs);
        return true;
    }
    if (methodName == "bind")
    {
        generateOptionBind(argExprs);
        return true;
    }
    if (methodName == "defaultValue")
    {
        generateOptionDefaultValue(argExprs);
        return true;
    }
    return false;
}

bool IRGenerator::tryGenerateOptionMethodCall(std::string const& methodName,
                                              ast::Expr const* objectExpr,
                                              std::vector<ast::Expr const*> const& argExprs)
{
    // Evaluate the object (the option value)
    auto* optionValue = codegen(objectExpr);
    if (!optionValue)
    {
        reportTypeError("Failed to evaluate option object for .{}", std::string_view(methodName));
        return true; // consumed, even on error
    }

    if (methodName == "map")
    {
        if (argExprs.size() != 1)
        {
            reportTypeError("Option.map requires exactly 1 function argument");
            return true;
        }
        generateOptionMapWithValue(argExprs[0], optionValue);
        return true;
    }
    if (methodName == "bind")
    {
        if (argExprs.size() != 1)
        {
            reportTypeError("Option.bind requires exactly 1 function argument");
            return true;
        }
        generateOptionBindWithValue(argExprs[0], optionValue);
        return true;
    }
    if (methodName == "defaultValue")
    {
        if (argExprs.size() != 1)
        {
            reportTypeError("Option.defaultValue requires exactly 1 default argument");
            return true;
        }
        generateOptionDefaultValueWithValue(argExprs[0], optionValue);
        return true;
    }
    return false;
}

void IRGenerator::generateOptionMap(std::vector<ast::Expr const*> const& argExprs)
{
    if (argExprs.size() != 2)
    {
        reportTypeError("Option.map requires 2 arguments: function and option value");
        return;
    }
    // Evaluate the option value first (argExprs[1])
    auto* optionValue = codegen(argExprs[1]);
    if (!optionValue)
    {
        reportTypeError("Failed to evaluate option argument for Option.map");
        return;
    }
    generateOptionMapWithValue(argExprs[0], optionValue);
}

void IRGenerator::generateOptionMapWithValue(ast::Expr const* funcExpr, CoreVM::Value* optionValue)
{
    // Resolve the function argument
    auto resolved = resolveFunctionArgument(funcExpr);
    if (!resolved)
    {
        reportTypeError("Option.map requires a function as first argument");
        return;
    }

    // Store option value in alloca for cross-block access
    auto* objStorage = createAllocaInEntryBlock(optionValue->type(), "optmap.obj");
    _builder.createStore(objStorage, optionValue, "optmap.obj.store");

    // Extract tag
    auto* tag = _builder.createObjGetTag(optionValue, "optmap.tag");
    auto* isSome =
        _builder.createNCmpEQ(tag, _builder.get(static_cast<CoreVM::CoreNumber>(1)), "optmap.is_some");

    // Create blocks
    auto* someBlock = _builder.createBlock("optmap.some");
    auto* noneBlock = _builder.createBlock("optmap.none");
    auto* continueBlock = _builder.createBlock("optmap.continue");

    // Result storage — always holds an Option (Object type)
    auto* resultStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "optmap.result");

    _builder.createCondBr(isSome, someBlock, noneBlock);

    // None path: create None option
    _builder.setInsertPoint(noneBlock);
    auto* noneObj = emitNoneOption("optmap.none");
    _builder.createStore(resultStorage, noneObj, "optmap.none.store");
    _builder.createBr(continueBlock);

    // Some path: extract inner value, apply function, wrap in Some
    _builder.setInsertPoint(someBlock);
    auto* objReload = _builder.createLoad(objStorage, "optmap.obj.reload");
    auto* innerValue = _builder.createObjGetSlot(
        objReload, _builder.get(static_cast<CoreVM::CoreNumber>(0)), "optmap.inner");

    // Use the option's inner type annotation (if available) for correct element typing.
    // ObjGetSlot returns Void; the annotation carries the actual type (e.g., String).
    auto elemType = getInnerType(optionValue).value_or(innerValue->type());
    auto* elemStorage = createAllocaInEntryBlock(elemType, "optmap.elem");
    _builder.createStore(elemStorage, innerValue, "optmap.elem.store");
    auto* elemReload = _builder.createLoad(elemStorage, "optmap.elem.reload");

    // Call the function with inner value
    generateFSharpCall(resolved->func, resolved->name, { elemReload });
    auto* mapped = _result;
    if (!mapped)
    {
        reportTypeError("Failed to evaluate function in Option.map");
        return;
    }

    // Remember the mapped value's type for inner type annotation
    auto const mappedType = mapped->type();

    // Store mapped value in tmp alloca (survives across OALLOC)
    auto* mappedTmp = createAllocaInEntryBlock(mappedType, "optmap.mapped");
    _builder.createStore(mappedTmp, mapped, "optmap.mapped.store");

    // Wrap result in Some
    auto* mappedReload = _builder.createLoad(mappedTmp, "optmap.mapped.reload");
    auto* someObj = emitSomeOption(mappedReload, mappedType, "optmap.some");
    _builder.createStore(resultStorage, someObj, "optmap.some.store");
    _builder.createBr(continueBlock);

    // Continue
    _builder.setInsertPoint(continueBlock);
    _result = _builder.createLoad(resultStorage, "optmap.result.load");

    // Annotate the result with inner type info for downstream ?| and convertToString
    annotateInnerType(_result, mappedType);
    annotateObjectTypeId(_result, CoreVM::BuiltinTypeId::Option);
}

void IRGenerator::generateOptionBind(std::vector<ast::Expr const*> const& argExprs)
{
    if (argExprs.size() != 2)
    {
        reportTypeError("Option.bind requires 2 arguments: function and option value");
        return;
    }
    // Evaluate the option value first (argExprs[1])
    auto* optionValue = codegen(argExprs[1]);
    if (!optionValue)
    {
        reportTypeError("Failed to evaluate option argument for Option.bind");
        return;
    }
    generateOptionBindWithValue(argExprs[0], optionValue);
}

void IRGenerator::generateOptionBindWithValue(ast::Expr const* funcExpr, CoreVM::Value* optionValue)
{
    // Resolve the function argument
    auto resolved = resolveFunctionArgument(funcExpr);
    if (!resolved)
    {
        reportTypeError("Option.bind requires a function as first argument");
        return;
    }

    // Store option value in alloca for cross-block access
    auto* objStorage = createAllocaInEntryBlock(optionValue->type(), "optbind.obj");
    _builder.createStore(objStorage, optionValue, "optbind.obj.store");

    // Extract tag
    auto* tag = _builder.createObjGetTag(optionValue, "optbind.tag");
    auto* isSome =
        _builder.createNCmpEQ(tag, _builder.get(static_cast<CoreVM::CoreNumber>(1)), "optbind.is_some");

    // Create blocks
    auto* someBlock = _builder.createBlock("optbind.some");
    auto* noneBlock = _builder.createBlock("optbind.none");
    auto* continueBlock = _builder.createBlock("optbind.continue");

    // Result storage — holds Option from either path
    auto* resultStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "optbind.result");

    _builder.createCondBr(isSome, someBlock, noneBlock);

    // None path: create None option
    _builder.setInsertPoint(noneBlock);
    auto* noneObj = emitNoneOption("optbind.none");
    _builder.createStore(resultStorage, noneObj, "optbind.none.store");
    _builder.createBr(continueBlock);

    // Some path: extract inner value, call function (f returns Option directly)
    _builder.setInsertPoint(someBlock);
    auto* objReload = _builder.createLoad(objStorage, "optbind.obj.reload");
    auto* innerValue = _builder.createObjGetSlot(
        objReload, _builder.get(static_cast<CoreVM::CoreNumber>(0)), "optbind.inner");

    // Use the option's inner type annotation for correct element typing
    auto elemType = getInnerType(optionValue).value_or(innerValue->type());
    auto* elemStorage = createAllocaInEntryBlock(elemType, "optbind.elem");
    _builder.createStore(elemStorage, innerValue, "optbind.elem.store");
    auto* elemReload = _builder.createLoad(elemStorage, "optbind.elem.reload");

    // Call the function — f already returns an Option, so no wrapping needed
    generateFSharpCall(resolved->func, resolved->name, { elemReload });
    auto* boundResult = _result;
    if (!boundResult)
    {
        reportTypeError("Failed to evaluate function in Option.bind");
        return;
    }

    _builder.createStore(resultStorage, boundResult, "optbind.some.store");
    _builder.createBr(continueBlock);

    // Continue
    _builder.setInsertPoint(continueBlock);
    _result = _builder.createLoad(resultStorage, "optbind.result.load");
    annotateObjectTypeId(_result, CoreVM::BuiltinTypeId::Option);
}

void IRGenerator::generateOptionDefaultValue(std::vector<ast::Expr const*> const& argExprs)
{
    if (argExprs.size() != 2)
    {
        reportTypeError("Option.defaultValue requires 2 arguments: default value and option value");
        return;
    }
    // Evaluate the option value first (argExprs[1])
    auto* optionValue = codegen(argExprs[1]);
    if (!optionValue)
    {
        reportTypeError("Failed to evaluate option argument for Option.defaultValue");
        return;
    }
    generateOptionDefaultValueWithValue(argExprs[0], optionValue);
}

void IRGenerator::generateOptionDefaultValueWithValue(ast::Expr const* defaultExpr,
                                                      CoreVM::Value* optionValue)
{
    // Same IR pattern as ?| operator

    // Store option in alloca for cross-block access
    auto* objStorage = createAllocaInEntryBlock(optionValue->type(), "optdefval.obj");
    _builder.createStore(objStorage, optionValue, "optdefval.obj.store");

    // Extract tag
    auto* tag = _builder.createObjGetTag(optionValue, "optdefval.tag");
    auto* isSome =
        _builder.createNCmpEQ(tag, _builder.get(static_cast<CoreVM::CoreNumber>(1)), "optdefval.is_some");

    // Create blocks
    auto* someBlock = _builder.createBlock("optdefval.some");
    auto* noneBlock = _builder.createBlock("optdefval.none");
    auto* continueBlock = _builder.createBlock("optdefval.continue");

    _builder.createCondBr(isSome, someBlock, noneBlock);

    // None path first: evaluate default expression to get concrete type for deferred alloca
    _builder.setInsertPoint(noneBlock);
    auto* defaultVal = codegen(defaultExpr);
    if (!defaultVal)
    {
        reportTypeError("Failed to evaluate default value for Option.defaultValue");
        return;
    }

    // Create result storage with default expression's concrete type (deferred alloca pattern)
    auto* resultStorage = createAllocaInEntryBlock(defaultVal->type(), "optdefval.result");
    _builder.createStore(resultStorage, defaultVal, "optdefval.none.store");
    _builder.createBr(continueBlock);

    // Some path: extract inner value
    _builder.setInsertPoint(someBlock);
    auto* objReload = _builder.createLoad(objStorage, "optdefval.obj.reload");
    auto* innerValue = _builder.createObjGetSlot(
        objReload, _builder.get(static_cast<CoreVM::CoreNumber>(0)), "optdefval.inner");
    _builder.createStore(resultStorage, innerValue, "optdefval.some.store");
    _builder.createBr(continueBlock);

    // Continue
    _builder.setInsertPoint(continueBlock);
    _result = _builder.createLoad(resultStorage, "optdefval.result.load");
}

} // namespace endo
