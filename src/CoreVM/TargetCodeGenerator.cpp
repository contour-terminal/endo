// SPDX-License-Identifier: Apache-2.0
#include <CoreVM/CoreVM.hpp>
#include <CoreVM/util.hpp>
#include <CoreVM/util/assert.hpp>

#include <array>
#include <cstdarg>
#include <limits>
#include <memory>
#include <optional>
#include <print>
#include <unordered_map>
#include <vector>

namespace CoreVM
{

#define GLOBAL_SCOPE_INIT_NAME "@__global_init__"

template <typename T, typename S>
std::vector<T> convert(const std::vector<Constant*>& source)
{
    std::vector<T> target(source.size());

    for (size_t i = 0, e = source.size(); i != e; ++i)
        target[i] = static_cast<S*>(source[i])->get();

    return target;
}

TargetCodeGenerator::TargetCodeGenerator(): _handlerId(0)
{
}

std::unique_ptr<Program> TargetCodeGenerator::generate(IRProgram* programIR)
{
    // generate target code for global scope initialization, if any
    IRHandler* init = programIR->findHandler(GLOBAL_SCOPE_INIT_NAME);
    if (init != nullptr)
        generate(init);

    for (IRHandler* handler: programIR->handlers())
        if (handler != init)
            generate(handler);

    _cp.setModules(programIR->modules());

    return std::make_unique<Program>(std::move(_cp));
}

void TargetCodeGenerator::generate(IRHandler* handler)
{
    // explicitely forward-declare handler, so we can use its ID internally.
    _handlerId = _cp.makeHandler(handler);

    std::unordered_map<BasicBlock*, size_t> basicBlockEntryPoints;

    // Reset stack, alloca, and location tracking for this handler
    _stack.clear();
    _allocaIndices.clear();
    _allocaCount = 0;
    _locationTable.clear();
    _lastRecordedLocation = {};

    // First pass: assign fixed indices to all allocas.
    // This ensures allocas have consistent indices regardless of instruction order.
    for (BasicBlock* bb: handler->basicBlocks())
    {
        for (Instr* instr: bb->instructions())
        {
            if (auto* allocaInstr = dynamic_cast<AllocaInstr*>(instr))
            {
                if (allocaInstr->getBasicBlock()->getHandler()->name() != GLOBAL_SCOPE_INIT_NAME)
                {
                    _allocaIndices[allocaInstr] = _allocaCount++;
                }
            }
        }
    }

    bool isFirstBlock = true;

    // generate code for all basic blocks, sequentially
    for (BasicBlock* bb: handler->basicBlocks())
    {
        if (isFirstBlock)
        {
            isFirstBlock = false;
        }
        else if (_allocaCount > 0 && _stack.size() > _allocaCount)
        {
            // At block boundaries, reset stack to contain only allocas.
            // Allocas have fixed indices 0..allocaCount-1.
            // Any entries beyond that are temporaries that don't persist across paths.
            //
            // Emit DISCARD to pop the physical stack elements AND reset tracking.
            // This is critical: the physical stack might have leftover values from
            // predecessor blocks, and we need to clean them up.
            size_t extraCount = _stack.size() - _allocaCount;
            emitInstr(Opcode::DISCARD, extraCount);
            while (_stack.size() > _allocaCount)
            {
                _stack.pop_back();
            }
        }

        basicBlockEntryPoints[bb] = getInstructionPointer();
        for (Instr* instr: bb->instructions())
        {
            // Record source location if it changed (sparse location table)
            SourceLocation const& loc = instr->sourceLocation();
            if (!loc.filename.empty() && loc != _lastRecordedLocation)
            {
                _locationTable.emplace_back(getInstructionPointer(), loc);
                _lastRecordedLocation = loc;
            }
            instr->accept(*this);
        }
    }

    // fixiate conditional jump instructions
    for (const auto& target: _conditionalJumps)
    {
        size_t targetPC = basicBlockEntryPoints[target.first];
        for (const auto& source: target.second)
        {
            _code[source.pc] = makeInstruction(source.opcode, targetPC);
        }
    }
    _conditionalJumps.clear();

    // fixiate unconditional jump instructions
    for (const auto& target: _unconditionalJumps)
    {
        size_t targetPC = basicBlockEntryPoints[target.first];
        for (const auto& source: target.second)
        {
            _code[source.pc] = makeInstruction(source.opcode, targetPC);
        }
    }
    _unconditionalJumps.clear();

    // fixiate match jump table
    for (const auto& hint: _matchHints)
    {
        size_t matchId = hint.second;
        MatchInstr* matchInstr = hint.first;
        const auto& cases = matchInstr->cases();
        MatchDef& def = _cp.getMatchDef(matchId);

        for (size_t i = 0, e = cases.size(); i != e; ++i)
        {
            def.cases[i].pc = basicBlockEntryPoints[cases[i].second];
        }

        if (matchInstr->elseBlock())
        {
            def.elsePC = basicBlockEntryPoints[matchInstr->elseBlock()];
        }
    }
    _matchHints.clear();

    _cp.getHandler(_handlerId).second = std::move(_code);

    // Store the sparse location table for this handler
    if (!_locationTable.empty())
        _cp.setHandlerLocationTable(_handlerId, std::move(_locationTable));

    // cleanup remaining handler-local work vars
    // COREVM_TRACE("CoreVM: stack depth after handler code generation: {}", _stack.size());
    _stack.clear();
}

void TargetCodeGenerator::emitInstr(Instruction instr)
{
    _code.push_back(instr);
}

void TargetCodeGenerator::emitCondJump(Opcode opcode, BasicBlock* bb)
{
    const auto pc = getInstructionPointer();
    emitInstr(opcode);
    changeStack(1, nullptr);
    _conditionalJumps[bb].push_back({ pc, opcode });
}

void TargetCodeGenerator::emitJump(BasicBlock* bb)
{
    const auto pc = getInstructionPointer();
    emitInstr(Opcode::JMP);
    _unconditionalJumps[bb].push_back({ pc, Opcode::JMP });
}

void TargetCodeGenerator::emitBinary(Instr& binaryInstr, Opcode opcode)
{
    // emit operands only if not already on stack in ordered form and just used by this instruction.
    if (!(_stack.size() >= 2 && binaryInstr.operand(0) == _stack[_stack.size() - 2]
          && binaryInstr.operand(1) == _stack[_stack.size() - 1] && binaryInstr.operand(0)->useCount() == 1
          && binaryInstr.operand(1)->useCount() == 1))
    {
        emitLoad(binaryInstr.operand(0));
        emitLoad(binaryInstr.operand(1));
    }

    emitInstr(opcode);
    changeStack(2, &binaryInstr);
}

void TargetCodeGenerator::emitBinaryAssoc(Instr& binaryInstr, Opcode opcode)
{
    // TODO: switch lhs and rhs if lhs is const and rhs is not
    // TODO: revive stack/imm opcodes
    emitBinary(binaryInstr, opcode);
}

void TargetCodeGenerator::emitUnary(Instr& unaryInstr, Opcode opcode)
{
    emitLoad(unaryInstr.operand(0));
    emitInstr(opcode);
    changeStack(1, &unaryInstr);
}

StackPointer TargetCodeGenerator::getStackPointer(const Value* value)
{
    // First check if this is an alloca with a pre-assigned index
    auto it = _allocaIndices.find(value);
    if (it != _allocaIndices.end())
        return it->second;

    // Otherwise search in the current stack
    for (size_t i = 0, e = _stack.size(); i != e; ++i)
        if (_stack[i] == value)
            return i;

    // ((Value*) value)->dump();
    return (StackPointer) -1;
}

void TargetCodeGenerator::changeStack(size_t pops, const Value* pushValue)
{
    if (pops)
        pop(pops);

    if (pushValue)
        push(pushValue);
}

void TargetCodeGenerator::pop(size_t count)
{
    COREVM_ASSERT(count <= _stack.size(), "CoreVM: BUG: stack smaller than amount of elements to pop.");

    for (size_t i = 0; i != count; i++)
    {
        _stack.pop_back();
    }
}

void TargetCodeGenerator::push(const Value* alias)
{
    _stack.push_back(alias);
}

// {{{ instruction code generation
void TargetCodeGenerator::visit(NopInstr& /*nopInstr*/)
{
    emitInstr(Opcode::NOP);
}

void TargetCodeGenerator::visit(AllocaInstr& allocaInstr)
{
    if (allocaInstr.getBasicBlock()->getHandler()->name() == GLOBAL_SCOPE_INIT_NAME)
    {
        emitInstr(Opcode::GALLOCA, 1);
        _globals.push_back(&allocaInstr);
    }
    else
    {
        emitInstr(Opcode::ALLOCA, 1);
        // Push the alloca to the stack. Its index was pre-assigned in the first pass.
        push(&allocaInstr);
    }
}

std::optional<size_t> TargetCodeGenerator::findGlobal(const Value* variable) const
{
    for (size_t i = 0, e = _globals.size(); i != e; ++i)
        if (_globals[i] == variable)
            return i;

    return std::nullopt;
}

// variable = expression
void TargetCodeGenerator::visit(StoreInstr& storeInstr)
{
    if (std::optional<size_t> gi = findGlobal(storeInstr.variable()); gi.has_value())
    {
        emitLoad(storeInstr.source());
        emitInstr(Opcode::GSTORE, *gi);
        changeStack(1, nullptr);
        return;
    }

    StackPointer di = getStackPointer(storeInstr.variable());
    COREVM_ASSERT(di != size_t(-1), "BUG: StoreInstr.variable not found on stack");

    if (storeInstr.source()->uses().size() == 1 && _stack.back() == storeInstr.source())
    {
        emitInstr(Opcode::STORE, di);
        changeStack(1, nullptr);
    }
    else
    {
        emitLoad(storeInstr.source());
        emitInstr(Opcode::STORE, di);
        changeStack(1, nullptr);
    }
}

void TargetCodeGenerator::visit(LoadInstr& loadInstr)
{
    if (std::optional<size_t> gi = findGlobal(loadInstr.variable()); gi.has_value())
    {
        emitInstr(Opcode::GLOAD, *gi);
        changeStack(0, &loadInstr);
        return;
    }

    StackPointer si = getStackPointer(loadInstr.variable());
    COREVM_ASSERT(si != static_cast<size_t>(-1),
                  "BUG: emitLoad: LoadInstr with variable() not yet on the stack.");

    emitInstr(Opcode::LOAD, si);
    changeStack(0, &loadInstr);
}

void TargetCodeGenerator::visit(CallInstr& callInstr)
{
    const int argc = static_cast<int>(callInstr.operands().size()) - 1;
    for (int i = 1; i <= argc; ++i)
        emitLoad(callInstr.operand(i));

    const bool returnsValue = callInstr.callee()->signature().returnType() != LiteralType::Void;

    emitInstr(Opcode::CALL,
              _cp.makeNativeFunction(callInstr.callee()),
              callInstr.operands().size() - 1,
              returnsValue ? 1 : 0);

    if (argc)
        pop(argc);

    if (returnsValue)
    {
        push(&callInstr);

        if (!callInstr.isUsed())
        {
            emitInstr(Opcode::DISCARD, 1);
            pop(1);
        }
    }
}

void TargetCodeGenerator::visit(HandlerCallInstr& handlerCallInstr)
{
    int argc = static_cast<int>(handlerCallInstr.operands().size()) - 1;
    for (int i = 1; i <= argc; ++i)
        emitLoad(handlerCallInstr.operand(i));

    emitInstr(Opcode::HANDLER,
              _cp.makeNativeHandler(handlerCallInstr.callee()),
              handlerCallInstr.operands().size() - 1);

    if (argc)
        pop(argc);
}

Operand TargetCodeGenerator::getConstantInt(Value* value)
{
    COREVM_ASSERT(dynamic_cast<ConstantInt*>(value) != nullptr, "Must be ConstantInt");
    return static_cast<ConstantInt*>(value)->get();
}

void TargetCodeGenerator::emitLoad(Value* value)
{
    assert(value != nullptr);

    // const int
    if (auto* integer = dynamic_cast<ConstantInt*>(value))
    {
        // FIXME this constant initialization should pretty much be done in the entry block
        CoreNumber number = integer->get();
        // Only use ILOAD for non-negative numbers that fit in Operand (uint16_t)
        if (number >= 0 && static_cast<uint64_t>(number) <= std::numeric_limits<Operand>::max())
        {
            emitInstr(Opcode::ILOAD, static_cast<Operand>(number));
            changeStack(0, value);
        }
        else
        {
            emitInstr(Opcode::NLOAD, _cp.makeInteger(number));
            changeStack(0, value);
        }
        return;
    }

    // const boolean
    if (auto* boolean = dynamic_cast<ConstantBoolean*>(value))
    {
        emitInstr(Opcode::ILOAD, boolean->get());
        changeStack(0, value);
        return;
    }

    // const string
    if (auto* str = dynamic_cast<ConstantString*>(value))
    {
        emitInstr(Opcode::SLOAD, _cp.makeString(str->get()));
        changeStack(0, value);
        return;
    }

    // const ip
    if (auto* ip = dynamic_cast<ConstantIP*>(value))
    {
        emitInstr(Opcode::PLOAD, _cp.makeIPAddress(ip->get()));
        changeStack(0, value);
        return;
    }

    // const cidr
    if (auto* cidr = dynamic_cast<ConstantCidr*>(value))
    {
        emitInstr(Opcode::CLOAD, _cp.makeCidr(cidr->get()));
        changeStack(0, value);
        return;
    }

    // const array<T>
    if (auto* array = dynamic_cast<ConstantArray*>(value))
    {
        switch (array->type())
        {
            case LiteralType::IntArray:
                emitInstr(Opcode::ITLOAD,
                          _cp.makeIntegerArray(convert<CoreNumber, ConstantInt>(array->get())));
                changeStack(0, value);
                break;
            case LiteralType::StringArray:
                emitInstr(Opcode::STLOAD,
                          _cp.makeStringArray(convert<std::string, ConstantString>(array->get())));
                changeStack(0, value);
                break;
            case LiteralType::IPAddrArray:
                emitInstr(Opcode::PTLOAD,
                          _cp.makeIPaddrArray(convert<util::IPAddress, ConstantIP>(array->get())));
                changeStack(0, value);
                break;
            case LiteralType::CidrArray:
                emitInstr(Opcode::CTLOAD, _cp.makeCidrArray(convert<util::Cidr, ConstantCidr>(array->get())));
                changeStack(0, value);
                break;
            default: fprintf(stderr, "BUG: Unsupported array type in target code generator."); abort();
        }
        return;
    }

    // const regex
    if (auto* re = dynamic_cast<ConstantRegExp*>(value))
    {
        // TODO emitInstr(Opcode::RLOAD, re->get());
        emitInstr(Opcode::ILOAD, _cp.makeRegExp(re->get()));
        changeStack(0, value);
        return;
    }

    // if value is already on stack, dup to top
    StackPointer si = getStackPointer(value);
    COREVM_ASSERT(si != static_cast<size_t>(-1),
                  "BUG: emitLoad: value not yet on the stack but referenced as operand.");

    // If value is at the top of stack AND only used once, we can use it directly
    if (si == getStackPointer() - 1 && value->useCount() == 1)
        return;

    if (value->useCount() == 1)
    {
        // Only used once, so move value to stack top using STACKROT.
        // STACKROT moves stack[si] to stack[top] and shifts the rest down.
        // We must update _stack tracking to match the physical stack order.
        emitInstr(Opcode::STACKROT, si);
        const Value* v = _stack[si];
        _stack.erase(_stack.begin() + si);
        _stack.push_back(v);
        return;
    }

    // Value is used multiple times, duplicate it onto stack top
    emitInstr(Opcode::LOAD, si);
    push(value);
}

void TargetCodeGenerator::dumpCurrentStack()
{
    std::print("Dump stack state ({} elements):\n", _stack.size());

    for (size_t i = 0, e = _stack.size(); i != e; ++i)
    {
        std::print("stack[{}]: {}\n", i, _stack[i]->to_string());
    }
}

void TargetCodeGenerator::visit(PhiNode& /*phiInstr*/)
{
    fprintf(
        stderr,
        "Should never reach here, as PHI instruction nodes should have been replaced by target registers.");
    abort();
}

void TargetCodeGenerator::visit(CondBrInstr& condBrInstr)
{
    // Load condition to top of stack
    emitLoad(condBrInstr.condition());

    // Now stack is: [allocas...][extras...][condition]
    // We need to remove extras so successor blocks get: [allocas...]
    //
    // Strategy: Move condition down to position _allocaCount, then DISCARD the extras.
    // STACKROT moves element at given index to top. We need the reverse.
    // We'll use repeated STACKROTs:
    // - STACKROT (size-2) moves condition one position down
    // - Repeat until condition is at _allocaCount
    // Then DISCARD the extras that are now on top.

    size_t extrasCount = _stack.size() - _allocaCount - 1; // -1 for condition at top
    if (extrasCount > 0)
    {
        // Current: [allocas...][extras...][condition]  (condition at top = index size-1)
        // Goal:    [allocas...][condition][extras...]  (condition at _allocaCount)
        //
        // To move condition from top to _allocaCount:
        // Repeatedly rotate the second-from-top element to top, which moves condition down.
        // Each STACKROT (currentCondPos - 1) brings the element below condition to top,
        // effectively moving condition down by one position.

        size_t conditionPos = _stack.size() - 1;
        while (conditionPos > _allocaCount)
        {
            // STACKROT (conditionPos - 1) brings element at conditionPos-1 to top
            // shifting condition down by one
            emitInstr(Opcode::STACKROT, conditionPos - 1);
            // Update tracking: element at conditionPos-1 moves to top
            const Value* below = _stack[conditionPos - 1];
            _stack.erase(_stack.begin() + (conditionPos - 1));
            _stack.push_back(below);
            // Condition is now at conditionPos - 1
            conditionPos--;
        }
        // Now: [allocas...][condition][extras...]
        // Condition is at _allocaCount, extras are at _allocaCount+1 .. size-1

        // Discard extras (they're now on top)
        emitInstr(Opcode::DISCARD, extrasCount);
        while (_stack.size() > _allocaCount + 1)
            _stack.pop_back();
    }

    // Stack is now: [allocas...][condition]
    // After JN/JZ pops condition: [allocas...]

    if (condBrInstr.getBasicBlock()->isAfter(condBrInstr.trueBlock()))
    {
        emitCondJump(Opcode::JZ, condBrInstr.falseBlock());
    }
    else if (condBrInstr.getBasicBlock()->isAfter(condBrInstr.falseBlock()))
    {
        emitCondJump(Opcode::JN, condBrInstr.trueBlock());
    }
    else
    {
        emitCondJump(Opcode::JN, condBrInstr.trueBlock());
        emitJump(condBrInstr.falseBlock());
    }
}

void TargetCodeGenerator::visit(BrInstr& brInstr)
{
    // Do not emit the JMP if the target block is emitted right after this block
    // (and thus, right after this instruction).
    if (brInstr.getBasicBlock()->isAfter(brInstr.targetBlock()))
        return;

    emitJump(brInstr.targetBlock());
}

void TargetCodeGenerator::visit(RetInstr& retInstr)
{
    Value* operand = retInstr.operands()[0];
    if (auto* constInt = dynamic_cast<ConstantInt*>(operand))
    {
        // Constant exit code - use EXIT with immediate value
        emitInstr(Opcode::EXIT, constInt->get());
    }
    else
    {
        // Dynamic exit code - load value and use EXITPOP
        emitLoad(operand);
        emitInstr(Opcode::EXITPOP);
    }
}

void TargetCodeGenerator::visit(MatchInstr& matchInstr)
{
    const size_t matchId = _cp.makeMatchDef();
    MatchDef& matchDef = _cp.getMatchDef(matchId);

    matchDef.handlerId = _cp.makeHandler(matchInstr.getBasicBlock()->getHandler());
    matchDef.op = matchInstr.op();
    matchDef.elsePC = 0; // XXX to be filled in post-processing the handler

    _matchHints.emplace_back(&matchInstr, matchId);

    for (const auto& one: matchInstr.cases())
    {
        if (auto* str = dynamic_cast<ConstantString*>(one.first))
        {
            matchDef.cases.emplace_back(_cp.makeString(str->get()));
        }
        else if (auto* regex = dynamic_cast<ConstantRegExp*>(one.first))
        {
            matchDef.cases.emplace_back(_cp.makeRegExp(regex->get()));
        }
        else
        {
            COREVM_ASSERT(false, "BUG: unsupported label type");
        }
    }

    emitLoad(matchInstr.condition());
    switch (matchDef.op)
    {
        case MatchClass::Same:
            emitInstr(Opcode::SMATCHEQ, matchId);
            pop(1);
            break;
        case MatchClass::Head:
            emitInstr(Opcode::SMATCHBEG, matchId);
            pop(1);
            break;
        case MatchClass::Tail:
            emitInstr(Opcode::SMATCHEND, matchId);
            pop(1);
            break;
        case MatchClass::RegExp:
            emitInstr(Opcode::SMATCHR, matchId);
            pop(1);
            break;
    }
}

void TargetCodeGenerator::visit(RegExpGroupInstr& regexGroupInstr)
{
    CoreNumber groupId = regexGroupInstr.groupId()->get();
    emitInstr(Opcode::SREGGROUP, groupId);
    push(&regexGroupInstr);
}

void TargetCodeGenerator::visit(CastInstr& castInstr)
{
    // map of (target, source, opcode)
    static const std::unordered_map<LiteralType, std::unordered_map<LiteralType, Opcode>> map = {
        { LiteralType::String,
          {
              { LiteralType::Number, Opcode::N2S },
              { LiteralType::IPAddress, Opcode::P2S },
              { LiteralType::Cidr, Opcode::C2S },
              { LiteralType::RegExp, Opcode::R2S },
              // Dynamic types (Void/Object) are treated as numbers at runtime
              { LiteralType::Void, Opcode::N2S },
              { LiteralType::Object, Opcode::N2S },
          } },
        { LiteralType::Number,
          {
              { LiteralType::String, Opcode::S2N },
          } },
    };

    // just alias same-type casts
    if (castInstr.type() == castInstr.source()->type())
    {
        emitLoad(castInstr.source());
        return;
    }

    // lookup target type
    const auto i = map.find(castInstr.type());
    assert(i != map.end() && "Cast target type not found.");

    // lookup source type
    const auto& sub = i->second;
    auto k = sub.find(castInstr.source()->type());
    assert(k != sub.end() && "Cast source type not found.");
    Opcode op = k->second;

    // emit instruction
    emitLoad(castInstr.source());
    emitInstr(op);
    changeStack(1, &castInstr);
}

void TargetCodeGenerator::visit(INegInstr& instr)
{
    emitUnary(instr, Opcode::NNEG);
}

void TargetCodeGenerator::visit(INotInstr& instr)
{
    emitUnary(instr, Opcode::NNOT);
}

void TargetCodeGenerator::visit(IAddInstr& instr)
{
    emitBinaryAssoc(instr, Opcode::NADD);
}

void TargetCodeGenerator::visit(ISubInstr& instr)
{
    emitBinaryAssoc(instr, Opcode::NSUB);
}

void TargetCodeGenerator::visit(IMulInstr& instr)
{
    emitBinaryAssoc(instr, Opcode::NMUL);
}

void TargetCodeGenerator::visit(IDivInstr& instr)
{
    emitBinaryAssoc(instr, Opcode::NDIV);
}

void TargetCodeGenerator::visit(IRemInstr& instr)
{
    emitBinaryAssoc(instr, Opcode::NREM);
}

void TargetCodeGenerator::visit(IPowInstr& instr)
{
    emitBinary(instr, Opcode::NPOW);
}

void TargetCodeGenerator::visit(IAndInstr& instr)
{
    emitBinaryAssoc(instr, Opcode::NAND);
}

void TargetCodeGenerator::visit(IOrInstr& instr)
{
    emitBinaryAssoc(instr, Opcode::NOR);
}

void TargetCodeGenerator::visit(IXorInstr& instr)
{
    emitBinaryAssoc(instr, Opcode::NXOR);
}

void TargetCodeGenerator::visit(IShlInstr& instr)
{
    emitBinaryAssoc(instr, Opcode::NSHL);
}

void TargetCodeGenerator::visit(IShrInstr& instr)
{
    emitBinaryAssoc(instr, Opcode::NSHR);
}

void TargetCodeGenerator::visit(ICmpEQInstr& instr)
{
    emitBinaryAssoc(instr, Opcode::NCMPEQ);
}

void TargetCodeGenerator::visit(ICmpNEInstr& instr)
{
    emitBinaryAssoc(instr, Opcode::NCMPNE);
}

void TargetCodeGenerator::visit(ICmpLEInstr& instr)
{
    emitBinaryAssoc(instr, Opcode::NCMPLE);
}

void TargetCodeGenerator::visit(ICmpGEInstr& instr)
{
    emitBinaryAssoc(instr, Opcode::NCMPGE);
}

void TargetCodeGenerator::visit(ICmpLTInstr& instr)
{
    emitBinaryAssoc(instr, Opcode::NCMPLT);
}

void TargetCodeGenerator::visit(ICmpGTInstr& instr)
{
    emitBinaryAssoc(instr, Opcode::NCMPGT);
}

void TargetCodeGenerator::visit(BNotInstr& instr)
{
    emitUnary(instr, Opcode::BNOT);
}

void TargetCodeGenerator::visit(BAndInstr& instr)
{
    emitBinary(instr, Opcode::BAND);
}

void TargetCodeGenerator::visit(BOrInstr& instr)
{
    emitBinary(instr, Opcode::BOR);
}

void TargetCodeGenerator::visit(BXorInstr& instr)
{
    emitBinary(instr, Opcode::BXOR);
}

void TargetCodeGenerator::visit(SLenInstr& instr)
{
    emitUnary(instr, Opcode::SLEN);
}

void TargetCodeGenerator::visit(SIsEmptyInstr& instr)
{
    emitUnary(instr, Opcode::SISEMPTY);
}

void TargetCodeGenerator::visit(SAddInstr& instr)
{
    emitBinary(instr, Opcode::SADD);
}

void TargetCodeGenerator::visit(SSubStrInstr& instr)
{
    emitBinary(instr, Opcode::SSUBSTR);
}

void TargetCodeGenerator::visit(SCmpEQInstr& instr)
{
    emitBinary(instr, Opcode::SCMPEQ);
}

void TargetCodeGenerator::visit(SCmpNEInstr& instr)
{
    emitBinary(instr, Opcode::SCMPNE);
}

void TargetCodeGenerator::visit(SCmpLEInstr& instr)
{
    emitBinary(instr, Opcode::SCMPLE);
}

void TargetCodeGenerator::visit(SCmpGEInstr& instr)
{
    emitBinary(instr, Opcode::SCMPGE);
}

void TargetCodeGenerator::visit(SCmpLTInstr& instr)
{
    emitBinary(instr, Opcode::SCMPLT);
}

void TargetCodeGenerator::visit(SCmpGTInstr& instr)
{
    emitBinary(instr, Opcode::SCMPGT);
}

void TargetCodeGenerator::visit(SCmpREInstr& instr)
{
    auto* re = dynamic_cast<ConstantRegExp*>(instr.operand(1));
    COREVM_ASSERT(re != nullptr, "CoreVM: RHS must be a ConstantRegExp");

    emitLoad(instr.operand(0));
    emitInstr(Opcode::SREGMATCH, _cp.makeRegExp(re->get()));
    changeStack(1, &instr);
}

void TargetCodeGenerator::visit(SCmpBegInstr& instr)
{
    emitBinary(instr, Opcode::SCMPBEG);
}

void TargetCodeGenerator::visit(SCmpEndInstr& instr)
{
    emitBinary(instr, Opcode::SCMPEND);
}

void TargetCodeGenerator::visit(SInInstr& instr)
{
    emitBinary(instr, Opcode::SCONTAINS);
}

void TargetCodeGenerator::visit(PCmpEQInstr& instr)
{
    emitBinary(instr, Opcode::PCMPEQ);
}

void TargetCodeGenerator::visit(PCmpNEInstr& instr)
{
    emitBinary(instr, Opcode::PCMPNE);
}

void TargetCodeGenerator::visit(PInCidrInstr& instr)
{
    emitBinary(instr, Opcode::PINCIDR);
}

// {{{ Object instructions
void TargetCodeGenerator::visit(ObjAllocInstr& instr)
{
    emitInstr(Opcode::OALLOC, static_cast<Operand>(instr.typeId()->get()));
    changeStack(0, &instr);
}

void TargetCodeGenerator::visit(ObjRetainInstr& instr)
{
    emitLoad(instr.object());
    emitInstr(Opcode::ORETAIN);
    // ORETAIN doesn't change stack - object remains on top
}

void TargetCodeGenerator::visit(ObjReleaseInstr& instr)
{
    // Load the object from its storage alloca using the fixed alloca index.
    // This avoids the cross-block value tracking issue where emitLoad() would fail
    // if the loaded value wasn't in the current block's stack tracking.
    auto it = _allocaIndices.find(instr.storage());
    COREVM_ASSERT(it != _allocaIndices.end(), "BUG: ObjReleaseInstr storage not found in alloca indices");
    emitInstr(Opcode::LOAD, it->second);
    push(instr.storage()); // Track that we pushed something
    emitInstr(Opcode::ORELEASE);
    changeStack(1, nullptr); // pops the object
}

void TargetCodeGenerator::visit(ObjGetTagInstr& instr)
{
    emitLoad(instr.object());
    emitInstr(Opcode::OGETTAG);
    changeStack(1, &instr); // replaces object with tag
}

void TargetCodeGenerator::visit(ObjSetTagInstr& instr)
{
    emitLoad(instr.object());
    emitLoad(instr.tag());
    emitInstr(Opcode::OSETTAG);
    changeStack(2, &instr); // pops tag and object, pushes object back
}

void TargetCodeGenerator::visit(ObjGetSlotInstr& instr)
{
    emitLoad(instr.object());
    emitInstr(Opcode::OGETSLOT, static_cast<Operand>(instr.slotIndex()->get()));
    changeStack(1, &instr); // replaces object with slot value
}

void TargetCodeGenerator::visit(ObjSetSlotInstr& instr)
{
    emitLoad(instr.object());
    emitLoad(instr.value());
    emitInstr(Opcode::OSETSLOT, static_cast<Operand>(instr.slotIndex()->get()));
    changeStack(2, &instr); // pops value and object, pushes object back
}

void TargetCodeGenerator::visit(ObjTypeIdInstr& instr)
{
    emitLoad(instr.object());
    emitInstr(Opcode::OTYPEID);
    changeStack(1, &instr); // replaces object with type ID
}

void TargetCodeGenerator::visit(ObjIsTypeInstr& instr)
{
    emitLoad(instr.object());
    emitInstr(Opcode::OISTYPE, static_cast<Operand>(instr.typeId()->get()));
    changeStack(1, &instr); // replaces object with boolean
}

void TargetCodeGenerator::visit(VCmpEQInstr& instr)
{
    emitLoad(instr.lhs());
    emitLoad(instr.rhs());
    emitInstr(Opcode::VCMPEQ);
    changeStack(2, &instr); // pops two values, pushes boolean result
}

void TargetCodeGenerator::visit(VCmpNEInstr& instr)
{
    emitLoad(instr.lhs());
    emitLoad(instr.rhs());
    emitInstr(Opcode::VCMPNE);
    changeStack(2, &instr);
}

void TargetCodeGenerator::visit(VCmpLTInstr& instr)
{
    emitLoad(instr.lhs());
    emitLoad(instr.rhs());
    emitInstr(Opcode::VCMPLT);
    changeStack(2, &instr);
}

void TargetCodeGenerator::visit(VCmpLEInstr& instr)
{
    emitLoad(instr.lhs());
    emitLoad(instr.rhs());
    emitInstr(Opcode::VCMPLE);
    changeStack(2, &instr);
}

void TargetCodeGenerator::visit(VCmpGTInstr& instr)
{
    emitLoad(instr.lhs());
    emitLoad(instr.rhs());
    emitInstr(Opcode::VCMPGT);
    changeStack(2, &instr);
}

void TargetCodeGenerator::visit(VCmpGEInstr& instr)
{
    emitLoad(instr.lhs());
    emitLoad(instr.rhs());
    emitInstr(Opcode::VCMPGE);
    changeStack(2, &instr);
}

// }}}

// }}}

} // namespace CoreVM
