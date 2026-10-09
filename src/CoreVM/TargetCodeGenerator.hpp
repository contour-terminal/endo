// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <CoreVM/SourceLocation.hpp>
#include <CoreVM/enums.hpp>
#include <CoreVM/ir/Instructions.hpp>
#include <CoreVM/util/assert.hpp>
#include <CoreVM/vm/Program.hpp>

#include <concepts>
#include <deque>
#include <list>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace CoreVM
{

class IRProgram;
class IRFunction;
class BasicBlock;

using StackPointer = size_t;

class TargetCodeGenerator: public InstructionVisitor
{
  public:
    TargetCodeGenerator();

    std::unique_ptr<Program> generate(IRProgram* program);

  protected:
    void generate(IRFunction* function);

    void dumpCurrentStack() const;

    void emitLoad(Value* value);

    /// Narrows @p value to an instruction operand.
    /// @param value A stack slot, constant-pool index, jump target or count.
    /// @return @p value as an Operand; a value the encoding cannot hold is a code generator bug.
    template <std::integral T>
    [[nodiscard]] static Operand toOperand(T value)
    {
        COREVM_ASSERT(std::in_range<Operand>(value), "BUG: instruction operand out of range");
        return static_cast<Operand>(value);
    }

    /// Emits @p opc with up to three operands, each narrowed by toOperand().
    template <std::integral... Operands>
        requires(sizeof...(Operands) <= 3)
    void emitInstr(Opcode opc, Operands... operands)
    {
        emitInstr(makeInstruction(opc, toOperand(operands)...));
    }

    void emitInstr(Instruction instr);

    void emitCondJump(Opcode opcode, BasicBlock* bb);
    void emitJump(BasicBlock* bb);

    void emitBinaryAssoc(Instr& instr, Opcode opcode);
    void emitBinary(Instr& instr, Opcode opcode);
    void emitUnary(Instr& instr, Opcode opcode);

    [[nodiscard]] size_t getInstructionPointer() const { return _code.size(); }

    std::optional<size_t> findGlobal(const Value* variable) const;

    [[nodiscard]] StackPointer getStackPointer() const { return _stack.size(); }

    StackPointer getStackPointer(const Value* value);

    void changeStack(size_t pops, const Value* pushValue);
    void pop(size_t count);
    void push(const Value* alias);

    void visit(NopInstr& instr) override;

    // storage
    void visit(AllocaInstr& instr) override;
    void visit(StoreInstr& instr) override;
    void visit(LoadInstr& instr) override;
    void visit(PhiNode& instr) override;

    // calls
    void visit(CallInstr& instr) override;
    void visit(FunctionCallInstr& instr) override;
    void visit(FunctionRetInstr& instr) override;
    void visit(TailCallInstr& instr) override;
    void visit(IndirectCallInstr& instr) override;
    void visit(IndirectTailCallInstr& instr) override;

    // terminator
    void visit(CondBrInstr& instr) override;
    void visit(BrInstr& instr) override;
    void visit(RetInstr& instr) override;
    void visit(MatchInstr& instr) override;

    // regexp
    void visit(RegExpGroupInstr& instr) override;

    // type cast
    void visit(CastInstr& instr) override;

    // numeric
    void visit(INegInstr& instr) override;
    void visit(INotInstr& instr) override;
    void visit(IAddInstr& instr) override;
    void visit(ISubInstr& instr) override;
    void visit(IMulInstr& instr) override;
    void visit(IDivInstr& instr) override;
    void visit(IRemInstr& instr) override;
    void visit(IPowInstr& instr) override;
    void visit(IAndInstr& instr) override;
    void visit(IOrInstr& instr) override;
    void visit(IXorInstr& instr) override;
    void visit(IShlInstr& instr) override;
    void visit(IShrInstr& instr) override;
    void visit(ICmpEQInstr& instr) override;
    void visit(ICmpNEInstr& instr) override;
    void visit(ICmpLEInstr& instr) override;
    void visit(ICmpGEInstr& instr) override;
    void visit(ICmpLTInstr& instr) override;
    void visit(ICmpGTInstr& instr) override;

    // boolean
    void visit(BNotInstr& instr) override;
    void visit(BAndInstr& instr) override;
    void visit(BOrInstr& instr) override;
    void visit(BXorInstr& instr) override;

    // string
    void visit(SLenInstr& instr) override;
    void visit(SIsEmptyInstr& instr) override;
    void visit(SAddInstr& instr) override;
    void visit(SSubStrInstr& instr) override;
    void visit(SCmpEQInstr& instr) override;
    void visit(SCmpNEInstr& instr) override;
    void visit(SCmpLEInstr& instr) override;
    void visit(SCmpGEInstr& instr) override;
    void visit(SCmpLTInstr& instr) override;
    void visit(SCmpGTInstr& instr) override;
    void visit(SCmpREInstr& instr) override;
    void visit(SCmpBegInstr& instr) override;
    void visit(SCmpEndInstr& instr) override;
    void visit(SInInstr& instr) override;

    // ip
    void visit(PCmpEQInstr& instr) override;
    void visit(PCmpNEInstr& instr) override;
    void visit(PInCidrInstr& instr) override;

    // object operations
    void visit(ObjAllocInstr& instr) override;
    void visit(ObjRetainInstr& instr) override;
    void visit(ObjReleaseInstr& instr) override;
    void visit(ObjGetTagInstr& instr) override;
    void visit(ObjSetTagInstr& instr) override;
    void visit(ObjGetSlotInstr& instr) override;
    void visit(ObjSetSlotInstr& instr) override;
    void visit(ObjTypeIdInstr& instr) override;
    void visit(ObjIsTypeInstr& instr) override;
    void visit(VCmpEQInstr& instr) override;
    void visit(VCmpNEInstr& instr) override;
    void visit(VCmpLTInstr& instr) override;
    void visit(VCmpLEInstr& instr) override;
    void visit(VCmpGTInstr& instr) override;
    void visit(VCmpGEInstr& instr) override;

    // float
    void visit(FNegInstr& instr) override;
    void visit(FAddInstr& instr) override;
    void visit(FSubInstr& instr) override;
    void visit(FMulInstr& instr) override;
    void visit(FDivInstr& instr) override;
    void visit(FRemInstr& instr) override;
    void visit(FPowInstr& instr) override;
    void visit(FCmpEQInstr& instr) override;
    void visit(FCmpNEInstr& instr) override;
    void visit(FCmpLEInstr& instr) override;
    void visit(FCmpGEInstr& instr) override;
    void visit(FCmpLTInstr& instr) override;
    void visit(FCmpGTInstr& instr) override;

    // lazy evaluation
    void visit(FunctionRefInstr& instr) override;
    void visit(LazyForceInstr& instr) override;

  private:
    struct ConditionalJump
    {
        size_t pc;
        Opcode opcode;
    };

    struct UnconditionalJump
    {
        size_t pc;
        Opcode opcode;
    };

    std::vector<std::string> _errors;

    std::unordered_map<BasicBlock*, std::list<ConditionalJump>> _conditionalJumps;
    std::unordered_map<BasicBlock*, std::list<UnconditionalJump>> _unconditionalJumps;
    std::list<std::pair<MatchInstr*, size_t>> _matchHints;

    size_t _functionId = 0;
    std::vector<Instruction> _code;

    ConstantPool::LocationTable _locationTable;
    SourceLocation _lastRecordedLocation;

    std::deque<const Value*> _stack;

    std::unordered_map<const Value*, size_t> _allocaIndices;

    size_t _allocaCount = 0;

    std::unordered_set<const AllocaInstr*> _parameterAllocas;

    std::deque<const Value*> _globals;

    ConstantPool _cp;
};

} // namespace CoreVM
