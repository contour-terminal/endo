// SPDX-License-Identifier: Apache-2.0
// Higher-Order Function IR Generators
//
// This file contains the IRGenerator member functions for list higher-order functions
// (map, filter, fold, reduce, reverse, find, exists, forall, each, take, drop, zip,
// flatten, sort, distinct, sortBy, groupBy) and the dispatch method generateBuiltinHOFCall.
//
// These are IRGenerator member functions defined in a separate translation unit
// to reduce the size of IRGenerator.cpp.

#include <endo-language/codegen/IRGenerator.hpp>

#include <CoreVM/CoreVM.hpp>

namespace endo
{

void IRGenerator::applyHOFFunction(std::string const& funcParamName,
                                   FSharpFunction const* func,
                                   std::string const& funcName,
                                   std::vector<CoreVM::Value*> const& args,
                                   std::string const& label,
                                   std::optional<CoreVM::LiteralType> expectedReturnType)
{
    // Check if the function parameter is a Callable object (function-typed parameter
    // inside a compiled function body). If so, emit an indirect call via IUCALL.
    if (auto* storage = lookupFSharpVariable(funcParamName))
    {
        if (getObjectTypeId(storage).value_or(0) == CoreVM::BuiltinTypeId::Callable)
        {
            auto* callableVal = _builder.createLoad(storage, label + ".callable.load");
            _result = _builder.createIndirectCall(callableVal, args, label + ".iucall");

            // IndirectCallInstr returns Void type. Cast the result to the correct IR type
            // via store/load so downstream operations (toBool, convertToString) dispatch
            // correctly. Prefer the compiled function's return type; fall back to the
            // caller-specified expectedReturnType for Callable parameters where func
            // metadata is unavailable.
            auto returnType = (func && func->compiledReturnType != CoreVM::LiteralType::Void)
                                  ? func->compiledReturnType
                                  : expectedReturnType.value_or(CoreVM::LiteralType::Void);
            if (returnType != CoreVM::LiteralType::Void)
            {
                auto* castAlloca = createAllocaInEntryBlock(returnType, label + ".ret.cast");
                _builder.createStore(castAlloca, _result);
                _result = _builder.createLoad(castAlloca, label + ".ret.load");
            }

            // Propagate return annotations from the compiled function metadata
            if (func && func->compiledReturnInnerType)
                annotateInnerType(_result, *func->compiledReturnInnerType);
            if (func && func->compiledReturnObjectTypeId)
                annotateObjectTypeId(_result, *func->compiledReturnObjectTypeId);
            if (func && func->compiledReturnListElementTypeId)
                annotateListElementTypeId(_result, *func->compiledReturnListElementTypeId);
            if (func && func->compiledReturnListElementLiteralType)
                annotateListElementLiteralType(_result, *func->compiledReturnListElementLiteralType);
            return;
        }
    }

    // Fall back to static dispatch via generateFSharpCall
    generateFSharpCall(func, funcName, args);
}

void IRGenerator::generateBuiltinHOFCall(FSharpFunction const* func,
                                         std::string const& /*funcName*/,
                                         std::vector<CoreVM::Value*> const& args)
{
    // Set up scope: rebind captured variables (from partial application) and function refs
    pushFSharpScope();
    for (auto const& [capName, capStorage]: func->capturedBindings)
        bindFSharpVariable(capName, capStorage);
    for (auto const& [varName, targetFunc]: func->capturedFunctionRefs)
        _sema.scopes().bindFunctionRef(varName, targetFunc);

    // Bind explicit arguments to parameter names
    for (size_t i = 0; i < func->parameters.size(); ++i)
    {
        auto* storage = createAllocaInEntryBlock(args[i]->type(), func->parameters[i]);
        _builder.createStore(storage, args[i], func->parameters[i]);
        bindFSharpVariable(func->parameters[i], storage);

        // Propagate all type annotations through HOF parameter bindings
        propagateAllAnnotations(args[i], storage);

        // Track function references passed as arguments
        if (auto const* constStr = dynamic_cast<CoreVM::ConstantString*>(args[i]))
        {
            auto const& refName = constStr->get();
            if (lookupFSharpFunction(refName) || refName == "print" || refName == "println")
                _sema.scopes().bindFunctionRef(func->parameters[i], constStr->get());
        }
    }

    // Resolve actual function and list arguments from scope
    auto const& hofName = func->builtinHOF;

    // Helper: load a list parameter and propagate all type annotations
    auto loadListParam = [&](std::string_view paramName, std::string_view label) -> CoreVM::Value* {
        auto* storage = lookupFSharpVariable(std::string(paramName));
        auto* loaded = _builder.createLoad(storage, std::string(label));
        propagateAllAnnotations(storage, loaded);
        return loaded;
    };

    if (hofName == "map")
    {
        auto* listVal = loadListParam("__xs", "map.xs");
        generateMapIR("__f", listVal);
    }
    else if (hofName == "filter")
    {
        auto* listVal = loadListParam("__xs", "filter.xs");
        generateFilterIR("__pred", listVal);
    }
    else if (hofName == "fold")
    {
        auto* initVal = _builder.createLoad(lookupFSharpVariable("__init"), "fold.init");
        auto* listVal = loadListParam("__xs", "fold.xs");
        generateFoldIR(initVal, "__f", listVal);
    }
    else if (hofName == "reduce")
    {
        auto* listVal = loadListParam("__xs", "reduce.xs");
        generateReduceIR("__f", listVal);
    }
    else if (hofName == "reverse")
    {
        auto* listVal = loadListParam("__xs", "reverse.xs");
        generateReverseIR(listVal);
    }
    else if (hofName == "find")
    {
        auto* listVal = loadListParam("__xs", "find.xs");
        generateFindIR("__pred", listVal);
    }
    else if (hofName == "exists")
    {
        auto* listVal = loadListParam("__xs", "exists.xs");
        generateExistsIR("__pred", listVal);
    }
    else if (hofName == "forall")
    {
        auto* listVal = loadListParam("__xs", "forall.xs");
        generateForallIR("__pred", listVal);
    }
    else if (hofName == "each")
    {
        auto* listVal = loadListParam("__xs", "each.xs");
        if (getObjectTypeId(listVal) == CoreVM::BuiltinTypeId::Seq)
            generateSeqEachIR("__f", listVal);
        else
            generateEachIR("__f", listVal);
    }
    else if (hofName == "take")
    {
        auto* countVal = _builder.createLoad(lookupFSharpVariable("__n"), "take.n");
        auto* listVal = loadListParam("__xs", "take.xs");
        if (getObjectTypeId(listVal) == CoreVM::BuiltinTypeId::Seq)
            generateSeqTakeIR(countVal, listVal);
        else
            generateTakeIR(countVal, listVal);
    }
    else if (hofName == "drop")
    {
        auto* countVal = _builder.createLoad(lookupFSharpVariable("__n"), "drop.n");
        auto* listVal = loadListParam("__xs", "drop.xs");
        generateDropIR(countVal, listVal);
    }
    else if (hofName == "zip")
    {
        auto* listA = loadListParam("__xs", "zip.xs");
        auto* listB = loadListParam("__ys", "zip.ys");
        generateZipIR(listA, listB);
    }
    else if (hofName == "flatten")
    {
        auto* listVal = loadListParam("__xss", "flatten.xss");
        generateFlattenIR(listVal);
    }
    else if (hofName == "sortBy")
    {
        auto* listVal = loadListParam("__xs", "sortBy.xs");
        generateSortByIR("__f", listVal);
    }
    else if (hofName == "groupBy")
    {
        auto* listVal = loadListParam("__xs", "groupBy.xs");
        generateGroupByIR("__f", listVal);
    }
    else if (hofName == "sort")
    {
        auto* listVal = loadListParam("__xs", "sort.xs");
        generateSortIR(listVal);
    }
    else if (hofName == "distinct")
    {
        auto* listVal = loadListParam("__xs", "distinct.xs");
        generateDistinctIR(listVal);
    }
    else if (hofName == "toList")
    {
        auto* seqVal = loadListParam("__xs", "toList.xs");
        if (getObjectTypeId(seqVal) == CoreVM::BuiltinTypeId::List)
            _result = seqVal; // Already a List — identity, annotations preserved by loadListParam
        else
            generateToListIR(seqVal);
    }
    else
    {
        reportTypeError("Unknown builtin HOF: {}", std::string_view(hofName));
    }

    popFSharpScope();
}

void IRGenerator::generateMapIR(std::string const& funcParamName, CoreVM::Value* listValue)
{
    auto* tag1 = _builder.get(CoreVM::CoreNumber(1));  // Cons
    auto* slot0 = _builder.get(CoreVM::CoreNumber(0)); // head
    auto* slot1 = _builder.get(CoreVM::CoreNumber(1)); // tail

    // Resolve the function to call (may be null for Callable parameters)
    auto funcName = funcParamName;
    if (auto ref = lookupFSharpFunctionRef(funcParamName))
        funcName = *ref;
    auto const* func = lookupFSharpFunction(funcName);
    if (!func)
    {
        // Allow Callable parameters (function-typed) to proceed with indirect dispatch
        if (auto* storage = lookupFSharpVariable(funcParamName);
            !storage || getObjectTypeId(storage).value_or(0) != CoreVM::BuiltinTypeId::Callable)
        {
            reportTypeError("map: function argument '{}' not found", std::string_view(funcParamName));
            return;
        }
    }

    // Allocas for phase 1 (forward iteration building reversed accumulator)
    auto* srcStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "map.src");
    _builder.createStore(srcStorage, listValue);

    auto* accStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "map.acc");
    auto* nil = emitNilList(CoreVM::LiteralType::Void, "map.nil");
    _builder.createStore(accStorage, nil);

    auto const mapElemType = getListElementLiteralType(listValue).value_or(CoreVM::LiteralType::Number);
    auto* elemAlloca = createAllocaInEntryBlock(mapElemType, "map.elem");

    // Propagate list element type to extracted elements (e.g., ProcessInfo for ps)
    if (auto elemTypeId = getListElementTypeId(listValue))
        annotateObjectTypeId(elemAlloca, *elemTypeId);

    // Create blocks
    auto* condBlock = _builder.createBlock("map.cond");
    auto* bodyBlock = _builder.createBlock("map.body");
    auto* revInitBlock = _builder.createBlock("map.rev.init");
    auto* revCondBlock = _builder.createBlock("map.rev.cond");
    auto* revBodyBlock = _builder.createBlock("map.rev.body");
    auto* endBlock = _builder.createBlock("map.end");

    _builder.createBr(condBlock);

    // Phase 1: Check if source list is Cons
    _builder.setInsertPoint(condBlock);
    auto* srcLoad = _builder.createLoad(srcStorage, "map.src.load");
    auto* srcTag = _builder.createObjGetTag(srcLoad, "map.src.tag");
    auto* isCons = _builder.createNCmpEQ(srcTag, tag1, "map.is_cons");
    _builder.createCondBr(isCons, bodyBlock, revInitBlock);

    // Phase 1: Extract head, apply function, cons onto reversed accumulator
    _builder.setInsertPoint(bodyBlock);
    auto* srcForHead = _builder.createLoad(srcStorage, "map.src.for_head");
    auto* head = _builder.createObjGetSlot(srcForHead, slot0, "map.head");
    _builder.createStore(elemAlloca, head);

    // Advance source cursor to tail (separate load)
    auto* srcForTail = _builder.createLoad(srcStorage, "map.src.for_tail");
    auto* tail = _builder.createObjGetSlot(srcForTail, slot1, "map.tail");
    _builder.createStore(srcStorage, tail);

    // Apply function to element
    auto* elemLoad = _builder.createLoad(elemAlloca, "map.elem.load");
    // Propagate element type annotation through load (for field access in lambda body)
    if (auto elemTypeId = getObjectTypeId(elemAlloca))
        annotateObjectTypeId(elemLoad, *elemTypeId);
    applyHOFFunction(funcParamName, func, funcName, { elemLoad }, "map");
    auto* mapped = _result;
    if (!mapped)
    {
        reportTypeError("map: failed to apply function to element");
        return;
    }

    // Capture the mapped element's type info for annotating the output list.
    // resolveObjectTypeId checks annotations first, then traces the IR chain as fallback
    // (needed because emitTuple2/emitTuple3 don't annotate their results).
    auto const mappedLiteralType = mapped->type();
    auto const mappedInnerType = getInnerType(mapped);
    auto mappedObjTypeId = resolveObjectTypeId(mapped);

    // Store mapped value and accumulator in temp allocas to survive ObjAlloc
    auto* mappedTmp = createAllocaInEntryBlock(mapped->type(), "map.mapped.tmp");
    _builder.createStore(mappedTmp, mapped);
    auto* accTmp = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "map.acc.tmp");
    auto* accForCons = _builder.createLoad(accStorage, "map.acc.for_cons");
    _builder.createStore(accTmp, accForCons);

    // Create Cons cell: tag=1, slot[0]=mapped, slot[1]=oldAcc
    auto* mappedReload = _builder.createLoad(mappedTmp, "map.mapped.reload");
    auto* accReload = _builder.createLoad(accTmp, "map.acc.reload");
    auto* cons = emitListCons(mappedReload, accReload, mappedLiteralType, "map.cons");

    _builder.createStore(accStorage, cons);
    _builder.createBr(condBlock);

    // Phase 2: Reverse the accumulated list
    auto* revSrcStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "map.rev.src");
    auto* revAccStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "map.rev.acc");

    _builder.setInsertPoint(revInitBlock);
    auto* revSrcInit = _builder.createLoad(accStorage, "map.rev.src.init");
    _builder.createStore(revSrcStorage, revSrcInit);
    auto* revNil = emitNilList(CoreVM::LiteralType::Void, "map.rev.nil");
    _builder.createStore(revAccStorage, revNil);
    _builder.createBr(revCondBlock);

    _builder.setInsertPoint(revCondBlock);
    auto* revSrcLoad = _builder.createLoad(revSrcStorage, "map.rev.src.load");
    auto* revSrcTag = _builder.createObjGetTag(revSrcLoad, "map.rev.src.tag");
    auto* revIsCons = _builder.createNCmpEQ(revSrcTag, tag1, "map.rev.is_cons");
    _builder.createCondBr(revIsCons, revBodyBlock, endBlock);

    _builder.setInsertPoint(revBodyBlock);
    auto* revSrcForHead = _builder.createLoad(revSrcStorage, "map.rev.src.for_head");
    auto* revHead = _builder.createObjGetSlot(revSrcForHead, slot0, "map.rev.head");
    auto* revSrcForTail = _builder.createLoad(revSrcStorage, "map.rev.src.for_tail");
    auto* revTail = _builder.createObjGetSlot(revSrcForTail, slot1, "map.rev.tail");
    _builder.createStore(revSrcStorage, revTail);

    auto* revElemTmp = createAllocaInEntryBlock(mappedLiteralType, "map.rev.elem.tmp");
    _builder.createStore(revElemTmp, revHead);
    auto* revAccTmp = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "map.rev.acc.tmp");
    auto* revAccForCons = _builder.createLoad(revAccStorage, "map.rev.acc.for_cons");
    _builder.createStore(revAccTmp, revAccForCons);

    auto* revElemReload = _builder.createLoad(revElemTmp, "map.rev.elem.reload");
    auto* revAccReload = _builder.createLoad(revAccTmp, "map.rev.acc.reload");
    auto* revCons = emitListCons(revElemReload, revAccReload, mappedLiteralType, "map.rev.cons");

    _builder.createStore(revAccStorage, revCons);
    _builder.createBr(revCondBlock);

    _builder.setInsertPoint(endBlock);
    _result = _builder.createLoad(revAccStorage, "map.result");
    annotateObjectTypeId(_result, CoreVM::BuiltinTypeId::List);
    // Annotate the output list with the mapped element type (may differ from input)
    if (mappedObjTypeId)
        annotateListElementTypeId(_result, *mappedObjTypeId);
    annotateListElementLiteralType(_result, mappedLiteralType);
    if (mappedInnerType)
        annotateListElementInnerType(_result, *mappedInnerType);
}

void IRGenerator::generateFilterIR(std::string const& predParamName, CoreVM::Value* listValue)
{
    auto* tag1 = _builder.get(CoreVM::CoreNumber(1));  // Cons
    auto* slot0 = _builder.get(CoreVM::CoreNumber(0)); // head
    auto* slot1 = _builder.get(CoreVM::CoreNumber(1)); // tail

    // Resolve the predicate function (may be null for Callable parameters)
    auto predName = predParamName;
    if (auto ref = lookupFSharpFunctionRef(predParamName))
        predName = *ref;
    auto const* pred = lookupFSharpFunction(predName);
    if (!pred)
    {
        if (auto* storage = lookupFSharpVariable(predParamName);
            !storage || getObjectTypeId(storage).value_or(0) != CoreVM::BuiltinTypeId::Callable)
        {
            reportTypeError("filter: predicate argument '{}' not found", std::string_view(predParamName));
            return;
        }
    }

    // Allocas
    auto* srcStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "filter.src");
    _builder.createStore(srcStorage, listValue);

    auto* accStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "filter.acc");
    auto* nil = emitNilList(CoreVM::LiteralType::Void, "filter.nil");
    _builder.createStore(accStorage, nil);

    auto const filterElemType = getListElementLiteralType(listValue).value_or(CoreVM::LiteralType::Number);
    auto* elemAlloca = createAllocaInEntryBlock(filterElemType, "filter.elem");

    // Propagate list element type to extracted elements (e.g., ProcessInfo for ps)
    if (auto elemTypeId = getListElementTypeId(listValue))
    {
        annotateObjectTypeId(elemAlloca, *elemTypeId);
        // Also annotate srcStorage so the result list inherits element type
        annotateListElementTypeId(srcStorage, *elemTypeId);
    }
    if (auto elt = getListElementLiteralType(listValue))
        annotateListElementLiteralType(srcStorage, *elt);
    if (auto innerType = getListElementInnerType(listValue))
    {
        annotateInnerType(elemAlloca, *innerType);
        annotateListElementInnerType(srcStorage, *innerType);
    }

    // Create blocks
    auto* condBlock = _builder.createBlock("filter.cond");
    auto* bodyBlock = _builder.createBlock("filter.body");
    auto* consBlock = _builder.createBlock("filter.cons");
    auto* revInitBlock = _builder.createBlock("filter.rev.init");
    auto* revCondBlock = _builder.createBlock("filter.rev.cond");
    auto* revBodyBlock = _builder.createBlock("filter.rev.body");
    auto* endBlock = _builder.createBlock("filter.end");

    _builder.createBr(condBlock);

    // Phase 1: Check if source list is Cons
    _builder.setInsertPoint(condBlock);
    auto* srcLoad = _builder.createLoad(srcStorage, "filter.src.load");
    auto* srcTag = _builder.createObjGetTag(srcLoad, "filter.src.tag");
    auto* isCons = _builder.createNCmpEQ(srcTag, tag1, "filter.is_cons");
    _builder.createCondBr(isCons, bodyBlock, revInitBlock);

    // Phase 1: Extract head, apply predicate
    _builder.setInsertPoint(bodyBlock);
    auto* srcForHead = _builder.createLoad(srcStorage, "filter.src.for_head");
    auto* head = _builder.createObjGetSlot(srcForHead, slot0, "filter.head");
    _builder.createStore(elemAlloca, head);

    // Advance source cursor to tail
    auto* srcForTail = _builder.createLoad(srcStorage, "filter.src.for_tail");
    auto* tail = _builder.createObjGetSlot(srcForTail, slot1, "filter.tail");
    _builder.createStore(srcStorage, tail);

    // Apply predicate to element
    auto* elemLoad = _builder.createLoad(elemAlloca, "filter.elem.load");
    // Propagate element type annotation through load (for field access in lambda body)
    if (auto elemTypeId = getObjectTypeId(elemAlloca))
        annotateObjectTypeId(elemLoad, *elemTypeId);
    applyHOFFunction(predParamName, pred, predName, { elemLoad }, "filter", CoreVM::LiteralType::Boolean);
    auto* predResult = _result;
    if (!predResult)
    {
        reportTypeError("filter: failed to apply predicate to element");
        return;
    }
    _builder.createCondBr(toBool(predResult), consBlock, condBlock);

    // Phase 1: Cons block — element passed filter, prepend to accumulator
    _builder.setInsertPoint(consBlock);
    auto* elemForCons = _builder.createLoad(elemAlloca, "filter.elem.for_cons");
    auto* accTmp = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "filter.acc.tmp");
    auto* accForCons = _builder.createLoad(accStorage, "filter.acc.for_cons");
    _builder.createStore(accTmp, accForCons);
    auto* elemTmp = createAllocaInEntryBlock(filterElemType, "filter.elem.tmp");
    _builder.createStore(elemTmp, elemForCons);

    auto* elemReload = _builder.createLoad(elemTmp, "filter.elem.reload");
    auto* accReload = _builder.createLoad(accTmp, "filter.acc.reload");
    auto* cons = emitListCons(elemReload, accReload, CoreVM::LiteralType::Void, "filter.cons");

    _builder.createStore(accStorage, cons);
    _builder.createBr(condBlock);

    // Phase 2: Reverse the accumulated list
    auto* revSrcStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "filter.rev.src");
    auto* revAccStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "filter.rev.acc");

    _builder.setInsertPoint(revInitBlock);
    auto* revSrcInit = _builder.createLoad(accStorage, "filter.rev.src.init");
    _builder.createStore(revSrcStorage, revSrcInit);
    auto* revNil = emitNilList(CoreVM::LiteralType::Void, "filter.rev.nil");
    _builder.createStore(revAccStorage, revNil);
    _builder.createBr(revCondBlock);

    _builder.setInsertPoint(revCondBlock);
    auto* revSrcLoad = _builder.createLoad(revSrcStorage, "filter.rev.src.load");
    auto* revSrcTag = _builder.createObjGetTag(revSrcLoad, "filter.rev.src.tag");
    auto* revIsCons = _builder.createNCmpEQ(revSrcTag, tag1, "filter.rev.is_cons");
    _builder.createCondBr(revIsCons, revBodyBlock, endBlock);

    _builder.setInsertPoint(revBodyBlock);
    auto* revSrcForHead = _builder.createLoad(revSrcStorage, "filter.rev.src.for_head");
    auto* revHead = _builder.createObjGetSlot(revSrcForHead, slot0, "filter.rev.head");
    auto* revSrcForTail = _builder.createLoad(revSrcStorage, "filter.rev.src.for_tail");
    auto* revTail = _builder.createObjGetSlot(revSrcForTail, slot1, "filter.rev.tail");
    _builder.createStore(revSrcStorage, revTail);

    auto* revElemTmp = createAllocaInEntryBlock(CoreVM::LiteralType::Void, "filter.rev.elem.tmp");
    _builder.createStore(revElemTmp, revHead);
    auto* revAccTmp = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "filter.rev.acc.tmp");
    auto* revAccForCons = _builder.createLoad(revAccStorage, "filter.rev.acc.for_cons");
    _builder.createStore(revAccTmp, revAccForCons);

    auto* revElemReload = _builder.createLoad(revElemTmp, "filter.rev.elem.reload");
    auto* revAccReload = _builder.createLoad(revAccTmp, "filter.rev.acc.reload");
    auto* revCons = emitListCons(revElemReload, revAccReload, CoreVM::LiteralType::Void, "filter.rev.cons");

    _builder.createStore(revAccStorage, revCons);
    _builder.createBr(revCondBlock);

    _builder.setInsertPoint(endBlock);
    _result = _builder.createLoad(revAccStorage, "filter.result");
    annotateObjectTypeId(_result, CoreVM::BuiltinTypeId::List);
    // Propagate list element type through filter (same element type as input)
    if (auto elemTypeId = getListElementTypeId(listValue))
        annotateListElementTypeId(_result, *elemTypeId);
    if (auto elt = getListElementLiteralType(listValue))
        annotateListElementLiteralType(_result, *elt);
    if (auto innerType = getListElementInnerType(listValue))
        annotateListElementInnerType(_result, *innerType);
}

void IRGenerator::generateFoldIR(CoreVM::Value* initValue,
                                 std::string const& funcParamName,
                                 CoreVM::Value* listValue)
{
    auto* tag1 = _builder.get(CoreVM::CoreNumber(1));  // Cons
    auto* slot0 = _builder.get(CoreVM::CoreNumber(0)); // head
    auto* slot1 = _builder.get(CoreVM::CoreNumber(1)); // tail

    // Resolve the function to call (may be null for Callable parameters)
    auto funcName = funcParamName;
    if (auto ref = lookupFSharpFunctionRef(funcParamName))
        funcName = *ref;
    auto const* func = lookupFSharpFunction(funcName);
    if (!func)
    {
        if (auto* storage = lookupFSharpVariable(funcParamName);
            !storage || getObjectTypeId(storage).value_or(0) != CoreVM::BuiltinTypeId::Callable)
        {
            reportTypeError("fold: function argument '{}' not found", std::string_view(funcParamName));
            return;
        }
    }

    // Allocas
    auto* srcStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "fold.src");
    _builder.createStore(srcStorage, listValue);

    auto* accStorage = createAllocaInEntryBlock(initValue->type(), "fold.acc");
    _builder.createStore(accStorage, initValue);

    auto const foldElemType = getListElementLiteralType(listValue).value_or(CoreVM::LiteralType::Number);
    auto* elemAlloca = createAllocaInEntryBlock(foldElemType, "fold.elem");

    // Propagate list element type to extracted elements
    if (auto elemTypeId = getListElementTypeId(listValue))
        annotateObjectTypeId(elemAlloca, *elemTypeId);

    // Create blocks
    auto* condBlock = _builder.createBlock("fold.cond");
    auto* bodyBlock = _builder.createBlock("fold.body");
    auto* endBlock = _builder.createBlock("fold.end");

    _builder.createBr(condBlock);

    // Condition: check if source list is Cons
    _builder.setInsertPoint(condBlock);
    auto* srcLoad = _builder.createLoad(srcStorage, "fold.src.load");
    auto* srcTag = _builder.createObjGetTag(srcLoad, "fold.src.tag");
    auto* isCons = _builder.createNCmpEQ(srcTag, tag1, "fold.is_cons");
    _builder.createCondBr(isCons, bodyBlock, endBlock);

    // Body: extract head, apply function(acc, elem), update acc
    _builder.setInsertPoint(bodyBlock);
    auto* srcForHead = _builder.createLoad(srcStorage, "fold.src.for_head");
    auto* head = _builder.createObjGetSlot(srcForHead, slot0, "fold.head");
    _builder.createStore(elemAlloca, head);

    // Advance source cursor
    auto* srcForTail = _builder.createLoad(srcStorage, "fold.src.for_tail");
    auto* tail = _builder.createObjGetSlot(srcForTail, slot1, "fold.tail");
    _builder.createStore(srcStorage, tail);

    // Apply function to (acc, elem)
    auto* accLoad = _builder.createLoad(accStorage, "fold.acc.load");
    auto* elemLoad = _builder.createLoad(elemAlloca, "fold.elem.load");
    if (auto elemTypeId = getObjectTypeId(elemAlloca))
        annotateObjectTypeId(elemLoad, *elemTypeId);
    applyHOFFunction(funcParamName, func, funcName, { accLoad, elemLoad }, "fold");
    auto* newAcc = _result;
    if (!newAcc)
    {
        reportTypeError("fold: failed to apply function");
        return;
    }
    _builder.createStore(accStorage, newAcc);
    _builder.createBr(condBlock);

    // End: return final accumulator
    _builder.setInsertPoint(endBlock);
    _result = _builder.createLoad(accStorage, "fold.result");
}

void IRGenerator::generateReduceIR(std::string const& funcParamName, CoreVM::Value* listValue)
{
    auto* tag1 = _builder.get(CoreVM::CoreNumber(1));  // Cons
    auto* slot0 = _builder.get(CoreVM::CoreNumber(0)); // head
    auto* slot1 = _builder.get(CoreVM::CoreNumber(1)); // tail

    // Resolve the function to call (may be null for Callable parameters)
    auto funcName = funcParamName;
    if (auto ref = lookupFSharpFunctionRef(funcParamName))
        funcName = *ref;
    auto const* func = lookupFSharpFunction(funcName);
    if (!func)
    {
        if (auto* storage = lookupFSharpVariable(funcParamName);
            !storage || getObjectTypeId(storage).value_or(0) != CoreVM::BuiltinTypeId::Callable)
        {
            reportTypeError("reduce: function argument '{}' not found", std::string_view(funcParamName));
            return;
        }
    }

    // Allocas
    auto* srcStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "reduce.src");
    _builder.createStore(srcStorage, listValue);

    auto const reduceElemType = getListElementLiteralType(listValue).value_or(CoreVM::LiteralType::Number);
    auto* accStorage = createAllocaInEntryBlock(reduceElemType, "reduce.acc");
    auto* resultStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "reduce.result");
    auto* elemAlloca = createAllocaInEntryBlock(reduceElemType, "reduce.elem");

    // Propagate list element type to extracted elements
    if (auto elemTypeId = getListElementTypeId(listValue))
        annotateObjectTypeId(elemAlloca, *elemTypeId);

    // Create blocks
    auto* checkEmptyBlock = _builder.createBlock("reduce.check_empty");
    auto* initBlock = _builder.createBlock("reduce.init");
    auto* condBlock = _builder.createBlock("reduce.cond");
    auto* bodyBlock = _builder.createBlock("reduce.body");
    auto* noneBlock = _builder.createBlock("reduce.none");
    auto* someBlock = _builder.createBlock("reduce.some");
    auto* endBlock = _builder.createBlock("reduce.end");

    _builder.createBr(checkEmptyBlock);

    // Check if list is empty
    _builder.setInsertPoint(checkEmptyBlock);
    auto* srcLoad = _builder.createLoad(srcStorage, "reduce.src.load");
    auto* srcTag = _builder.createObjGetTag(srcLoad, "reduce.src.tag");
    auto* isCons = _builder.createNCmpEQ(srcTag, tag1, "reduce.is_cons");
    _builder.createCondBr(isCons, initBlock, noneBlock);

    // Empty list → return None
    _builder.setInsertPoint(noneBlock);
    auto* noneVal = emitNoneOption("reduce.none");
    _builder.createStore(resultStorage, noneVal);
    _builder.createBr(endBlock);

    // Non-empty: use first element as initial accumulator
    _builder.setInsertPoint(initBlock);
    auto* srcForHead = _builder.createLoad(srcStorage, "reduce.src.for_head");
    auto* firstElem = _builder.createObjGetSlot(srcForHead, slot0, "reduce.first");
    _builder.createStore(accStorage, firstElem);

    // Advance to tail
    auto* srcForTail = _builder.createLoad(srcStorage, "reduce.src.for_tail");
    auto* tailVal = _builder.createObjGetSlot(srcForTail, slot1, "reduce.tail");
    _builder.createStore(srcStorage, tailVal);
    _builder.createBr(condBlock);

    // Fold loop: condition
    _builder.setInsertPoint(condBlock);
    auto* srcLoad2 = _builder.createLoad(srcStorage, "reduce.src.load2");
    auto* srcTag2 = _builder.createObjGetTag(srcLoad2, "reduce.src.tag2");
    auto* isCons2 = _builder.createNCmpEQ(srcTag2, tag1, "reduce.is_cons2");
    _builder.createCondBr(isCons2, bodyBlock, someBlock);

    // Fold loop: body
    _builder.setInsertPoint(bodyBlock);
    auto* srcForHead2 = _builder.createLoad(srcStorage, "reduce.src.for_head2");
    auto* head = _builder.createObjGetSlot(srcForHead2, slot0, "reduce.head");
    _builder.createStore(elemAlloca, head);

    auto* srcForTail2 = _builder.createLoad(srcStorage, "reduce.src.for_tail2");
    auto* tail2 = _builder.createObjGetSlot(srcForTail2, slot1, "reduce.tail2");
    _builder.createStore(srcStorage, tail2);

    auto* accLoad = _builder.createLoad(accStorage, "reduce.acc.load");
    auto* elemLoad = _builder.createLoad(elemAlloca, "reduce.elem.load");
    if (auto elemTypeId = getObjectTypeId(elemAlloca))
        annotateObjectTypeId(elemLoad, *elemTypeId);
    applyHOFFunction(funcParamName, func, funcName, { accLoad, elemLoad }, "reduce");
    auto* newAcc = _result;
    if (!newAcc)
    {
        reportTypeError("reduce: failed to apply function");
        return;
    }
    _builder.createStore(accStorage, newAcc);
    _builder.createBr(condBlock);

    // Wrap result in Some
    _builder.setInsertPoint(someBlock);
    auto* finalAcc = _builder.createLoad(accStorage, "reduce.final_acc");
    auto* accTmp = createAllocaInEntryBlock(reduceElemType, "reduce.acc.tmp");
    _builder.createStore(accTmp, finalAcc);

    auto* accReload = _builder.createLoad(accTmp, "reduce.acc.reload");
    auto* someVal = emitSomeOption(accReload, CoreVM::LiteralType::Void, "reduce.some");

    _builder.createStore(resultStorage, someVal);
    _builder.createBr(endBlock);

    // End: return result
    _builder.setInsertPoint(endBlock);
    _result = _builder.createLoad(resultStorage, "reduce.result");
    annotateObjectTypeId(_result, CoreVM::BuiltinTypeId::Option);
}

void IRGenerator::generateReverseIR(CoreVM::Value* listValue)
{
    auto* tag1 = _builder.get(CoreVM::CoreNumber(1));  // Cons
    auto* slot0 = _builder.get(CoreVM::CoreNumber(0)); // head
    auto* slot1 = _builder.get(CoreVM::CoreNumber(1)); // tail

    // Allocas
    auto* srcStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "rev.src");
    _builder.createStore(srcStorage, listValue);

    auto* accStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "rev.acc");
    auto* nil = emitNilList(CoreVM::LiteralType::Void, "rev.nil");
    _builder.createStore(accStorage, nil);

    // Create blocks
    auto* condBlock = _builder.createBlock("rev.cond");
    auto* bodyBlock = _builder.createBlock("rev.body");
    auto* endBlock = _builder.createBlock("rev.end");

    _builder.createBr(condBlock);

    // Condition: check if source list is Cons
    _builder.setInsertPoint(condBlock);
    auto* srcLoad = _builder.createLoad(srcStorage, "rev.src.load");
    auto* srcTag = _builder.createObjGetTag(srcLoad, "rev.src.tag");
    auto* isCons = _builder.createNCmpEQ(srcTag, tag1, "rev.is_cons");
    _builder.createCondBr(isCons, bodyBlock, endBlock);

    // Body: extract head, cons onto accumulator (naturally reverses)
    _builder.setInsertPoint(bodyBlock);
    auto* srcForHead = _builder.createLoad(srcStorage, "rev.src.for_head");
    auto* head = _builder.createObjGetSlot(srcForHead, slot0, "rev.head");

    // Advance source cursor
    auto* srcForTail = _builder.createLoad(srcStorage, "rev.src.for_tail");
    auto* tail = _builder.createObjGetSlot(srcForTail, slot1, "rev.tail");
    _builder.createStore(srcStorage, tail);

    // Store head and acc in temp allocas to survive ObjAlloc
    auto* elemTmp = createAllocaInEntryBlock(CoreVM::LiteralType::Void, "rev.elem.tmp");
    _builder.createStore(elemTmp, head);
    auto* accTmp = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "rev.acc.tmp");
    auto* accForCons = _builder.createLoad(accStorage, "rev.acc.for_cons");
    _builder.createStore(accTmp, accForCons);

    // Create Cons cell: tag=1, slot[0]=head, slot[1]=oldAcc
    auto* elemReload = _builder.createLoad(elemTmp, "rev.elem.reload");
    auto* accReload = _builder.createLoad(accTmp, "rev.acc.reload");
    auto* cons = emitListCons(elemReload, accReload, CoreVM::LiteralType::Void, "rev.cons");

    _builder.createStore(accStorage, cons);
    _builder.createBr(condBlock);

    // End: return reversed list
    _builder.setInsertPoint(endBlock);
    _result = _builder.createLoad(accStorage, "rev.result");
    annotateObjectTypeId(_result, CoreVM::BuiltinTypeId::List);
    if (auto elemTypeId = getListElementTypeId(listValue))
        annotateListElementTypeId(_result, *elemTypeId);
    if (auto elt = getListElementLiteralType(listValue))
        annotateListElementLiteralType(_result, *elt);
}

void IRGenerator::generateFindIR(std::string const& predParamName, CoreVM::Value* listValue)
{
    auto* tag1 = _builder.get(CoreVM::CoreNumber(1));  // Cons
    auto* slot0 = _builder.get(CoreVM::CoreNumber(0)); // head
    auto* slot1 = _builder.get(CoreVM::CoreNumber(1)); // tail

    // Resolve predicate function
    auto predName = predParamName;
    if (auto ref = lookupFSharpFunctionRef(predParamName))
        predName = *ref;
    auto const* pred = lookupFSharpFunction(predName);
    if (!pred)
    {
        reportTypeError("find: predicate argument '{}' not found", std::string_view(predParamName));
        return;
    }

    // Allocas
    auto* srcStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "find.src");
    _builder.createStore(srcStorage, listValue);
    auto const findElemType = getListElementLiteralType(listValue).value_or(CoreVM::LiteralType::Number);
    auto* elemAlloca = createAllocaInEntryBlock(findElemType, "find.elem");
    auto* resultStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "find.result");

    // Propagate list element type to extracted elements
    if (auto elemTypeId = getListElementTypeId(listValue))
        annotateObjectTypeId(elemAlloca, *elemTypeId);

    // Create blocks
    auto* condBlock = _builder.createBlock("find.cond");
    auto* bodyBlock = _builder.createBlock("find.body");
    auto* foundBlock = _builder.createBlock("find.found");
    auto* noneBlock = _builder.createBlock("find.none");
    auto* endBlock = _builder.createBlock("find.end");

    _builder.createBr(condBlock);

    // Condition: check if source list is Cons
    _builder.setInsertPoint(condBlock);
    auto* srcLoad = _builder.createLoad(srcStorage, "find.src.load");
    auto* srcTag = _builder.createObjGetTag(srcLoad, "find.src.tag");
    auto* isCons = _builder.createNCmpEQ(srcTag, tag1, "find.is_cons");
    _builder.createCondBr(isCons, bodyBlock, noneBlock);

    // Body: extract head, apply predicate
    _builder.setInsertPoint(bodyBlock);
    auto* srcForHead = _builder.createLoad(srcStorage, "find.src.for_head");
    auto* head = _builder.createObjGetSlot(srcForHead, slot0, "find.head");
    _builder.createStore(elemAlloca, head);

    // Advance source cursor to tail
    auto* srcForTail = _builder.createLoad(srcStorage, "find.src.for_tail");
    auto* tail = _builder.createObjGetSlot(srcForTail, slot1, "find.tail");
    _builder.createStore(srcStorage, tail);

    // Apply predicate to element
    auto* elemLoad = _builder.createLoad(elemAlloca, "find.elem.load");
    if (auto elemTypeId = getObjectTypeId(elemAlloca))
        annotateObjectTypeId(elemLoad, *elemTypeId);
    generateFSharpCall(pred, predName, { elemLoad });
    auto* predResult = _result;
    if (!predResult)
    {
        reportTypeError("find: failed to apply predicate to element");
        return;
    }
    _builder.createCondBr(toBool(predResult), foundBlock, condBlock);

    // Found: wrap element in Some
    _builder.setInsertPoint(foundBlock);
    auto* foundElem = _builder.createLoad(elemAlloca, "find.found.elem");
    auto* elemTmp = createAllocaInEntryBlock(findElemType, "find.elem.tmp");
    _builder.createStore(elemTmp, foundElem);

    auto* elemReload = _builder.createLoad(elemTmp, "find.elem.reload");
    auto* someVal = emitSomeOption(elemReload, CoreVM::LiteralType::Void, "find.some");
    _builder.createStore(resultStorage, someVal);
    _builder.createBr(endBlock);

    // None: list exhausted
    _builder.setInsertPoint(noneBlock);
    auto* noneVal = emitNoneOption("find.none");
    _builder.createStore(resultStorage, noneVal);
    _builder.createBr(endBlock);

    // End
    _builder.setInsertPoint(endBlock);
    _result = _builder.createLoad(resultStorage, "find.result");
    annotateObjectTypeId(_result, CoreVM::BuiltinTypeId::Option);
    if (auto elemTypeId = getListElementTypeId(listValue))
        annotateInnerObjectTypeId(_result, *elemTypeId);
}

void IRGenerator::generateExistsIR(std::string const& predParamName, CoreVM::Value* listValue)
{
    auto* tag1 = _builder.get(CoreVM::CoreNumber(1));  // Cons
    auto* slot0 = _builder.get(CoreVM::CoreNumber(0)); // head
    auto* slot1 = _builder.get(CoreVM::CoreNumber(1)); // tail

    // Resolve predicate function
    auto predName = predParamName;
    if (auto ref = lookupFSharpFunctionRef(predParamName))
        predName = *ref;
    auto const* pred = lookupFSharpFunction(predName);
    if (!pred)
    {
        reportTypeError("exists: predicate argument '{}' not found", std::string_view(predParamName));
        return;
    }

    // Allocas
    auto* srcStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "exists.src");
    _builder.createStore(srcStorage, listValue);
    auto const existsElemType = getListElementLiteralType(listValue).value_or(CoreVM::LiteralType::Number);
    auto* elemAlloca = createAllocaInEntryBlock(existsElemType, "exists.elem");
    auto* resultStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Boolean, "exists.result");

    // Propagate list element type to extracted elements
    if (auto elemTypeId = getListElementTypeId(listValue))
        annotateObjectTypeId(elemAlloca, *elemTypeId);

    // Create blocks
    auto* condBlock = _builder.createBlock("exists.cond");
    auto* bodyBlock = _builder.createBlock("exists.body");
    auto* trueBlock = _builder.createBlock("exists.true");
    auto* falseBlock = _builder.createBlock("exists.false");
    auto* endBlock = _builder.createBlock("exists.end");

    _builder.createBr(condBlock);

    // Condition: check if source list is Cons
    _builder.setInsertPoint(condBlock);
    auto* srcLoad = _builder.createLoad(srcStorage, "exists.src.load");
    auto* srcTag = _builder.createObjGetTag(srcLoad, "exists.src.tag");
    auto* isCons = _builder.createNCmpEQ(srcTag, tag1, "exists.is_cons");
    _builder.createCondBr(isCons, bodyBlock, falseBlock);

    // Body: extract head, apply predicate
    _builder.setInsertPoint(bodyBlock);
    auto* srcForHead = _builder.createLoad(srcStorage, "exists.src.for_head");
    auto* head = _builder.createObjGetSlot(srcForHead, slot0, "exists.head");
    _builder.createStore(elemAlloca, head);

    auto* srcForTail = _builder.createLoad(srcStorage, "exists.src.for_tail");
    auto* tail = _builder.createObjGetSlot(srcForTail, slot1, "exists.tail");
    _builder.createStore(srcStorage, tail);

    auto* elemLoad = _builder.createLoad(elemAlloca, "exists.elem.load");
    if (auto elemTypeId = getObjectTypeId(elemAlloca))
        annotateObjectTypeId(elemLoad, *elemTypeId);
    generateFSharpCall(pred, predName, { elemLoad });
    auto* predResult = _result;
    if (!predResult)
    {
        reportTypeError("exists: failed to apply predicate to element");
        return;
    }
    _builder.createCondBr(toBool(predResult), trueBlock, condBlock);

    // True: predicate matched
    _builder.setInsertPoint(trueBlock);
    _builder.createStore(resultStorage, _builder.getBoolean(true));
    _builder.createBr(endBlock);

    // False: list exhausted
    _builder.setInsertPoint(falseBlock);
    _builder.createStore(resultStorage, _builder.getBoolean(false));
    _builder.createBr(endBlock);

    // End
    _builder.setInsertPoint(endBlock);
    _result = _builder.createLoad(resultStorage, "exists.result");
}

void IRGenerator::generateForallIR(std::string const& predParamName, CoreVM::Value* listValue)
{
    auto* tag1 = _builder.get(CoreVM::CoreNumber(1));  // Cons
    auto* slot0 = _builder.get(CoreVM::CoreNumber(0)); // head
    auto* slot1 = _builder.get(CoreVM::CoreNumber(1)); // tail

    // Resolve predicate function
    auto predName = predParamName;
    if (auto ref = lookupFSharpFunctionRef(predParamName))
        predName = *ref;
    auto const* pred = lookupFSharpFunction(predName);
    if (!pred)
    {
        reportTypeError("forall: predicate argument '{}' not found", std::string_view(predParamName));
        return;
    }

    // Allocas
    auto* srcStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "forall.src");
    _builder.createStore(srcStorage, listValue);
    auto const forallElemType = getListElementLiteralType(listValue).value_or(CoreVM::LiteralType::Number);
    auto* elemAlloca = createAllocaInEntryBlock(forallElemType, "forall.elem");
    auto* resultStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Boolean, "forall.result");

    // Propagate list element type to extracted elements
    if (auto elemTypeId = getListElementTypeId(listValue))
        annotateObjectTypeId(elemAlloca, *elemTypeId);

    // Create blocks
    auto* condBlock = _builder.createBlock("forall.cond");
    auto* bodyBlock = _builder.createBlock("forall.body");
    auto* emptyTrueBlock = _builder.createBlock("forall.true");
    auto* falseBlock = _builder.createBlock("forall.false");
    auto* endBlock = _builder.createBlock("forall.end");

    _builder.createBr(condBlock);

    // Condition: check if source list is Cons
    _builder.setInsertPoint(condBlock);
    auto* srcLoad = _builder.createLoad(srcStorage, "forall.src.load");
    auto* srcTag = _builder.createObjGetTag(srcLoad, "forall.src.tag");
    auto* isCons = _builder.createNCmpEQ(srcTag, tag1, "forall.is_cons");
    // isCons=true → bodyBlock (process element), isCons=false → emptyTrueBlock (vacuous truth)
    _builder.createCondBr(isCons, bodyBlock, emptyTrueBlock); // NOLINT(readability-suspicious-call-argument)

    // Body: extract head, apply predicate
    _builder.setInsertPoint(bodyBlock);
    auto* srcForHead = _builder.createLoad(srcStorage, "forall.src.for_head");
    auto* head = _builder.createObjGetSlot(srcForHead, slot0, "forall.head");
    _builder.createStore(elemAlloca, head);

    auto* srcForTail = _builder.createLoad(srcStorage, "forall.src.for_tail");
    auto* tail = _builder.createObjGetSlot(srcForTail, slot1, "forall.tail");
    _builder.createStore(srcStorage, tail);

    auto* elemLoad = _builder.createLoad(elemAlloca, "forall.elem.load");
    if (auto elemTypeId = getObjectTypeId(elemAlloca))
        annotateObjectTypeId(elemLoad, *elemTypeId);
    generateFSharpCall(pred, predName, { elemLoad });
    auto* predResult = _result;
    if (!predResult)
    {
        reportTypeError("forall: failed to apply predicate to element");
        return;
    }
    // If predicate is false, short-circuit to falseBlock
    _builder.createCondBr(toBool(predResult), condBlock, falseBlock);

    // True: all elements matched
    _builder.setInsertPoint(emptyTrueBlock);
    _builder.createStore(resultStorage, _builder.getBoolean(true));
    _builder.createBr(endBlock);

    // False: predicate failed for an element
    _builder.setInsertPoint(falseBlock);
    _builder.createStore(resultStorage, _builder.getBoolean(false));
    _builder.createBr(endBlock);

    // End
    _builder.setInsertPoint(endBlock);
    _result = _builder.createLoad(resultStorage, "forall.result");
}

void IRGenerator::generateEachIR(std::string const& funcParamName, CoreVM::Value* listValue)
{
    auto* tag1 = _builder.get(CoreVM::CoreNumber(1));  // Cons
    auto* slot0 = _builder.get(CoreVM::CoreNumber(0)); // head
    auto* slot1 = _builder.get(CoreVM::CoreNumber(1)); // tail

    // Resolve function
    auto funcName = funcParamName;
    if (auto ref = lookupFSharpFunctionRef(funcParamName))
        funcName = *ref;

    // Check for builtin print/println as function argument
    bool const isPrintBuiltin = (funcName == "print" || funcName == "println");
    FSharpFunction const* func = nullptr;
    if (!isPrintBuiltin)
    {
        func = lookupFSharpFunction(funcName);
        if (!func)
        {
            if (auto* storage = lookupFSharpVariable(funcParamName);
                !storage || getObjectTypeId(storage).value_or(0) != CoreVM::BuiltinTypeId::Callable)
            {
                reportTypeError("each: function argument '{}' not found", std::string_view(funcParamName));
                return;
            }
        }
    }

    // Allocas
    auto* srcStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "each.src");
    _builder.createStore(srcStorage, listValue);
    // Use Void alloca for typed object elements (e.g., CompletionEntry) so convertToString
    // dispatches to object_to_string at runtime instead of N2S on the raw pointer.
    auto const hasObjectElements = getListElementTypeId(listValue).has_value();
    auto const eachElemType = hasObjectElements
                                  ? CoreVM::LiteralType::Void
                                  : getListElementLiteralType(listValue).value_or(CoreVM::LiteralType::Number);
    auto* elemAlloca = createAllocaInEntryBlock(eachElemType, "each.elem");

    // Propagate list element type to extracted elements
    if (auto elemTypeId = getListElementTypeId(listValue))
        annotateObjectTypeId(elemAlloca, *elemTypeId);

    // Create blocks
    auto* condBlock = _builder.createBlock("each.cond");
    auto* bodyBlock = _builder.createBlock("each.body");
    auto* endBlock = _builder.createBlock("each.end");

    _builder.createBr(condBlock);

    // Condition: check if source list is Cons
    _builder.setInsertPoint(condBlock);
    auto* srcLoad = _builder.createLoad(srcStorage, "each.src.load");
    auto* srcTag = _builder.createObjGetTag(srcLoad, "each.src.tag");
    auto* isCons = _builder.createNCmpEQ(srcTag, tag1, "each.is_cons");
    _builder.createCondBr(isCons, bodyBlock, endBlock);

    // Body: extract head, apply function, advance tail, loop
    _builder.setInsertPoint(bodyBlock);
    auto* srcForHead = _builder.createLoad(srcStorage, "each.src.for_head");
    auto* head = _builder.createObjGetSlot(srcForHead, slot0, "each.head");
    _builder.createStore(elemAlloca, head);

    auto* srcForTail = _builder.createLoad(srcStorage, "each.src.for_tail");
    auto* tail = _builder.createObjGetSlot(srcForTail, slot1, "each.tail");
    _builder.createStore(srcStorage, tail);

    auto* elemLoad = _builder.createLoad(elemAlloca, "each.elem.load");
    if (auto elemTypeId = getObjectTypeId(elemAlloca))
        annotateObjectTypeId(elemLoad, *elemTypeId);
    // Propagate element inner type (e.g., String for Number-typed values from compiled functions)
    if (auto elemInnerType = getListElementInnerType(listValue))
    {
        annotateInnerType(elemLoad, *elemInnerType);
    }

    if (isPrintBuiltin)
    {
        // Directly call print/println builtin
        auto* strVal = convertToString(elemLoad, "each.elem");
        const auto* const sig = funcName == "println" ? "println(S)V" : "print(S)V";
        auto* callback = findCallback(sig);
        if (callback)
            _builder.createCallFunction(_builder.getBuiltinFunction(*callback), { strVal }, funcName);
    }
    else
    {
        applyHOFFunction(funcParamName, func, funcName, { elemLoad }, "each");
        if (!_result)
        {
            reportTypeError("each: failed to apply function to element");
            return;
        }
    }
    // Discard function result (each is for side effects only)
    _builder.createBr(condBlock);

    // End: return unit
    _builder.setInsertPoint(endBlock);
    _result = _builder.get(CoreVM::CoreNumber(0));
}

void IRGenerator::generateTakeIR(CoreVM::Value* countValue, CoreVM::Value* listValue)
{
    auto* tag1 = _builder.get(CoreVM::CoreNumber(1));  // Cons
    auto* slot0 = _builder.get(CoreVM::CoreNumber(0)); // head
    auto* slot1 = _builder.get(CoreVM::CoreNumber(1)); // tail
    auto* one = _builder.get(CoreVM::CoreNumber(1));
    auto* zero = _builder.get(CoreVM::CoreNumber(0));

    // Allocas
    auto* srcStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "take.src");
    _builder.createStore(srcStorage, listValue);

    auto* counterStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Number, "take.counter");
    _builder.createStore(counterStorage, countValue);

    auto* accStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "take.acc");
    auto* nil = emitNilList(CoreVM::LiteralType::Void, "take.nil");
    _builder.createStore(accStorage, nil);

    // Create blocks
    auto* condBlock = _builder.createBlock("take.cond");
    auto* bodyBlock = _builder.createBlock("take.body");
    auto* revInitBlock = _builder.createBlock("take.rev.init");
    auto* revCondBlock = _builder.createBlock("take.rev.cond");
    auto* revBodyBlock = _builder.createBlock("take.rev.body");
    auto* endBlock = _builder.createBlock("take.end");

    _builder.createBr(condBlock);

    // Phase 1: Check if counter > 0 AND list is Cons
    _builder.setInsertPoint(condBlock);
    auto* counterLoad = _builder.createLoad(counterStorage, "take.counter.load");
    auto* counterGtZero = _builder.createNCmpGT(counterLoad, zero, "take.counter_gt_zero");
    auto* checkListBlock = _builder.createBlock("take.check_list");
    _builder.createCondBr(counterGtZero, checkListBlock, revInitBlock);

    _builder.setInsertPoint(checkListBlock);
    auto* srcLoad = _builder.createLoad(srcStorage, "take.src.load");
    auto* srcTag = _builder.createObjGetTag(srcLoad, "take.src.tag");
    auto* isCons = _builder.createNCmpEQ(srcTag, tag1, "take.is_cons");
    _builder.createCondBr(isCons, bodyBlock, revInitBlock);

    // Body: extract head, cons onto reversed accumulator, decrement counter
    _builder.setInsertPoint(bodyBlock);
    auto* srcForHead = _builder.createLoad(srcStorage, "take.src.for_head");
    auto* head = _builder.createObjGetSlot(srcForHead, slot0, "take.head");

    auto* srcForTail = _builder.createLoad(srcStorage, "take.src.for_tail");
    auto* tail = _builder.createObjGetSlot(srcForTail, slot1, "take.tail");
    _builder.createStore(srcStorage, tail);

    // Store head and acc in temp allocas to survive ObjAlloc
    auto* elemTmp = createAllocaInEntryBlock(CoreVM::LiteralType::Void, "take.elem.tmp");
    _builder.createStore(elemTmp, head);
    auto* accTmp = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "take.acc.tmp");
    auto* accForCons = _builder.createLoad(accStorage, "take.acc.for_cons");
    _builder.createStore(accTmp, accForCons);

    auto* elemReload = _builder.createLoad(elemTmp, "take.elem.reload");
    auto* accReload = _builder.createLoad(accTmp, "take.acc.reload");
    auto* cons = emitListCons(elemReload, accReload, CoreVM::LiteralType::Void, "take.cons");
    _builder.createStore(accStorage, cons);

    // Decrement counter
    auto* counterLoad2 = _builder.createLoad(counterStorage, "take.counter.load2");
    auto* counterDec = _builder.createSub(counterLoad2, one, "take.counter.dec");
    _builder.createStore(counterStorage, counterDec);

    _builder.createBr(condBlock);

    // Phase 2: Reverse the accumulated list
    auto* revSrcStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "take.rev.src");
    auto* revAccStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "take.rev.acc");

    _builder.setInsertPoint(revInitBlock);
    auto* revSrcInit = _builder.createLoad(accStorage, "take.rev.src.init");
    _builder.createStore(revSrcStorage, revSrcInit);
    auto* revNil = emitNilList(CoreVM::LiteralType::Void, "take.rev.nil");
    _builder.createStore(revAccStorage, revNil);
    _builder.createBr(revCondBlock);

    _builder.setInsertPoint(revCondBlock);
    auto* revSrcLoad = _builder.createLoad(revSrcStorage, "take.rev.src.load");
    auto* revSrcTag = _builder.createObjGetTag(revSrcLoad, "take.rev.src.tag");
    auto* revIsCons = _builder.createNCmpEQ(revSrcTag, tag1, "take.rev.is_cons");
    _builder.createCondBr(revIsCons, revBodyBlock, endBlock);

    _builder.setInsertPoint(revBodyBlock);
    auto* revSrcForHead = _builder.createLoad(revSrcStorage, "take.rev.src.for_head");
    auto* revHead = _builder.createObjGetSlot(revSrcForHead, slot0, "take.rev.head");
    auto* revSrcForTail = _builder.createLoad(revSrcStorage, "take.rev.src.for_tail");
    auto* revTail = _builder.createObjGetSlot(revSrcForTail, slot1, "take.rev.tail");
    _builder.createStore(revSrcStorage, revTail);

    auto* revElemTmp = createAllocaInEntryBlock(CoreVM::LiteralType::Void, "take.rev.elem.tmp");
    _builder.createStore(revElemTmp, revHead);
    auto* revAccTmp = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "take.rev.acc.tmp");
    auto* revAccForCons = _builder.createLoad(revAccStorage, "take.rev.acc.for_cons");
    _builder.createStore(revAccTmp, revAccForCons);

    auto* revElemReload = _builder.createLoad(revElemTmp, "take.rev.elem.reload");
    auto* revAccReload = _builder.createLoad(revAccTmp, "take.rev.acc.reload");
    auto* revCons = emitListCons(revElemReload, revAccReload, CoreVM::LiteralType::Void, "take.rev.cons");

    _builder.createStore(revAccStorage, revCons);
    _builder.createBr(revCondBlock);

    _builder.setInsertPoint(endBlock);
    _result = _builder.createLoad(revAccStorage, "take.result");
    annotateObjectTypeId(_result, CoreVM::BuiltinTypeId::List);
    if (auto elemTypeId = getListElementTypeId(listValue))
        annotateListElementTypeId(_result, *elemTypeId);
    if (auto elt = getListElementLiteralType(listValue))
        annotateListElementLiteralType(_result, *elt);
}

void IRGenerator::generateDropIR(CoreVM::Value* countValue, CoreVM::Value* listValue)
{
    auto* tag1 = _builder.get(CoreVM::CoreNumber(1));  // Cons
    auto* slot1 = _builder.get(CoreVM::CoreNumber(1)); // tail
    auto* one = _builder.get(CoreVM::CoreNumber(1));
    auto* zero = _builder.get(CoreVM::CoreNumber(0));

    // Allocas
    auto* srcStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "drop.src");
    _builder.createStore(srcStorage, listValue);

    auto* counterStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Number, "drop.counter");
    _builder.createStore(counterStorage, countValue);

    // Create blocks
    auto* condBlock = _builder.createBlock("drop.cond");
    auto* bodyBlock = _builder.createBlock("drop.body");
    auto* endBlock = _builder.createBlock("drop.end");

    _builder.createBr(condBlock);

    // Condition: check counter > 0 AND list is Cons
    _builder.setInsertPoint(condBlock);
    auto* counterLoad = _builder.createLoad(counterStorage, "drop.counter.load");
    auto* counterGtZero = _builder.createNCmpGT(counterLoad, zero, "drop.counter_gt_zero");
    auto* checkListBlock = _builder.createBlock("drop.check_list");
    _builder.createCondBr(counterGtZero, checkListBlock, endBlock);

    _builder.setInsertPoint(checkListBlock);
    auto* srcLoad = _builder.createLoad(srcStorage, "drop.src.load");
    auto* srcTag = _builder.createObjGetTag(srcLoad, "drop.src.tag");
    auto* isCons = _builder.createNCmpEQ(srcTag, tag1, "drop.is_cons");
    _builder.createCondBr(isCons, bodyBlock, endBlock);

    // Body: advance to tail, decrement counter
    _builder.setInsertPoint(bodyBlock);
    auto* srcForTail = _builder.createLoad(srcStorage, "drop.src.for_tail");
    auto* tail = _builder.createObjGetSlot(srcForTail, slot1, "drop.tail");
    _builder.createStore(srcStorage, tail);

    auto* counterLoad2 = _builder.createLoad(counterStorage, "drop.counter.load2");
    auto* counterDec = _builder.createSub(counterLoad2, one, "drop.counter.dec");
    _builder.createStore(counterStorage, counterDec);

    _builder.createBr(condBlock);

    // End: return remaining list
    _builder.setInsertPoint(endBlock);
    _result = _builder.createLoad(srcStorage, "drop.result");
    annotateObjectTypeId(_result, CoreVM::BuiltinTypeId::List);
    if (auto elemTypeId = getListElementTypeId(listValue))
        annotateListElementTypeId(_result, *elemTypeId);
    if (auto elt = getListElementLiteralType(listValue))
        annotateListElementLiteralType(_result, *elt);
}

void IRGenerator::generateZipIR(CoreVM::Value* listA, CoreVM::Value* listB)
{
    auto* tag1 = _builder.get(CoreVM::CoreNumber(1));  // Cons
    auto* slot0 = _builder.get(CoreVM::CoreNumber(0)); // head / fst
    auto* slot1 = _builder.get(CoreVM::CoreNumber(1)); // tail / snd

    // Allocas for both source lists
    auto* srcAStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "zip.srcA");
    _builder.createStore(srcAStorage, listA);
    auto* srcBStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "zip.srcB");
    _builder.createStore(srcBStorage, listB);

    auto* accStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "zip.acc");
    auto* nil = emitNilList(CoreVM::LiteralType::Object, "zip.nil");
    _builder.createStore(accStorage, nil);

    // Temp allocas for head values (must survive ObjAlloc for tuple + cons)
    auto* headATmp = createAllocaInEntryBlock(CoreVM::LiteralType::Void, "zip.headA.tmp");
    auto* headBTmp = createAllocaInEntryBlock(CoreVM::LiteralType::Void, "zip.headB.tmp");
    auto* tupleTmp = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "zip.tuple.tmp");
    auto* accTmp = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "zip.acc.tmp");

    // Create blocks
    auto* condBlock = _builder.createBlock("zip.cond");
    auto* bodyBlock = _builder.createBlock("zip.body");
    auto* revInitBlock = _builder.createBlock("zip.rev.init");
    auto* revCondBlock = _builder.createBlock("zip.rev.cond");
    auto* revBodyBlock = _builder.createBlock("zip.rev.body");
    auto* endBlock = _builder.createBlock("zip.end");

    _builder.createBr(condBlock);

    // Phase 1: Check if BOTH lists are Cons
    _builder.setInsertPoint(condBlock);
    auto* srcALoad = _builder.createLoad(srcAStorage, "zip.srcA.load");
    auto* srcATag = _builder.createObjGetTag(srcALoad, "zip.srcA.tag");
    auto* isConsA = _builder.createNCmpEQ(srcATag, tag1, "zip.is_consA");
    auto* checkBBlock = _builder.createBlock("zip.checkB");
    _builder.createCondBr(isConsA, checkBBlock, revInitBlock);

    _builder.setInsertPoint(checkBBlock);
    auto* srcBLoad = _builder.createLoad(srcBStorage, "zip.srcB.load");
    auto* srcBTag = _builder.createObjGetTag(srcBLoad, "zip.srcB.tag");
    auto* isConsB = _builder.createNCmpEQ(srcBTag, tag1, "zip.is_consB");
    _builder.createCondBr(isConsB, bodyBlock, revInitBlock);

    // Body: extract heads from both lists, build tuple, cons onto acc
    _builder.setInsertPoint(bodyBlock);
    // Extract head A
    auto* srcAForHead = _builder.createLoad(srcAStorage, "zip.srcA.for_head");
    auto* headA = _builder.createObjGetSlot(srcAForHead, slot0, "zip.headA");
    _builder.createStore(headATmp, headA);
    // Advance A to tail
    auto* srcAForTail = _builder.createLoad(srcAStorage, "zip.srcA.for_tail");
    auto* tailA = _builder.createObjGetSlot(srcAForTail, slot1, "zip.tailA");
    _builder.createStore(srcAStorage, tailA);

    // Extract head B
    auto* srcBForHead = _builder.createLoad(srcBStorage, "zip.srcB.for_head");
    auto* headB = _builder.createObjGetSlot(srcBForHead, slot0, "zip.headB");
    _builder.createStore(headBTmp, headB);
    // Advance B to tail
    auto* srcBForTail = _builder.createLoad(srcBStorage, "zip.srcB.for_tail");
    auto* tailB = _builder.createObjGetSlot(srcBForTail, slot1, "zip.tailB");
    _builder.createStore(srcBStorage, tailB);

    // Build Tuple2 object from headA, headB
    // Reload heads from temp allocas (they must survive ObjAlloc)
    auto* headAReload = _builder.createLoad(headATmp, "zip.headA.reload");
    auto* headATmp2 = createAllocaInEntryBlock(CoreVM::LiteralType::Void, "zip.headA.tmp2");
    _builder.createStore(headATmp2, headAReload);
    auto* headBReload = _builder.createLoad(headBTmp, "zip.headB.reload");
    auto* headBTmp2 = createAllocaInEntryBlock(CoreVM::LiteralType::Void, "zip.headB.tmp2");
    _builder.createStore(headBTmp2, headBReload);

    auto* headAFinal = _builder.createLoad(headATmp2, "zip.headA.final");
    auto* headBFinal = _builder.createLoad(headBTmp2, "zip.headB.final");
    auto* tuple =
        emitTuple2(headAFinal, headBFinal, CoreVM::LiteralType::Void, CoreVM::LiteralType::Void, "zip.tuple");
    _builder.createStore(tupleTmp, tuple);

    // Cons tuple onto reversed accumulator
    auto* accForCons = _builder.createLoad(accStorage, "zip.acc.for_cons");
    _builder.createStore(accTmp, accForCons);

    auto* tupleReload = _builder.createLoad(tupleTmp, "zip.tuple.reload");
    auto* tupleStoreTmp = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "zip.tuple.store.tmp");
    _builder.createStore(tupleStoreTmp, tupleReload);
    auto* accReloadPre = _builder.createLoad(accTmp, "zip.acc.reload.pre");
    auto* accStoreTmp = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "zip.acc.store.tmp");
    _builder.createStore(accStoreTmp, accReloadPre);

    auto* tupleForSlot = _builder.createLoad(tupleStoreTmp, "zip.tuple.for_slot");
    auto* accForSlot = _builder.createLoad(accStoreTmp, "zip.acc.for_slot");
    auto* cons = emitListCons(tupleForSlot, accForSlot, CoreVM::LiteralType::Object, "zip.cons");

    _builder.createStore(accStorage, cons);
    _builder.createBr(condBlock);

    // Phase 2: Reverse the accumulated list
    auto* revSrcStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "zip.rev.src");
    auto* revAccStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "zip.rev.acc");

    _builder.setInsertPoint(revInitBlock);
    auto* revSrcInit = _builder.createLoad(accStorage, "zip.rev.src.init");
    _builder.createStore(revSrcStorage, revSrcInit);
    auto* revNil = emitNilList(CoreVM::LiteralType::Object, "zip.rev.nil");
    _builder.createStore(revAccStorage, revNil);
    _builder.createBr(revCondBlock);

    _builder.setInsertPoint(revCondBlock);
    auto* revSrcLoad = _builder.createLoad(revSrcStorage, "zip.rev.src.load");
    auto* revSrcTag = _builder.createObjGetTag(revSrcLoad, "zip.rev.src.tag");
    auto* revIsCons = _builder.createNCmpEQ(revSrcTag, tag1, "zip.rev.is_cons");
    _builder.createCondBr(revIsCons, revBodyBlock, endBlock);

    _builder.setInsertPoint(revBodyBlock);
    auto* revSrcForHead = _builder.createLoad(revSrcStorage, "zip.rev.src.for_head");
    auto* revHead = _builder.createObjGetSlot(revSrcForHead, slot0, "zip.rev.head");
    auto* revSrcForTail = _builder.createLoad(revSrcStorage, "zip.rev.src.for_tail");
    auto* revTail = _builder.createObjGetSlot(revSrcForTail, slot1, "zip.rev.tail");
    _builder.createStore(revSrcStorage, revTail);

    auto* revElemTmp = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "zip.rev.elem.tmp");
    _builder.createStore(revElemTmp, revHead);
    auto* revAccTmp = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "zip.rev.acc.tmp");
    auto* revAccForCons = _builder.createLoad(revAccStorage, "zip.rev.acc.for_cons");
    _builder.createStore(revAccTmp, revAccForCons);

    auto* revElemReload = _builder.createLoad(revElemTmp, "zip.rev.elem.reload");
    auto* revAccReload = _builder.createLoad(revAccTmp, "zip.rev.acc.reload");
    auto* revCons = emitListCons(revElemReload, revAccReload, CoreVM::LiteralType::Object, "zip.rev.cons");

    _builder.createStore(revAccStorage, revCons);
    _builder.createBr(revCondBlock);

    _builder.setInsertPoint(endBlock);
    _result = _builder.createLoad(revAccStorage, "zip.result");
    annotateObjectTypeId(_result, CoreVM::BuiltinTypeId::List);
}

void IRGenerator::generateFlattenIR(CoreVM::Value* listOfLists)
{
    auto* tag1 = _builder.get(CoreVM::CoreNumber(1));  // Cons
    auto* slot0 = _builder.get(CoreVM::CoreNumber(0)); // head
    auto* slot1 = _builder.get(CoreVM::CoreNumber(1)); // tail

    // Allocas
    auto* outerSrcStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "flatten.outer.src");
    _builder.createStore(outerSrcStorage, listOfLists);

    auto* innerSrcStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "flatten.inner.src");

    auto* accStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "flatten.acc");
    auto* nil = emitNilList(CoreVM::LiteralType::Void, "flatten.nil");
    _builder.createStore(accStorage, nil);

    // Create blocks
    auto* outerCondBlock = _builder.createBlock("flatten.outer.cond");
    auto* outerBodyBlock = _builder.createBlock("flatten.outer.body");
    auto* innerCondBlock = _builder.createBlock("flatten.inner.cond");
    auto* innerBodyBlock = _builder.createBlock("flatten.inner.body");
    auto* revInitBlock = _builder.createBlock("flatten.rev.init");
    auto* revCondBlock = _builder.createBlock("flatten.rev.cond");
    auto* revBodyBlock = _builder.createBlock("flatten.rev.body");
    auto* endBlock = _builder.createBlock("flatten.end");

    _builder.createBr(outerCondBlock);

    // Outer loop: iterate list of lists
    _builder.setInsertPoint(outerCondBlock);
    auto* outerSrcLoad = _builder.createLoad(outerSrcStorage, "flatten.outer.src.load");
    auto* outerSrcTag = _builder.createObjGetTag(outerSrcLoad, "flatten.outer.src.tag");
    auto* outerIsCons = _builder.createNCmpEQ(outerSrcTag, tag1, "flatten.outer.is_cons");
    _builder.createCondBr(outerIsCons, outerBodyBlock, revInitBlock);

    // Outer body: extract inner list, advance outer
    _builder.setInsertPoint(outerBodyBlock);
    auto* outerForHead = _builder.createLoad(outerSrcStorage, "flatten.outer.for_head");
    auto* innerList = _builder.createObjGetSlot(outerForHead, slot0, "flatten.inner.list");
    _builder.createStore(innerSrcStorage, innerList);

    auto* outerForTail = _builder.createLoad(outerSrcStorage, "flatten.outer.for_tail");
    auto* outerTail = _builder.createObjGetSlot(outerForTail, slot1, "flatten.outer.tail");
    _builder.createStore(outerSrcStorage, outerTail);

    _builder.createBr(innerCondBlock);

    // Inner loop: iterate elements of current inner list
    _builder.setInsertPoint(innerCondBlock);
    auto* innerSrcLoad = _builder.createLoad(innerSrcStorage, "flatten.inner.src.load");
    auto* innerSrcTag = _builder.createObjGetTag(innerSrcLoad, "flatten.inner.src.tag");
    auto* innerIsCons = _builder.createNCmpEQ(innerSrcTag, tag1, "flatten.inner.is_cons");
    _builder.createCondBr(innerIsCons, innerBodyBlock, outerCondBlock);

    // Inner body: extract element, cons onto accumulator
    _builder.setInsertPoint(innerBodyBlock);
    auto* innerForHead = _builder.createLoad(innerSrcStorage, "flatten.inner.for_head");
    auto* elem = _builder.createObjGetSlot(innerForHead, slot0, "flatten.elem");

    auto* innerForTail = _builder.createLoad(innerSrcStorage, "flatten.inner.for_tail");
    auto* innerTail = _builder.createObjGetSlot(innerForTail, slot1, "flatten.inner.tail");
    _builder.createStore(innerSrcStorage, innerTail);

    // Store elem and acc in temp allocas to survive ObjAlloc
    auto* elemTmp = createAllocaInEntryBlock(CoreVM::LiteralType::Void, "flatten.elem.tmp");
    _builder.createStore(elemTmp, elem);
    auto* accTmp = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "flatten.acc.tmp");
    auto* accForCons = _builder.createLoad(accStorage, "flatten.acc.for_cons");
    _builder.createStore(accTmp, accForCons);

    auto* elemReload = _builder.createLoad(elemTmp, "flatten.elem.reload");
    auto* accReload = _builder.createLoad(accTmp, "flatten.acc.reload");
    auto* cons = emitListCons(elemReload, accReload, CoreVM::LiteralType::Void, "flatten.cons");

    _builder.createStore(accStorage, cons);
    _builder.createBr(innerCondBlock);

    // Phase 2: Reverse the accumulated list
    auto* revSrcStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "flatten.rev.src");
    auto* revAccStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "flatten.rev.acc");

    _builder.setInsertPoint(revInitBlock);
    auto* revSrcInit = _builder.createLoad(accStorage, "flatten.rev.src.init");
    _builder.createStore(revSrcStorage, revSrcInit);
    auto* revNil = emitNilList(CoreVM::LiteralType::Void, "flatten.rev.nil");
    _builder.createStore(revAccStorage, revNil);
    _builder.createBr(revCondBlock);

    _builder.setInsertPoint(revCondBlock);
    auto* revSrcLoad = _builder.createLoad(revSrcStorage, "flatten.rev.src.load");
    auto* revSrcTag = _builder.createObjGetTag(revSrcLoad, "flatten.rev.src.tag");
    auto* revIsCons = _builder.createNCmpEQ(revSrcTag, tag1, "flatten.rev.is_cons");
    _builder.createCondBr(revIsCons, revBodyBlock, endBlock);

    _builder.setInsertPoint(revBodyBlock);
    auto* revSrcForHead = _builder.createLoad(revSrcStorage, "flatten.rev.src.for_head");
    auto* revHead = _builder.createObjGetSlot(revSrcForHead, slot0, "flatten.rev.head");
    auto* revSrcForTail = _builder.createLoad(revSrcStorage, "flatten.rev.src.for_tail");
    auto* revTail = _builder.createObjGetSlot(revSrcForTail, slot1, "flatten.rev.tail");
    _builder.createStore(revSrcStorage, revTail);

    auto* revElemTmp = createAllocaInEntryBlock(CoreVM::LiteralType::Void, "flatten.rev.elem.tmp");
    _builder.createStore(revElemTmp, revHead);
    auto* revAccTmp = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "flatten.rev.acc.tmp");
    auto* revAccForCons = _builder.createLoad(revAccStorage, "flatten.rev.acc.for_cons");
    _builder.createStore(revAccTmp, revAccForCons);

    auto* revElemReload = _builder.createLoad(revElemTmp, "flatten.rev.elem.reload");
    auto* revAccReload = _builder.createLoad(revAccTmp, "flatten.rev.acc.reload");
    auto* revCons = emitListCons(revElemReload, revAccReload, CoreVM::LiteralType::Void, "flatten.rev.cons");

    _builder.createStore(revAccStorage, revCons);
    _builder.createBr(revCondBlock);

    _builder.setInsertPoint(endBlock);
    _result = _builder.createLoad(revAccStorage, "flatten.result");
    annotateObjectTypeId(_result, CoreVM::BuiltinTypeId::List);
}

void IRGenerator::generateSortIR(CoreVM::Value* listValue)
{
    auto* callback = findCallback("list_sort(I)I");
    if (!callback)
    {
        reportTypeError("sort: native callback 'list_sort' not found");
        return;
    }
    _result =
        _builder.createCallFunction(_builder.getBuiltinFunction(*callback), { listValue }, "sort.result");
    annotateObjectTypeId(_result, CoreVM::BuiltinTypeId::List);
    if (auto elt = getListElementLiteralType(listValue))
        annotateListElementLiteralType(_result, *elt);
}

void IRGenerator::generateDistinctIR(CoreVM::Value* listValue)
{
    auto* callback = findCallback("list_distinct(I)I");
    if (!callback)
    {
        reportTypeError("distinct: native callback 'list_distinct' not found");
        return;
    }
    _result =
        _builder.createCallFunction(_builder.getBuiltinFunction(*callback), { listValue }, "distinct.result");
    annotateObjectTypeId(_result, CoreVM::BuiltinTypeId::List);
    if (auto elt = getListElementLiteralType(listValue))
        annotateListElementLiteralType(_result, *elt);
}

void IRGenerator::generateSortByIR(std::string const& funcParamName, CoreVM::Value* listValue)
{
    auto* tag1 = _builder.get(CoreVM::CoreNumber(1));  // Cons
    auto* slot0 = _builder.get(CoreVM::CoreNumber(0)); // head / fst
    auto* slot1 = _builder.get(CoreVM::CoreNumber(1)); // tail / snd

    // Resolve the key function
    auto funcName = funcParamName;
    if (auto ref = lookupFSharpFunctionRef(funcParamName))
        funcName = *ref;
    auto const* func = lookupFSharpFunction(funcName);
    if (!func)
    {
        reportTypeError("sortBy: function argument '{}' not found", std::string_view(funcParamName));
        return;
    }

    // Phase 1: Iterate source list, call key function on each element, build reversed list of Tuple2(key,
    // elem)
    auto* srcStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "sortBy.src");
    _builder.createStore(srcStorage, listValue);

    auto* accStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "sortBy.acc");
    auto* nil = emitNilList(CoreVM::LiteralType::Object, "sortBy.nil");
    _builder.createStore(accStorage, nil);

    auto const sortByElemType = getListElementLiteralType(listValue).value_or(CoreVM::LiteralType::Number);
    auto* elemAlloca = createAllocaInEntryBlock(sortByElemType, "sortBy.elem");

    // Propagate list element type to extracted elements
    if (auto elemTypeId = getListElementTypeId(listValue))
        annotateObjectTypeId(elemAlloca, *elemTypeId);

    // Temp allocas for values that must survive ObjAlloc
    auto* keyTmp = createAllocaInEntryBlock(CoreVM::LiteralType::Number, "sortBy.key.tmp");
    auto* elemTmp2 = createAllocaInEntryBlock(sortByElemType, "sortBy.elem.tmp2");
    auto* tupleTmp = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "sortBy.tuple.tmp");
    auto* accTmp = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "sortBy.acc.tmp");

    // Create blocks
    auto* condBlock = _builder.createBlock("sortBy.cond");
    auto* bodyBlock = _builder.createBlock("sortBy.body");
    auto* callBlock = _builder.createBlock("sortBy.call");

    _builder.createBr(condBlock);

    // Condition: check if source list is Cons
    _builder.setInsertPoint(condBlock);
    auto* srcLoad = _builder.createLoad(srcStorage, "sortBy.src.load");
    auto* srcTag = _builder.createObjGetTag(srcLoad, "sortBy.src.tag");
    auto* isCons = _builder.createNCmpEQ(srcTag, tag1, "sortBy.is_cons");
    _builder.createCondBr(isCons, bodyBlock, callBlock);

    // Body: extract head, call key function, build Tuple2(key, elem), cons onto acc
    _builder.setInsertPoint(bodyBlock);

    // Extract head (elem)
    auto* srcForHead = _builder.createLoad(srcStorage, "sortBy.src.for_head");
    auto* head = _builder.createObjGetSlot(srcForHead, slot0, "sortBy.head");
    _builder.createStore(elemAlloca, head);

    // Advance source to tail
    auto* srcForTail = _builder.createLoad(srcStorage, "sortBy.src.for_tail");
    auto* tail = _builder.createObjGetSlot(srcForTail, slot1, "sortBy.tail");
    _builder.createStore(srcStorage, tail);

    // Call key function on element
    auto* elemLoad = _builder.createLoad(elemAlloca, "sortBy.elem.load");
    if (auto elemTypeId = getObjectTypeId(elemAlloca))
        annotateObjectTypeId(elemLoad, *elemTypeId);
    generateFSharpCall(func, funcName, { elemLoad });
    auto* keyValue = _result;
    if (!keyValue)
    {
        reportTypeError("sortBy: failed to apply key function to element");
        return;
    }

    // Unwrap Size objects to their raw byte count for numeric comparison
    if (auto const keyTypeId = getObjectTypeId(keyValue))
    {
        if (*keyTypeId == CoreVM::BuiltinTypeId::Size)
            keyValue = _builder.createObjGetSlot(
                keyValue, _builder.get(CoreVM::CoreNumber(0)), "sortBy.key.size.bytes");
    }

    // Store key and elem in temp allocas (must survive Tuple2 ObjAlloc)
    _builder.createStore(keyTmp, keyValue);
    auto* elemReload = _builder.createLoad(elemAlloca, "sortBy.elem.reload");
    _builder.createStore(elemTmp2, elemReload);

    // Build Tuple2(key, elem)
    auto* keyReload = _builder.createLoad(keyTmp, "sortBy.key.reload");
    auto* elemFinal = _builder.createLoad(elemTmp2, "sortBy.elem.final");
    auto* tuple = emitTuple2(
        keyReload, elemFinal, CoreVM::LiteralType::Void, CoreVM::LiteralType::Void, "sortBy.tuple");
    _builder.createStore(tupleTmp, tuple);

    // Cons tuple onto accumulator
    auto* accForCons = _builder.createLoad(accStorage, "sortBy.acc.for_cons");
    _builder.createStore(accTmp, accForCons);

    auto* tupleReload = _builder.createLoad(tupleTmp, "sortBy.tuple.reload");
    auto* tupleStoreTmp = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "sortBy.tuple.store.tmp");
    _builder.createStore(tupleStoreTmp, tupleReload);
    auto* accReloadPre = _builder.createLoad(accTmp, "sortBy.acc.reload.pre");
    auto* accStoreTmp = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "sortBy.acc.store.tmp");
    _builder.createStore(accStoreTmp, accReloadPre);

    auto* tupleForSlot = _builder.createLoad(tupleStoreTmp, "sortBy.tuple.for_slot");
    auto* accForSlot = _builder.createLoad(accStoreTmp, "sortBy.acc.for_slot");
    auto* cons = emitListCons(tupleForSlot, accForSlot, CoreVM::LiteralType::Object, "sortBy.cons");

    _builder.createStore(accStorage, cons);
    _builder.createBr(condBlock);

    // Phase 2: Call native list_sort_pairs to sort by key and extract elements
    _builder.setInsertPoint(callBlock);
    auto* callback = findCallback("list_sort_pairs(I)I");
    if (!callback)
    {
        reportTypeError("sortBy: native callback 'list_sort_pairs' not found");
        return;
    }
    auto* accFinal = _builder.createLoad(accStorage, "sortBy.acc.final");
    _result =
        _builder.createCallFunction(_builder.getBuiltinFunction(*callback), { accFinal }, "sortBy.result");
    annotateObjectTypeId(_result, CoreVM::BuiltinTypeId::List);
    if (auto elemTypeId = getListElementTypeId(listValue))
        annotateListElementTypeId(_result, *elemTypeId);
    if (auto elt = getListElementLiteralType(listValue))
        annotateListElementLiteralType(_result, *elt);
}

void IRGenerator::generateGroupByIR(std::string const& funcParamName, CoreVM::Value* listValue)
{
    auto* tag1 = _builder.get(CoreVM::CoreNumber(1));  // Cons
    auto* slot0 = _builder.get(CoreVM::CoreNumber(0)); // head / fst
    auto* slot1 = _builder.get(CoreVM::CoreNumber(1)); // tail / snd

    // Resolve the key function
    auto funcName = funcParamName;
    if (auto ref = lookupFSharpFunctionRef(funcParamName))
        funcName = *ref;
    auto const* func = lookupFSharpFunction(funcName);
    if (!func)
    {
        reportTypeError("groupBy: function argument '{}' not found", std::string_view(funcParamName));
        return;
    }

    // Phase 1: Iterate source list, call key function on each element, build reversed list of Tuple2(key,
    // elem)
    auto* srcStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "groupBy.src");
    _builder.createStore(srcStorage, listValue);

    auto* accStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "groupBy.acc");
    auto* nil = emitNilList(CoreVM::LiteralType::Object, "groupBy.nil");
    _builder.createStore(accStorage, nil);

    auto const groupByElemType = getListElementLiteralType(listValue).value_or(CoreVM::LiteralType::Number);
    auto* elemAlloca = createAllocaInEntryBlock(groupByElemType, "groupBy.elem");

    // Propagate list element type to extracted elements
    if (auto elemTypeId = getListElementTypeId(listValue))
        annotateObjectTypeId(elemAlloca, *elemTypeId);

    // Temp allocas for values that must survive ObjAlloc
    auto* keyTmp = createAllocaInEntryBlock(CoreVM::LiteralType::Number, "groupBy.key.tmp");
    auto* elemTmp2 = createAllocaInEntryBlock(groupByElemType, "groupBy.elem.tmp2");
    auto* tupleTmp = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "groupBy.tuple.tmp");
    auto* accTmp = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "groupBy.acc.tmp");

    // Create blocks
    auto* condBlock = _builder.createBlock("groupBy.cond");
    auto* bodyBlock = _builder.createBlock("groupBy.body");
    auto* callBlock = _builder.createBlock("groupBy.call");

    _builder.createBr(condBlock);

    // Condition: check if source list is Cons
    _builder.setInsertPoint(condBlock);
    auto* srcLoad = _builder.createLoad(srcStorage, "groupBy.src.load");
    auto* srcTag = _builder.createObjGetTag(srcLoad, "groupBy.src.tag");
    auto* isCons = _builder.createNCmpEQ(srcTag, tag1, "groupBy.is_cons");
    _builder.createCondBr(isCons, bodyBlock, callBlock);

    // Body: extract head, call key function, build Tuple2(key, elem), cons onto acc
    _builder.setInsertPoint(bodyBlock);

    // Extract head (elem)
    auto* srcForHead = _builder.createLoad(srcStorage, "groupBy.src.for_head");
    auto* head = _builder.createObjGetSlot(srcForHead, slot0, "groupBy.head");
    _builder.createStore(elemAlloca, head);

    // Advance source to tail
    auto* srcForTail = _builder.createLoad(srcStorage, "groupBy.src.for_tail");
    auto* tail = _builder.createObjGetSlot(srcForTail, slot1, "groupBy.tail");
    _builder.createStore(srcStorage, tail);

    // Call key function on element
    auto* elemLoad = _builder.createLoad(elemAlloca, "groupBy.elem.load");
    if (auto elemTypeId = getObjectTypeId(elemAlloca))
        annotateObjectTypeId(elemLoad, *elemTypeId);
    generateFSharpCall(func, funcName, { elemLoad });
    auto* keyValue = _result;
    if (!keyValue)
    {
        reportTypeError("groupBy: failed to apply key function to element");
        return;
    }

    // Store key and elem in temp allocas (must survive Tuple2 ObjAlloc)
    _builder.createStore(keyTmp, keyValue);
    auto* elemReload = _builder.createLoad(elemAlloca, "groupBy.elem.reload");
    _builder.createStore(elemTmp2, elemReload);

    // Build Tuple2(key, elem)
    auto* keyReload = _builder.createLoad(keyTmp, "groupBy.key.reload");
    auto* elemFinal = _builder.createLoad(elemTmp2, "groupBy.elem.final");
    auto* tuple = emitTuple2(
        keyReload, elemFinal, CoreVM::LiteralType::Void, CoreVM::LiteralType::Void, "groupBy.tuple");
    _builder.createStore(tupleTmp, tuple);

    // Cons tuple onto accumulator
    auto* accForCons = _builder.createLoad(accStorage, "groupBy.acc.for_cons");
    _builder.createStore(accTmp, accForCons);

    auto* tupleReload = _builder.createLoad(tupleTmp, "groupBy.tuple.reload");
    auto* tupleStoreTmp = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "groupBy.tuple.store.tmp");
    _builder.createStore(tupleStoreTmp, tupleReload);
    auto* accReloadPre = _builder.createLoad(accTmp, "groupBy.acc.reload.pre");
    auto* accStoreTmp = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "groupBy.acc.store.tmp");
    _builder.createStore(accStoreTmp, accReloadPre);

    auto* tupleForSlot = _builder.createLoad(tupleStoreTmp, "groupBy.tuple.for_slot");
    auto* accForSlot = _builder.createLoad(accStoreTmp, "groupBy.acc.for_slot");
    auto* cons = emitListCons(tupleForSlot, accForSlot, CoreVM::LiteralType::Object, "groupBy.cons");

    _builder.createStore(accStorage, cons);
    _builder.createBr(condBlock);

    // Phase 2: Call native list_group_pairs to group by key
    _builder.setInsertPoint(callBlock);
    auto* callback = findCallback("list_group_pairs(I)I");
    if (!callback)
    {
        reportTypeError("groupBy: native callback 'list_group_pairs' not found");
        return;
    }
    auto* accFinal = _builder.createLoad(accStorage, "groupBy.acc.final");
    _result =
        _builder.createCallFunction(_builder.getBuiltinFunction(*callback), { accFinal }, "groupBy.result");
    annotateObjectTypeId(_result, CoreVM::BuiltinTypeId::List);
}

// =============================================================================
// Seq-aware HOF generators
// =============================================================================

void IRGenerator::generateSeqEachIR(std::string const& funcParamName, CoreVM::Value* seqValue)
{
    auto* tag1 = _builder.get(CoreVM::CoreNumber(1));  // Cons
    auto* slot0 = _builder.get(CoreVM::CoreNumber(0)); // head
    auto* slot1 = _builder.get(CoreVM::CoreNumber(1)); // lazyTail

    // Resolve function
    auto funcName = funcParamName;
    if (auto ref = lookupFSharpFunctionRef(funcParamName))
        funcName = *ref;

    bool const isPrintBuiltin = (funcName == "print" || funcName == "println");
    FSharpFunction const* func = nullptr;
    if (!isPrintBuiltin)
    {
        func = lookupFSharpFunction(funcName);
        if (!func)
        {
            reportTypeError("each: function argument '{}' not found", std::string_view(funcParamName));
            return;
        }
    }

    // Allocas
    auto* srcStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "seq.each.src");
    _builder.createStore(srcStorage, seqValue);
    auto* elemAlloca = createAllocaInEntryBlock(CoreVM::LiteralType::Void, "seq.each.elem");

    // Create blocks
    auto* condBlock = _builder.createBlock("seq.each.cond");
    auto* bodyBlock = _builder.createBlock("seq.each.body");
    auto* endBlock = _builder.createBlock("seq.each.end");

    _builder.createBr(condBlock);

    // Condition: check if current seq node is Cons (tag == 1)
    _builder.setInsertPoint(condBlock);
    auto* srcLoad = _builder.createLoad(srcStorage, "seq.each.src.load");
    auto* srcTag = _builder.createObjGetTag(srcLoad, "seq.each.src.tag");
    auto* isCons = _builder.createNCmpEQ(srcTag, tag1, "seq.each.is_cons");
    _builder.createCondBr(isCons, bodyBlock, endBlock);

    // Body: extract head, apply function, force lazy tail, loop
    _builder.setInsertPoint(bodyBlock);
    auto* srcForHead = _builder.createLoad(srcStorage, "seq.each.src.for_head");
    auto* head = _builder.createObjGetSlot(srcForHead, slot0, "seq.each.head");
    _builder.createStore(elemAlloca, head);

    // Force lazy tail to get next seq node
    auto* srcForTail = _builder.createLoad(srcStorage, "seq.each.src.for_tail");
    auto* lazyTail = _builder.createObjGetSlot(srcForTail, slot1, "seq.each.lazy_tail");
    auto* nextSeq = _builder.createLazyForce(lazyTail, "seq.each.next");
    _builder.createStore(srcStorage, nextSeq);

    // Apply function to element
    auto* elemLoad = _builder.createLoad(elemAlloca, "seq.each.elem.load");
    if (isPrintBuiltin)
    {
        auto* strVal = convertToString(elemLoad, "seq.each.elem");
        auto const* sig = funcName == "println" ? "println(S)V" : "print(S)V";
        auto* callback = findCallback(sig);
        if (callback)
            _builder.createCallFunction(_builder.getBuiltinFunction(*callback), { strVal }, funcName);
    }
    else
    {
        generateFSharpCall(func, funcName, { elemLoad });
        if (!_result)
        {
            reportTypeError("each: failed to apply function to seq element");
            return;
        }
    }
    _builder.createBr(condBlock);

    // End: return unit
    _builder.setInsertPoint(endBlock);
    _result = _builder.get(CoreVM::CoreNumber(0));
}

void IRGenerator::generateSeqTakeIR(CoreVM::Value* countValue, CoreVM::Value* seqValue)
{
    auto* tag1 = _builder.get(CoreVM::CoreNumber(1));  // Cons
    auto* slot0 = _builder.get(CoreVM::CoreNumber(0)); // head
    auto* slot1 = _builder.get(CoreVM::CoreNumber(1)); // lazyTail
    auto* one = _builder.get(CoreVM::CoreNumber(1));
    auto* zero = _builder.get(CoreVM::CoreNumber(0));

    // Allocas
    auto* srcStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "seq.take.src");
    _builder.createStore(srcStorage, seqValue);

    auto* counterStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Number, "seq.take.counter");
    _builder.createStore(counterStorage, countValue);

    auto* accStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "seq.take.acc");
    auto* nil = emitNilList(CoreVM::LiteralType::Void, "seq.take.nil");
    _builder.createStore(accStorage, nil);

    // Phase 1 blocks: accumulate in reverse
    auto* condBlock = _builder.createBlock("seq.take.cond");
    auto* bodyBlock = _builder.createBlock("seq.take.body");
    auto* revInitBlock = _builder.createBlock("seq.take.rev.init");
    auto* revCondBlock = _builder.createBlock("seq.take.rev.cond");
    auto* revBodyBlock = _builder.createBlock("seq.take.rev.body");
    auto* endBlock = _builder.createBlock("seq.take.end");

    _builder.createBr(condBlock);

    // Phase 1: Check counter > 0 AND seq is Cons
    _builder.setInsertPoint(condBlock);
    auto* counterLoad = _builder.createLoad(counterStorage, "seq.take.counter.load");
    auto* counterGtZero = _builder.createNCmpGT(counterLoad, zero, "seq.take.counter_gt_zero");
    auto* checkSeqBlock = _builder.createBlock("seq.take.check_seq");
    _builder.createCondBr(counterGtZero, checkSeqBlock, revInitBlock);

    _builder.setInsertPoint(checkSeqBlock);
    auto* srcLoad = _builder.createLoad(srcStorage, "seq.take.src.load");
    auto* srcTag = _builder.createObjGetTag(srcLoad, "seq.take.src.tag");
    auto* isCons = _builder.createNCmpEQ(srcTag, tag1, "seq.take.is_cons");
    _builder.createCondBr(isCons, bodyBlock, revInitBlock);

    // Body: extract head, force lazy tail, cons head onto accumulator, decrement counter
    _builder.setInsertPoint(bodyBlock);
    auto* srcForHead = _builder.createLoad(srcStorage, "seq.take.src.for_head");
    auto* head = _builder.createObjGetSlot(srcForHead, slot0, "seq.take.head");

    // Force lazy tail to get next seq node
    auto* srcForTail = _builder.createLoad(srcStorage, "seq.take.src.for_tail");
    auto* lazyTail = _builder.createObjGetSlot(srcForTail, slot1, "seq.take.lazy_tail");
    auto* nextSeq = _builder.createLazyForce(lazyTail, "seq.take.next");
    _builder.createStore(srcStorage, nextSeq);

    // Store head and acc in temp allocas to survive ObjAlloc
    auto* elemTmp = createAllocaInEntryBlock(CoreVM::LiteralType::Void, "seq.take.elem.tmp");
    _builder.createStore(elemTmp, head);
    auto* accTmp = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "seq.take.acc.tmp");
    auto* accForCons = _builder.createLoad(accStorage, "seq.take.acc.for_cons");
    _builder.createStore(accTmp, accForCons);

    auto* elemReload = _builder.createLoad(elemTmp, "seq.take.elem.reload");
    auto* accReload = _builder.createLoad(accTmp, "seq.take.acc.reload");
    auto* cons = emitListCons(elemReload, accReload, CoreVM::LiteralType::Void, "seq.take.cons");
    _builder.createStore(accStorage, cons);

    // Decrement counter
    auto* counterLoad2 = _builder.createLoad(counterStorage, "seq.take.counter.load2");
    auto* counterDec = _builder.createSub(counterLoad2, one, "seq.take.counter.dec");
    _builder.createStore(counterStorage, counterDec);

    _builder.createBr(condBlock);

    // Phase 2: Reverse the accumulated list
    auto* revSrcStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "seq.take.rev.src");
    auto* revAccStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "seq.take.rev.acc");

    _builder.setInsertPoint(revInitBlock);
    auto* revSrcInit = _builder.createLoad(accStorage, "seq.take.rev.src.init");
    _builder.createStore(revSrcStorage, revSrcInit);
    auto* revNil = emitNilList(CoreVM::LiteralType::Void, "seq.take.rev.nil");
    _builder.createStore(revAccStorage, revNil);
    _builder.createBr(revCondBlock);

    _builder.setInsertPoint(revCondBlock);
    auto* revSrcLoad = _builder.createLoad(revSrcStorage, "seq.take.rev.src.load");
    auto* revSrcTag = _builder.createObjGetTag(revSrcLoad, "seq.take.rev.src.tag");
    auto* revIsCons = _builder.createNCmpEQ(revSrcTag, tag1, "seq.take.rev.is_cons");
    _builder.createCondBr(revIsCons, revBodyBlock, endBlock);

    _builder.setInsertPoint(revBodyBlock);
    auto* revSrcForHead = _builder.createLoad(revSrcStorage, "seq.take.rev.src.for_head");
    auto* revHead = _builder.createObjGetSlot(revSrcForHead, slot0, "seq.take.rev.head");
    auto* revSrcForTail = _builder.createLoad(revSrcStorage, "seq.take.rev.src.for_tail");
    auto* revTail = _builder.createObjGetSlot(revSrcForTail, slot1, "seq.take.rev.tail");
    _builder.createStore(revSrcStorage, revTail);

    auto* revElemTmp = createAllocaInEntryBlock(CoreVM::LiteralType::Void, "seq.take.rev.elem.tmp");
    _builder.createStore(revElemTmp, revHead);
    auto* revAccTmp = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "seq.take.rev.acc.tmp");
    auto* revAccForCons = _builder.createLoad(revAccStorage, "seq.take.rev.acc.for_cons");
    _builder.createStore(revAccTmp, revAccForCons);

    auto* revElemReload = _builder.createLoad(revElemTmp, "seq.take.rev.elem.reload");
    auto* revAccReload = _builder.createLoad(revAccTmp, "seq.take.rev.acc.reload");
    auto* revCons = emitListCons(revElemReload, revAccReload, CoreVM::LiteralType::Void, "seq.take.rev.cons");

    _builder.createStore(revAccStorage, revCons);
    _builder.createBr(revCondBlock);

    _builder.setInsertPoint(endBlock);
    _result = _builder.createLoad(revAccStorage, "seq.take.result");
    annotateObjectTypeId(_result, CoreVM::BuiltinTypeId::List);
    // Propagate list element type through take (same element type as input seq)
    if (auto elt = getListElementLiteralType(seqValue))
        annotateListElementLiteralType(_result, *elt);
    if (auto elemTypeId = getListElementTypeId(seqValue))
        annotateListElementTypeId(_result, *elemTypeId);
    if (auto innerType = getListElementInnerType(seqValue))
        annotateListElementInnerType(_result, *innerType);
}

void IRGenerator::generateToListIR(CoreVM::Value* seqValue)
{
    auto* tag1 = _builder.get(CoreVM::CoreNumber(1));  // Cons
    auto* slot0 = _builder.get(CoreVM::CoreNumber(0)); // head
    auto* slot1 = _builder.get(CoreVM::CoreNumber(1)); // lazyTail

    // Allocas
    auto* srcStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "toList.src");
    _builder.createStore(srcStorage, seqValue);

    auto* accStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "toList.acc");
    auto* nil = emitNilList(CoreVM::LiteralType::Void, "toList.nil");
    _builder.createStore(accStorage, nil);

    // Phase 1: accumulate elements in reverse order
    auto* condBlock = _builder.createBlock("toList.cond");
    auto* bodyBlock = _builder.createBlock("toList.body");
    auto* revInitBlock = _builder.createBlock("toList.rev.init");
    auto* revCondBlock = _builder.createBlock("toList.rev.cond");
    auto* revBodyBlock = _builder.createBlock("toList.rev.body");
    auto* endBlock = _builder.createBlock("toList.end");

    _builder.createBr(condBlock);

    // Condition: check if seq is Cons
    _builder.setInsertPoint(condBlock);
    auto* srcLoad = _builder.createLoad(srcStorage, "toList.src.load");
    auto* srcTag = _builder.createObjGetTag(srcLoad, "toList.src.tag");
    auto* isCons = _builder.createNCmpEQ(srcTag, tag1, "toList.is_cons");
    _builder.createCondBr(isCons, bodyBlock, revInitBlock);

    // Body: extract head, force lazy tail, cons head onto reversed accumulator
    _builder.setInsertPoint(bodyBlock);
    auto* srcForHead = _builder.createLoad(srcStorage, "toList.src.for_head");
    auto* head = _builder.createObjGetSlot(srcForHead, slot0, "toList.head");

    auto* srcForTail = _builder.createLoad(srcStorage, "toList.src.for_tail");
    auto* lazyTail = _builder.createObjGetSlot(srcForTail, slot1, "toList.lazy_tail");
    auto* nextSeq = _builder.createLazyForce(lazyTail, "toList.next");
    _builder.createStore(srcStorage, nextSeq);

    auto* elemTmp = createAllocaInEntryBlock(CoreVM::LiteralType::Void, "toList.elem.tmp");
    _builder.createStore(elemTmp, head);
    auto* accTmp = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "toList.acc.tmp");
    auto* accForCons = _builder.createLoad(accStorage, "toList.acc.for_cons");
    _builder.createStore(accTmp, accForCons);

    auto* elemReload = _builder.createLoad(elemTmp, "toList.elem.reload");
    auto* accReload = _builder.createLoad(accTmp, "toList.acc.reload");
    auto* cons = emitListCons(elemReload, accReload, CoreVM::LiteralType::Void, "toList.cons");
    _builder.createStore(accStorage, cons);

    _builder.createBr(condBlock);

    // Phase 2: reverse the accumulated list
    auto* revSrcStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "toList.rev.src");
    auto* revAccStorage = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "toList.rev.acc");

    _builder.setInsertPoint(revInitBlock);
    auto* revSrcInit = _builder.createLoad(accStorage, "toList.rev.src.init");
    _builder.createStore(revSrcStorage, revSrcInit);
    auto* revNil = emitNilList(CoreVM::LiteralType::Void, "toList.rev.nil");
    _builder.createStore(revAccStorage, revNil);
    _builder.createBr(revCondBlock);

    _builder.setInsertPoint(revCondBlock);
    auto* revSrcLoad = _builder.createLoad(revSrcStorage, "toList.rev.src.load");
    auto* revSrcTag = _builder.createObjGetTag(revSrcLoad, "toList.rev.src.tag");
    auto* revIsCons = _builder.createNCmpEQ(revSrcTag, tag1, "toList.rev.is_cons");
    _builder.createCondBr(revIsCons, revBodyBlock, endBlock);

    _builder.setInsertPoint(revBodyBlock);
    auto* revSrcForHead = _builder.createLoad(revSrcStorage, "toList.rev.src.for_head");
    auto* revHead = _builder.createObjGetSlot(revSrcForHead, slot0, "toList.rev.head");
    auto* revSrcForTail = _builder.createLoad(revSrcStorage, "toList.rev.src.for_tail");
    auto* revTail = _builder.createObjGetSlot(revSrcForTail, slot1, "toList.rev.tail");
    _builder.createStore(revSrcStorage, revTail);

    auto* revElemTmp = createAllocaInEntryBlock(CoreVM::LiteralType::Void, "toList.rev.elem.tmp");
    _builder.createStore(revElemTmp, revHead);
    auto* revAccTmp = createAllocaInEntryBlock(CoreVM::LiteralType::Object, "toList.rev.acc.tmp");
    auto* revAccForCons = _builder.createLoad(revAccStorage, "toList.rev.acc.for_cons");
    _builder.createStore(revAccTmp, revAccForCons);

    auto* revElemReload = _builder.createLoad(revElemTmp, "toList.rev.elem.reload");
    auto* revAccReload = _builder.createLoad(revAccTmp, "toList.rev.acc.reload");
    auto* revCons = emitListCons(revElemReload, revAccReload, CoreVM::LiteralType::Void, "toList.rev.cons");

    _builder.createStore(revAccStorage, revCons);
    _builder.createBr(revCondBlock);

    _builder.setInsertPoint(endBlock);
    _result = _builder.createLoad(revAccStorage, "toList.result");
    annotateObjectTypeId(_result, CoreVM::BuiltinTypeId::List);
    // Propagate list element type through toList (same element type as input seq)
    if (auto elt = getListElementLiteralType(seqValue))
        annotateListElementLiteralType(_result, *elt);
    if (auto elemTypeId = getListElementTypeId(seqValue))
        annotateListElementTypeId(_result, *elemTypeId);
    if (auto innerType = getListElementInnerType(seqValue))
        annotateListElementInnerType(_result, *innerType);
}

} // namespace endo
