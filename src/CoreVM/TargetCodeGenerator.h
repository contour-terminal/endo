// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <CoreVM/LiteralType.h>
#include <CoreVM/ir/InstructionVisitor.h>
#include <CoreVM/util/Cidr.h>
#include <CoreVM/util/IPAddress.h>
#include <CoreVM/vm/ConstantPool.h>

#include <deque>
#include <list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace CoreVM
{
class Value;
class Instr;
class IRProgram;
class IRHandler;
class BasicBlock;
class IRBuiltinHandler;
class IRBuiltinFunction;
} // namespace CoreVM

namespace CoreVM::vm
{
class Program;
}

namespace CoreVM
{

//! \addtogroup CoreVM
//@{

using StackPointer = size_t;

class TargetCodeGenerator: public InstructionVisitor
{
  public:
    TargetCodeGenerator();

    std::unique_ptr<Program> generate(IRProgram* program);

  protected:
    void generate(IRHandler* handler);

    void dumpCurrentStack();

    /**
     * Ensures @p value is available on top of the stack.
     *
     * May emit a LOAD instruction if stack[sp] is not on top of the stack.
     */
    void emitLoad(Value* value);

    void emitInstr(Opcode opc) { emitInstr(makeInstruction(opc)); }
    void emitInstr(Opcode opc, Operand op1) { emitInstr(makeInstruction(opc, op1)); }
    void emitInstr(Opcode opc, Operand op1, Operand op2) { emitInstr(makeInstruction(opc, op1, op2)); }
    void emitInstr(Opcode opc, Operand op1, Operand op2, Operand op3)
    {
        emitInstr(makeInstruction(opc, op1, op2, op3));
    }
    void emitInstr(Instruction instr);

    /**
     * Emits conditional jump instruction.
     *
     * @param opcode Opcode for the conditional jump.
     * @param bb Target basic block to jump to by \p opcode.
     *
     * This function will just emit a placeholder and will remember the
     * instruction pointer and passed operands for later back-patching once all
     * basic block addresses have been computed.
     */
    void emitCondJump(Opcode opcode, BasicBlock* bb);

    /**
     * Emits unconditional jump instruction.
     *
     * @param bb Target basic block to jump to by \p opcode.
     *
     * This function will just emit a placeholder and will remember the
     * instruction pointer and passed operands for later back-patching once all
     * basic block addresses have been computed.
     */
    void emitJump(BasicBlock* bb);

    void emitBinaryAssoc(Instr& instr, Opcode opcode);
    void emitBinary(Instr& instr, Opcode opcode);
    void emitUnary(Instr& instr, Opcode opcode);

    Operand getConstantInt(Value* value);

    /**
     * Retrieves the instruction pointer of the next instruction to be emitted.
     */
    size_t getInstructionPointer() const { return _code.size(); }

    /**
     * Finds given variable on global storage and returns its absolute offset if found or -1 if not.
     */
    std::optional<size_t> findGlobal(const Value* variable) const;

    /**
     * Retrieves the current number of elements on the stack.
     *
     * This also means, this value will be the absolute index for the next value
     * to be placed on top of the stack.
     */
    StackPointer getStackPointer() const { return _stack.size(); }

    /** Locates given @p value on the stack.
     */
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
    void visit(HandlerCallInstr& instr) override;

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

    //! list of raised errors during code generation.
    std::vector<std::string> _errors;

    std::unordered_map<BasicBlock*, std::list<ConditionalJump>> _conditionalJumps;
    std::unordered_map<BasicBlock*, std::list<UnconditionalJump>> _unconditionalJumps;
    std::list<std::pair<MatchInstr*, size_t /*matchId*/>> _matchHints;

    size_t _handlerId;              //!< current handler's ID
    std::vector<Instruction> _code; //!< current handler's code

    /** target stack during target code generation */
    std::deque<const Value*> _stack;

    /** global scope mapping */
    std::deque<const Value*> _globals;

    // target program output
    ConstantPool _cp;
};

//!@}

} // namespace CoreVM
