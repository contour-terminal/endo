// SPDX-License-Identifier: Apache-2.0
#include <CoreVM/ir/BasicBlock.hpp>
#include <CoreVM/ir/IRProgram.hpp>
#include <CoreVM/transform/Passes.hpp>
#include <CoreVM/vm/NativeCallback.hpp>
#include <CoreVM/wasm/WasmBuiltins.hpp>
#include <CoreVM/wasm/WasmFunctionLowerer.hpp>
#include <CoreVM/wasm/WasmOpTables.hpp>

#include <array>
#include <format>
#include <ranges>

namespace CoreVM::wasm
{

WasmFunctionLowerer::WasmFunctionLowerer(BinaryenModuleRef module,
                                         WasmOptions const& options,
                                         diagnostics::Report& report,
                                         std::set<RuntimeHelperDef const*>& usedHelpers,
                                         WasmStringTable& strings,
                                         std::unordered_map<int64_t, int64_t> const& slotCounts):
    _module { module },
    _options { options },
    _report { report },
    _usedHelpers { usedHelpers },
    _strings { strings },
    _slotCounts { slotCounts }
{
}

std::string WasmFunctionLowerer::mangledName(std::string_view irName)
{
    return std::format("fn${}", irName);
}

void WasmFunctionLowerer::lower(IRFunction* function)
{
    _function = function;

    // Mandatory lowering: after this pass no SSA value crosses basic-block
    // boundaries except through allocas, and PhiNodes never occur.
    transform::materializeCrossBlockValues(function);

    assignLocals();

    auto* relooper = RelooperCreate(_module);
    for (BasicBlock* block: function->basicBlocks())
    {
        _statements.clear();
        _currentBlock = block;
        for (Instr* instr: block->instructions())
        {
            _currentInstr = instr;
            instr->accept(*this);
        }
        if (block->getTerminator() == nullptr)
            pushStatement(BinaryenUnreachable(_module));
        auto* code = BinaryenBlock(_module,
                                   nullptr,
                                   _statements.data(),
                                   static_cast<BinaryenIndex>(_statements.size()),
                                   BinaryenTypeAuto());
        _relooperBlocks[block] = RelooperAddBlock(relooper, code);
    }

    for (auto const& branch: _pendingBranches)
    {
        auto* condition = [&]() -> BinaryenExpressionRef {
            if (branch.condition == nullptr)
                return nullptr;
            if (branch.caseLabel != nullptr)
                return matchCaseCondition(branch.condition, *branch.caseLabel, branch.matchClass);
            return asCond(emitValue(branch.condition));
        }();
        RelooperAddBranch(_relooperBlocks[branch.from], _relooperBlocks[branch.to], condition, nullptr);
    }

    auto const labelHelper = scratchLocal(BinaryenTypeInt32());
    auto* body = RelooperRenderAndDispose(relooper, _relooperBlocks[function->getEntryBlock()], labelHelper);

    // Safety net: guarantee the body yields the declared i64 result even if
    // all exits go through proc_exit/return (renders as unreachable type).
    auto tail = std::array {
        body,
        BinaryenReturn(_module, BinaryenConst(_module, BinaryenLiteralInt64(0))),
    };
    auto* wrapped = BinaryenBlock(
        _module, nullptr, tail.data(), static_cast<BinaryenIndex>(tail.size()), BinaryenTypeAuto());

    auto params = std::vector<BinaryenType>(_paramCount, BinaryenTypeInt64());
    auto const paramsType = _paramCount == 0
                                ? BinaryenTypeNone()
                                : BinaryenTypeCreate(params.data(), static_cast<BinaryenIndex>(_paramCount));
    BinaryenAddFunction(_module,
                        mangledName(function->name()).c_str(),
                        paramsType,
                        BinaryenTypeInt64(),
                        _varTypes.data(),
                        static_cast<BinaryenIndex>(_varTypes.size()),
                        wrapped);
}

void WasmFunctionLowerer::assignLocals()
{
    // The first parameterCount() allocas (in block/instruction order) are the
    // function parameters; they map to param locals 0..P-1 and contribute no
    // varTypes entry. All later allocas become i64 locals.
    _paramCount = static_cast<uint32_t>(_function->parameterCount());
    auto allocaOrdinal = uint32_t { 0 };
    for (BasicBlock* block: _function->basicBlocks())
    {
        for (Instr* instr: block->instructions())
        {
            auto* alloca = dynamic_cast<AllocaInstr*>(instr);
            if (alloca == nullptr)
                continue;
            if (allocaOrdinal < _paramCount)
            {
                _localIndex[alloca] = allocaOrdinal;
            }
            else
            {
                _localIndex[alloca] = _paramCount + static_cast<uint32_t>(_varTypes.size());
                _varTypes.push_back(BinaryenTypeInt64());
            }
            ++allocaOrdinal;
        }
    }
}

// {{{ value materialization and result integration

BinaryenExpressionRef WasmFunctionLowerer::emitValue(Value* value)
{
    if (auto* constInt = dynamic_cast<ConstantInt*>(value))
        return BinaryenConst(_module, BinaryenLiteralInt64(constInt->get()));

    if (auto* constBool = dynamic_cast<ConstantBoolean*>(value))
        return BinaryenConst(_module, BinaryenLiteralInt64(constBool->get() ? 1 : 0));

    if (auto* constFloat = dynamic_cast<ConstantFloat*>(value))
        return BinaryenConst(_module, BinaryenLiteralFloat64(constFloat->get()));

    if (auto* constString = dynamic_cast<ConstantString*>(value))
        return BinaryenConst(_module,
                             BinaryenLiteralInt64(static_cast<int64_t>(_strings.intern(constString->get()))));

    if (auto* alloca = dynamic_cast<AllocaInstr*>(value))
    {
        if (auto const it = _localIndex.find(alloca); it != _localIndex.end())
            return BinaryenLocalGet(_module, it->second, BinaryenTypeInt64());
        internalError("reference to an unassigned alloca");
        return BinaryenConst(_module, BinaryenLiteralInt64(0));
    }

    if (auto const pinned = _pinned.find(value); pinned != _pinned.end())
        return BinaryenLocalGet(_module, pinned->second.localIndex, pinned->second.type);

    if (auto const mapped = _valueMap.find(value); mapped != _valueMap.end())
    {
        // Binaryen expressions are single-use tree nodes: hand the expression
        // out exactly once. Multi-use values are pinned by setResult instead.
        auto* expr = mapped->second;
        _valueMap.erase(mapped);
        return expr;
    }

    internalError(std::format("operand '{}' has no materialized value", value->name()));
    return BinaryenConst(_module, BinaryenLiteralInt64(0));
}

void WasmFunctionLowerer::setResult(Instr& instr, BinaryenExpressionRef expr, ResultMode mode)
{
    switch (mode)
    {
        case ResultMode::Pure:
            if (!instr.isUsed())
                return; // dead pure value: emit nothing
            if (instr.useCount() > 1)
                pin(instr, expr);
            else
                _valueMap[&instr] = expr;
            return;
        case ResultMode::PinIfUsed:
            if (instr.isUsed())
                pin(instr, expr);
            return;
        case ResultMode::Ordered:
            if (instr.isUsed())
                pin(instr, expr);
            else if (BinaryenExpressionGetType(expr) == BinaryenTypeNone())
                pushStatement(expr);
            else
                pushStatement(BinaryenDrop(_module, expr));
            return;
    }
}

void WasmFunctionLowerer::pin(Value& value, BinaryenExpressionRef expr)
{
    auto const type = BinaryenExpressionGetType(expr);
    auto const localIndex = scratchLocal(type);
    pushStatement(BinaryenLocalSet(_module, localIndex, expr));
    _pinned[&value] = PinnedValue { .localIndex = localIndex, .type = type };
}

void WasmFunctionLowerer::pinValue(Value* value)
{
    if (dynamic_cast<Constant*>(value) != nullptr || dynamic_cast<AllocaInstr*>(value) != nullptr)
        return; // re-materialized freshly on each emitValue()
    if (_pinned.contains(value))
        return;
    if (auto const mapped = _valueMap.find(value); mapped != _valueMap.end())
    {
        auto* expr = mapped->second;
        _valueMap.erase(mapped);
        pin(*value, expr);
        return;
    }
    internalError(std::format("cannot pin value '{}': no materialized expression", value->name()));
}

uint32_t WasmFunctionLowerer::scratchLocal(BinaryenType type)
{
    _varTypes.push_back(type);
    return _paramCount + static_cast<uint32_t>(_varTypes.size()) - 1;
}

BinaryenExpressionRef WasmFunctionLowerer::callRuntime(std::string_view helperName,
                                                       std::span<BinaryenExpressionRef> args)
{
    auto const* helper = findRuntimeHelper(helperName);
    if (helper == nullptr)
    {
        internalError(std::format("unknown runtime helper '{}'", helperName));
        return BinaryenConst(_module, BinaryenLiteralInt64(0));
    }
    _usedHelpers.insert(helper);
    return BinaryenCall(_module,
                        helper->name,
                        args.data(),
                        static_cast<BinaryenIndex>(args.size()),
                        binaryenResultType(*helper));
}

BinaryenExpressionRef WasmFunctionLowerer::zeroI64()
{
    return BinaryenConst(_module, BinaryenLiteralInt64(0));
}

void WasmFunctionLowerer::lowerValueCompare(Instr& instr, BinaryenOp compareOp)
{
    auto* lhs = asI64(emitValue(instr.operand(0)));
    auto* rhs = asI64(emitValue(instr.operand(1)));
    setResult(instr, BinaryenBinary(_module, compareOp, lhs, rhs), ResultMode::Pure);
}

BinaryenExpressionRef WasmFunctionLowerer::asI64(BinaryenExpressionRef expr)
{
    auto const type = BinaryenExpressionGetType(expr);
    if (type == BinaryenTypeFloat64())
        return BinaryenUnary(_module, BinaryenReinterpretFloat64(), expr);
    if (type == BinaryenTypeInt32())
        return BinaryenUnary(_module, BinaryenExtendUInt32(), expr);
    return expr;
}

BinaryenExpressionRef WasmFunctionLowerer::asF64(BinaryenExpressionRef expr)
{
    auto const type = BinaryenExpressionGetType(expr);
    if (type == BinaryenTypeInt64())
        return BinaryenUnary(_module, BinaryenReinterpretInt64(), expr);
    if (type == BinaryenTypeInt32())
        return BinaryenUnary(_module, BinaryenReinterpretInt64(), asI64(expr));
    return expr;
}

BinaryenExpressionRef WasmFunctionLowerer::asCond(BinaryenExpressionRef expr)
{
    auto const type = BinaryenExpressionGetType(expr);
    if (type == BinaryenTypeInt32())
        return expr;
    // Robust against non-canonical booleans: any non-zero i64 is true.
    return BinaryenBinary(
        _module, BinaryenNeInt64(), asI64(expr), BinaryenConst(_module, BinaryenLiteralInt64(0)));
}

// }}}
// {{{ diagnostics

void WasmFunctionLowerer::unsupported(Instr& instr, std::string_view what)
{
    _report.typeError(instr.sourceLocation(), "cannot compile to WebAssembly: {} is not supported", what);
    // Poison the result so lowering can continue and collect further errors.
    if (instr.isUsed())
        _valueMap[&instr] = BinaryenConst(_module, BinaryenLiteralInt64(0));
}

void WasmFunctionLowerer::internalError(std::string_view what)
{
    auto const location = _currentInstr != nullptr ? _currentInstr->sourceLocation() : SourceLocation {};
    _report.typeError(location, "internal error (WASM backend): {}", what);
}

// }}}
// {{{ storage instructions

void WasmFunctionLowerer::visit(NopInstr& instr)
{
    (void) instr;
}

void WasmFunctionLowerer::visit(AllocaInstr& instr)
{
    // Locals are pre-assigned in assignLocals(); no code is emitted here.
    auto const* size = dynamic_cast<ConstantInt*>(instr.arraySize());
    if (size == nullptr || size->get() != 1)
        unsupported(instr, "array allocation");
}

void WasmFunctionLowerer::visit(StoreInstr& instr)
{
    if (instr.index()->get() != 0)
    {
        unsupported(instr, "indexed store (array element assignment)");
        return;
    }
    auto* alloca = dynamic_cast<AllocaInstr*>(instr.variable());
    if (alloca == nullptr)
    {
        internalError("store target is not an alloca");
        return;
    }
    auto const it = _localIndex.find(alloca);
    if (it == _localIndex.end())
    {
        internalError("store target alloca has no local index");
        return;
    }
    pushStatement(BinaryenLocalSet(_module, it->second, asI64(emitValue(instr.source()))));
}

void WasmFunctionLowerer::visit(LoadInstr& instr)
{
    auto* alloca = dynamic_cast<AllocaInstr*>(instr.variable());
    if (alloca == nullptr)
    {
        unsupported(instr, "load from a non-local variable");
        return;
    }
    auto const it = _localIndex.find(alloca);
    if (it == _localIndex.end())
    {
        internalError("loaded alloca has no local index");
        return;
    }
    // Pin the loaded value now: the local may be re-stored later in this
    // block, so the load must observe the value at this program point.
    setResult(instr, BinaryenLocalGet(_module, it->second, BinaryenTypeInt64()), ResultMode::PinIfUsed);
}

void WasmFunctionLowerer::visit(PhiNode& instr)
{
    (void) instr;
    // materializeCrossBlockValues() eliminates all cross-block SSA values.
    internalError("unexpected PhiNode after cross-block value materialization");
}

// }}}
// {{{ calls

void WasmFunctionLowerer::visit(CallInstr& instr)
{
    auto const signature = instr.callee()->signature().to_s();
    auto const* builtin = findWasmBuiltin(signature);
    if (builtin == nullptr)
    {
        unsupported(instr, std::format("builtin '{}'", signature));
        return;
    }

    // Operand 0 is the callee; the arguments follow.
    auto args = std::vector<BinaryenExpressionRef> {};
    args.reserve(instr.operands().size() - 1);
    for (auto* operand: instr.operands() | std::views::drop(1))
        args.push_back(asI64(emitValue(operand)));

    // Inline lowerings produce no meaningful value; IR may still reference the
    // call result (e.g. as an if-arm value), so map a dummy for those uses.
    auto const mapDummyResultForUses = [&]() {
        if (instr.isUsed())
            setResult(instr, zeroI64(), ResultMode::Pure);
    };

    switch (builtin->inlineOp)
    {
        case BuiltinInlineOp::ProcExit: {
            auto exitCode = std::array { BinaryenUnary(
                _module, BinaryenWrapInt64(), args.empty() ? zeroI64() : args[0]) };
            pushStatement(BinaryenCall(_module,
                                       "proc_exit",
                                       exitCode.data(),
                                       static_cast<BinaryenIndex>(exitCode.size()),
                                       BinaryenTypeNone()));
            pushStatement(BinaryenUnreachable(_module));
            mapDummyResultForUses();
            return;
        }
        case BuiltinInlineOp::SetExitStatus:
            pushStatement(BinaryenGlobalSet(
                _module,
                layout::ExitStatusGlobal,
                BinaryenUnary(_module, BinaryenWrapInt64(), args.empty() ? zeroI64() : args[0])));
            mapDummyResultForUses();
            return;
        case BuiltinInlineOp::Ignore: mapDummyResultForUses(); return;
        case BuiltinInlineOp::ObjectToString: {
            // Compile-time dispatch where the value is unambiguous; everything
            // else goes through the runtime pointer/number classifier. Note
            // that a Number-typed argument may still be an object pointer
            // (native callbacks return object pointers as Number), so only
            // constants and statically String/Boolean-typed values short-cut.
            auto* argument = instr.operands().size() > 1 ? instr.operand(1) : nullptr;
            auto const argumentType = argument != nullptr ? argument->type() : LiteralType::Void;
            if (argumentType == LiteralType::String)
            {
                setResult(instr, args.empty() ? zeroI64() : args[0], ResultMode::Pure);
                return;
            }
            auto const isConstantNumeric = dynamic_cast<ConstantInt*>(argument) != nullptr
                                           || dynamic_cast<ConstantBoolean*>(argument) != nullptr;
            auto const* helper = (isConstantNumeric || argumentType == LiteralType::Boolean)
                                     ? "endo_i64_to_str"
                                     : "endo_object_to_string";
            setResult(instr, callRuntime(helper, args), ResultMode::Ordered);
            return;
        }
        case BuiltinInlineOp::None: break;
    }

    auto* result = callRuntime(builtin->runtimeHelper, args);
    if (instr.callee()->signature().returnType() == LiteralType::Void)
        pushStatement(result);
    else
        setResult(instr, result, ResultMode::Ordered);
}

void WasmFunctionLowerer::visit(FunctionCallInstr& instr)
{
    if (instr.callee()->empty())
    {
        unsupported(instr, "calling a function without a compiled body");
        return;
    }
    auto args = std::vector<BinaryenExpressionRef> {};
    args.reserve(instr.operands().size());
    for (auto* operand: instr.operands())
        args.push_back(asI64(emitValue(operand)));
    auto* call = BinaryenCall(_module,
                              mangledName(instr.callee()->name()).c_str(),
                              args.data(),
                              static_cast<BinaryenIndex>(args.size()),
                              BinaryenTypeInt64());
    setResult(instr, call, ResultMode::Ordered);
}

void WasmFunctionLowerer::visit(FunctionRetInstr& instr)
{
    auto* result = instr.result() != nullptr ? asI64(emitValue(instr.result())) : zeroI64();
    pushStatement(BinaryenReturn(_module, result));
}

void WasmFunctionLowerer::visit(TailCallInstr& instr)
{
    if (instr.callee()->empty())
    {
        unsupported(instr, "tail-calling a function without a compiled body");
        pushStatement(BinaryenUnreachable(_module));
        return;
    }
    auto args = std::vector<BinaryenExpressionRef> {};
    args.reserve(instr.operands().size());
    for (auto* operand: instr.operands())
        args.push_back(asI64(emitValue(operand)));
    auto const callee = mangledName(instr.callee()->name());
    if (_options.tailCalls)
    {
        pushStatement(BinaryenReturnCall(_module,
                                         callee.c_str(),
                                         args.data(),
                                         static_cast<BinaryenIndex>(args.size()),
                                         BinaryenTypeInt64()));
    }
    else
    {
        // Portable fallback for runtimes without the tail-call proposal;
        // deep recursion may exhaust the value stack here.
        auto* call = BinaryenCall(_module,
                                  callee.c_str(),
                                  args.data(),
                                  static_cast<BinaryenIndex>(args.size()),
                                  BinaryenTypeInt64());
        pushStatement(BinaryenReturn(_module, call));
    }
}

void WasmFunctionLowerer::visit(IndirectCallInstr& instr)
{
    unsupported(instr, "closures and function values (indirect calls)");
}

void WasmFunctionLowerer::visit(IndirectTailCallInstr& instr)
{
    unsupported(instr, "closures and function values (indirect tail calls)");
    pushStatement(BinaryenUnreachable(_module));
}

void WasmFunctionLowerer::visit(FunctionRefInstr& instr)
{
    unsupported(instr, "function references (closures)");
}

// }}}
// {{{ terminators

void WasmFunctionLowerer::visit(CondBrInstr& instr)
{
    pinValue(instr.condition());
    _pendingBranches.push_back(
        PendingBranch { .from = _currentBlock, .to = instr.trueBlock(), .condition = instr.condition() });
    _pendingBranches.push_back(
        PendingBranch { .from = _currentBlock, .to = instr.falseBlock(), .condition = nullptr });
}

void WasmFunctionLowerer::visit(BrInstr& instr)
{
    _pendingBranches.push_back(
        PendingBranch { .from = _currentBlock, .to = instr.targetBlock(), .condition = nullptr });
}

void WasmFunctionLowerer::visit(RetInstr& instr)
{
    // RetInstr terminates the *program* (VM: EXIT), not the current function.
    // The shell exit-status policy (status global wins, operand collapses to
    // 1/0) lives in the endo_exit runtime helper.
    auto args = std::array { instr.operands().empty() ? zeroI64() : asI64(emitValue(instr.operand(0))) };
    pushStatement(callRuntime("endo_exit", args));
    pushStatement(BinaryenUnreachable(_module));
}

void WasmFunctionLowerer::visit(MatchInstr& instr)
{
    if (instr.op() == MatchClass::RegExp)
    {
        unsupported(instr, "regular expression matching");
        pushStatement(BinaryenUnreachable(_module));
        return;
    }
    if (instr.elseBlock() == nullptr)
    {
        internalError("match dispatch without an else block");
        pushStatement(BinaryenUnreachable(_module));
        return;
    }

    pinValue(instr.condition());
    // Relooper evaluates branch conditions in insertion order, giving
    // first-match case semantics.
    for (auto const& [label, block]: instr.cases())
        _pendingBranches.push_back(PendingBranch { .from = _currentBlock,
                                                   .to = block,
                                                   .condition = instr.condition(),
                                                   .caseLabel = label,
                                                   .matchClass = instr.op() });
    _pendingBranches.push_back(
        PendingBranch { .from = _currentBlock, .to = instr.elseBlock(), .condition = nullptr });
}

BinaryenExpressionRef WasmFunctionLowerer::matchCaseCondition(Value* scrutinee,
                                                              Constant& caseLabel,
                                                              MatchClass matchClass)
{
    auto* lhs = emitValue(scrutinee);
    auto* rhs = emitValue(&caseLabel);
    switch (matchClass)
    {
        case MatchClass::Same:
            // String labels compare by content (the VM keys its match tables
            // by string value, not by interned pointer identity).
            if (dynamic_cast<ConstantString*>(&caseLabel) != nullptr)
            {
                auto args = std::array { asI64(lhs), asI64(rhs) };
                return asCond(callRuntime("endo_str_eq", args));
            }
            return BinaryenBinary(_module, BinaryenEqInt64(), asI64(lhs), asI64(rhs));
        case MatchClass::Head: {
            auto args = std::array { asI64(lhs), asI64(rhs) };
            return asCond(callRuntime("endo_str_starts_with", args));
        }
        case MatchClass::Tail: {
            auto args = std::array { asI64(lhs), asI64(rhs) };
            return asCond(callRuntime("endo_str_ends_with", args));
        }
        case MatchClass::RegExp: break; // rejected in visit(MatchInstr)
    }
    internalError("unexpected match class");
    return BinaryenConst(_module, BinaryenLiteralInt32(0));
}

// }}}
// {{{ operator families (data-driven lowering; tables filled in later milestones)

void WasmFunctionLowerer::lowerUnaryOp(Instr& instr, UnaryOperator op)
{
    auto const& lowering = unaryOpLowering(op);
    auto const coerce = [&](BinaryenExpressionRef expr) {
        return lowering.floatOperand ? asF64(expr) : asI64(expr);
    };

    switch (lowering.kind)
    {
        case UnaryOpLowering::Kind::DirectOp:
            setResult(instr,
                      BinaryenUnary(_module, lowering.op(), coerce(emitValue(instr.operand(0)))),
                      ResultMode::Pure);
            return;
        case UnaryOpLowering::Kind::SubFromZero:
            setResult(
                instr,
                BinaryenBinary(_module, BinaryenSubInt64(), zeroI64(), asI64(emitValue(instr.operand(0)))),
                ResultMode::Pure);
            return;
        case UnaryOpLowering::Kind::XorImmediate:
            setResult(instr,
                      BinaryenBinary(_module,
                                     BinaryenXorInt64(),
                                     asI64(emitValue(instr.operand(0))),
                                     BinaryenConst(_module, BinaryenLiteralInt64(lowering.immediate))),
                      ResultMode::Pure);
            return;
        case UnaryOpLowering::Kind::HelperCall: {
            auto args = std::array { coerce(emitValue(instr.operand(0))) };
            setResult(instr, callRuntime(lowering.helper, args), ResultMode::Ordered);
            return;
        }
        case UnaryOpLowering::Kind::HelperIsZero: {
            auto args = std::array { coerce(emitValue(instr.operand(0))) };
            setResult(instr,
                      BinaryenUnary(_module, BinaryenEqZInt64(), callRuntime(lowering.helper, args)),
                      ResultMode::Ordered);
            return;
        }
        case UnaryOpLowering::Kind::Unsupported: break;
    }
    unsupported(instr, std::format("operator '{}' ({})", cstr(op), lowering.unsupportedWhat));
}

void WasmFunctionLowerer::lowerBinaryOp(Instr& instr, BinaryOperator op)
{
    auto const& lowering = binaryOpLowering(op);
    auto const coerce = [&](BinaryenExpressionRef expr) {
        return lowering.floatOperands ? asF64(expr) : asI64(expr);
    };

    switch (lowering.kind)
    {
        case BinaryOpLowering::Kind::DirectOp: {
            auto* lhs = coerce(emitValue(instr.operand(0)));
            auto* rhs = coerce(emitValue(instr.operand(1)));
            setResult(instr, BinaryenBinary(_module, lowering.op(), lhs, rhs), ResultMode::Pure);
            return;
        }
        case BinaryOpLowering::Kind::HelperCall: {
            auto args =
                std::array { coerce(emitValue(instr.operand(0))), coerce(emitValue(instr.operand(1))) };
            setResult(instr, callRuntime(lowering.helper, args), ResultMode::Ordered);
            return;
        }
        case BinaryOpLowering::Kind::HelperCmp0: {
            auto args =
                std::array { coerce(emitValue(instr.operand(0))), coerce(emitValue(instr.operand(1))) };
            setResult(instr,
                      BinaryenBinary(_module, lowering.op(), callRuntime(lowering.helper, args), zeroI64()),
                      ResultMode::Ordered);
            return;
        }
        case BinaryOpLowering::Kind::Unsupported: break;
    }
    unsupported(instr, std::format("operator '{}' ({})", cstr(op), lowering.unsupportedWhat));
}

// }}}
// {{{ casts, regexp

void WasmFunctionLowerer::visit(CastInstr& instr)
{
    auto const targetType = instr.type();
    auto const sourceType = instr.source()->type();

    // Same-type casts are aliases.
    if (targetType == sourceType)
    {
        setResult(instr, emitValue(instr.source()), ResultMode::Pure);
        return;
    }

    auto const* rule = castLowering(targetType, sourceType);
    if (rule == nullptr)
    {
        unsupported(instr, std::format("conversion from {} to {}", tos(sourceType), tos(targetType)));
        return;
    }

    switch (rule->kind)
    {
        case CastLowering::Kind::HelperI64: {
            auto args = std::array { asI64(emitValue(instr.source())) };
            setResult(instr, callRuntime(rule->helper, args), ResultMode::Ordered);
            return;
        }
        case CastLowering::Kind::HelperF64: {
            auto args = std::array { asF64(emitValue(instr.source())) };
            setResult(instr, callRuntime(rule->helper, args), ResultMode::Ordered);
            return;
        }
        case CastLowering::Kind::TruncSat:
            setResult(
                instr,
                BinaryenUnary(_module, BinaryenTruncSatSFloat64ToInt64(), asF64(emitValue(instr.source()))),
                ResultMode::Pure);
            return;
        case CastLowering::Kind::Convert:
            setResult(
                instr,
                BinaryenUnary(_module, BinaryenConvertSInt64ToFloat64(), asI64(emitValue(instr.source()))),
                ResultMode::Pure);
            return;
    }
}

void WasmFunctionLowerer::visit(RegExpGroupInstr& instr)
{
    unsupported(instr, "regular expressions");
}

// }}}
// {{{ object operations
//
// Objects live in linear memory with the unified 8-byte header (see
// WasmRuntimeABI.hpp); slot/tag/typeId access is inlined address math.

uint32_t WasmFunctionLowerer::pinObjectOperand(Value* object)
{
    auto const localIndex = scratchLocal(BinaryenTypeInt64());
    pushStatement(BinaryenLocalSet(_module, localIndex, asI64(emitValue(object))));
    return localIndex;
}

BinaryenExpressionRef WasmFunctionLowerer::pinnedPointer(uint32_t localIndex)
{
    return BinaryenUnary(
        _module, BinaryenWrapInt64(), BinaryenLocalGet(_module, localIndex, BinaryenTypeInt64()));
}

void WasmFunctionLowerer::visit(ObjAllocInstr& instr)
{
    auto const typeId = instr.typeId()->get();
    auto const slotCount = _slotCounts.find(typeId);
    if (slotCount == _slotCounts.end())
    {
        unsupported(instr, std::format("allocation of unknown object type id {}", typeId));
        return;
    }
    auto args = std::array {
        BinaryenConst(_module, BinaryenLiteralInt64(typeId)),
        BinaryenConst(_module, BinaryenLiteralInt64(slotCount->second)),
    };
    setResult(instr, callRuntime("endo_obj_alloc", args), ResultMode::Ordered);
}

void WasmFunctionLowerer::visit(ObjRetainInstr& instr)
{
    // No reference counting: the bump allocator never frees. The retain's
    // result aliases the object operand.
    setResult(instr, asI64(emitValue(instr.object())), ResultMode::Pure);
}

void WasmFunctionLowerer::visit(ObjReleaseInstr& instr)
{
    // No reference counting (see ObjRetainInstr).
    (void) instr;
}

BinaryenExpressionRef WasmFunctionLowerer::loadObjectField(Value* object,
                                                           uint32_t bytes,
                                                           uint32_t offset,
                                                           BinaryenType type)
{
    return BinaryenLoad(_module,
                        bytes,
                        false,
                        offset,
                        0,
                        type,
                        BinaryenUnary(_module, BinaryenWrapInt64(), asI64(emitValue(object))),
                        "0");
}

void WasmFunctionLowerer::visit(ObjGetTagInstr& instr)
{
    setResult(instr,
              loadObjectField(instr.object(), 1, layout::TagOffset, BinaryenTypeInt32()),
              ResultMode::PinIfUsed);
}

void WasmFunctionLowerer::visit(ObjSetTagInstr& instr)
{
    auto const object = pinObjectOperand(instr.object());
    auto* tag = BinaryenUnary(_module, BinaryenWrapInt64(), asI64(emitValue(instr.tag())));
    pushStatement(BinaryenStore(
        _module, 1, layout::TagOffset, 0, pinnedPointer(object), tag, BinaryenTypeInt32(), "0"));
    // The instruction's Object-typed result aliases the object operand.
    _pinned[&instr] = PinnedValue { .localIndex = object, .type = BinaryenTypeInt64() };
}

void WasmFunctionLowerer::visit(ObjGetSlotInstr& instr)
{
    auto const offset =
        layout::HeaderSize + (layout::SlotSize * static_cast<uint32_t>(instr.slotIndex()->get()));
    setResult(instr, loadObjectField(instr.object(), 8, offset, BinaryenTypeInt64()), ResultMode::PinIfUsed);
}

void WasmFunctionLowerer::visit(ObjSetSlotInstr& instr)
{
    auto const offset =
        layout::HeaderSize + (layout::SlotSize * static_cast<uint32_t>(instr.slotIndex()->get()));
    auto const object = pinObjectOperand(instr.object());
    pushStatement(BinaryenStore(_module,
                                8,
                                offset,
                                0,
                                pinnedPointer(object),
                                asI64(emitValue(instr.value())),
                                BinaryenTypeInt64(),
                                "0"));
    // The instruction's Object-typed result aliases the object operand.
    _pinned[&instr] = PinnedValue { .localIndex = object, .type = BinaryenTypeInt64() };
}

void WasmFunctionLowerer::visit(ObjTypeIdInstr& instr)
{
    setResult(instr,
              loadObjectField(instr.object(), 2, layout::TypeIdOffset, BinaryenTypeInt32()),
              ResultMode::PinIfUsed);
}

void WasmFunctionLowerer::visit(ObjIsTypeInstr& instr)
{
    auto* test = BinaryenBinary(
        _module,
        BinaryenEqInt32(),
        loadObjectField(instr.object(), 2, layout::TypeIdOffset, BinaryenTypeInt32()),
        BinaryenConst(_module, BinaryenLiteralInt32(static_cast<int32_t>(instr.typeId()->get()))));
    setResult(instr, test, ResultMode::PinIfUsed);
}

// }}}
// {{{ dynamic value comparison (raw canonical i64 values, mirroring the VM)

void WasmFunctionLowerer::visit(VCmpEQInstr& instr)
{
    lowerValueCompare(instr, BinaryenEqInt64());
}

void WasmFunctionLowerer::visit(VCmpNEInstr& instr)
{
    lowerValueCompare(instr, BinaryenNeInt64());
}

void WasmFunctionLowerer::visit(VCmpLTInstr& instr)
{
    lowerValueCompare(instr, BinaryenLtSInt64());
}

void WasmFunctionLowerer::visit(VCmpLEInstr& instr)
{
    lowerValueCompare(instr, BinaryenLeSInt64());
}

void WasmFunctionLowerer::visit(VCmpGTInstr& instr)
{
    lowerValueCompare(instr, BinaryenGtSInt64());
}

void WasmFunctionLowerer::visit(VCmpGEInstr& instr)
{
    lowerValueCompare(instr, BinaryenGeSInt64());
}

// }}}
// {{{ lazy evaluation

void WasmFunctionLowerer::visit(LazyForceInstr& instr)
{
    unsupported(instr, "lazy sequences");
}

// }}}

} // namespace CoreVM::wasm
