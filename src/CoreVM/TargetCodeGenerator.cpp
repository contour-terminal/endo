// SPDX-License-Identifier: Apache-2.0

#include <CoreVM/LiteralType.h>
#include <CoreVM/TargetCodeGenerator.h>
#include <CoreVM/ir/BasicBlock.h>
#include <CoreVM/ir/ConstantArray.h>
#include <CoreVM/ir/ConstantValue.h>
#include <CoreVM/ir/IRBuiltinFunction.h>
#include <CoreVM/ir/IRBuiltinHandler.h>
#include <CoreVM/ir/IRHandler.h>
#include <CoreVM/ir/IRProgram.h>
#include <CoreVM/ir/Instructions.h>
#include <CoreVM/util/assert.h>
#include <CoreVM/vm/ConstantPool.h>
#include <CoreVM/vm/Match.h>
#include <CoreVM/vm/Program.h>

#include <array>
#include <cstdarg>
#include <limits>
#include <unordered_map>

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

    // generate code for all basic blocks, sequentially
    for (BasicBlock* bb: handler->basicBlocks())
    {
        basicBlockEntryPoints[bb] = getInstructionPointer();
        for (Instr* instr: bb->instructions())
        {
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
        _stack.pop_back();
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
        if (number <= std::numeric_limits<Operand>::max())
        {
            emitInstr(Opcode::ILOAD, number);
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

    if (si == getStackPointer() - 1)
        return;

    if (value->useCount() == 1)
    {
        // XXX only used once, so move value to stack top
        emitInstr(Opcode::STACKROT, si);
        return;
    }

    // XXX duplicate value onto stack top
    emitInstr(Opcode::LOAD, si);
    push(value);
}

void TargetCodeGenerator::dumpCurrentStack()
{
    fmt::print("Dump stack state ({} elements):\n", _stack.size());

    for (size_t i = 0, e = _stack.size(); i != e; ++i)
    {
        fmt::print("stack[{}]: {}\n", i, _stack[i]->to_string());
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
    if (condBrInstr.getBasicBlock()->isAfter(condBrInstr.trueBlock()))
    {
        emitLoad(condBrInstr.condition());
        emitCondJump(Opcode::JZ, condBrInstr.falseBlock());
    }
    else if (condBrInstr.getBasicBlock()->isAfter(condBrInstr.falseBlock()))
    {
        emitLoad(condBrInstr.condition());
        emitCondJump(Opcode::JN, condBrInstr.trueBlock());
    }
    else
    {
        emitLoad(condBrInstr.condition());
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
    emitInstr(Opcode::EXIT, getConstantInt(retInstr.operands()[0]));
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
    emitBinary(instr, Opcode::BAND);
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
// }}}

} // namespace CoreVM
