// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <CoreVM/MatchClass.h>
#include <CoreVM/ir/ConstantValue.h>
#include <CoreVM/ir/Instr.h>

#include <list>
#include <string>
#include <vector>

namespace CoreVM
{

class Instr;
class BasicBlock;
class IRProgram;
class IRBuilder;
class IRBuiltinHandler;
class IRBuiltinFunction;

class NopInstr: public Instr
{
  public:
    NopInstr(): Instr(LiteralType::Void, {}, "nop") {}

    [[nodiscard]] std::string to_string() const override;
    [[nodiscard]] std::unique_ptr<Instr> clone() override;
    void accept(InstructionVisitor& v) override;
};

/**
 * Allocates an array of given type and elements.
 */
class AllocaInstr: public Instr
{
  private:
    static LiteralType computeType(LiteralType elementType, Value* size)
    { // {{{
        if (auto* n = dynamic_cast<ConstantInt*>(size))
        {
            if (n->get() == 1)
                return elementType;
        }

        switch (elementType)
        {
            case LiteralType::Number: return LiteralType::IntArray;
            case LiteralType::String: return LiteralType::StringArray;
            default: return LiteralType::Void;
        }
    } // }}}

  public:
    AllocaInstr(LiteralType ty, Value* n, const std::string& name): Instr(ty, { n }, name) {}

    [[nodiscard]] LiteralType elementType() const
    {
        switch (type())
        {
            case LiteralType::StringArray: return LiteralType::String;
            case LiteralType::IntArray: return LiteralType::Number;
            default: return LiteralType::Void;
        }
    }

    [[nodiscard]] Value* arraySize() const { return operands()[0]; }

    [[nodiscard]] std::string to_string() const override;
    [[nodiscard]] std::unique_ptr<Instr> clone() override;
    void accept(InstructionVisitor& v) override;
};

class StoreInstr: public Instr
{
  public:
    StoreInstr(Value* variable, ConstantInt* index, Value* source, const std::string& name):
        Instr(LiteralType::Void, { variable, index, source }, name)
    {
    }

    [[nodiscard]] Value* variable() const { return operand(0); }
    [[nodiscard]] ConstantInt* index() const { return static_cast<ConstantInt*>(operand(1)); }
    [[nodiscard]] Value* source() const { return operand(2); }

    [[nodiscard]] std::string to_string() const override;
    [[nodiscard]] std::unique_ptr<Instr> clone() override;
    void accept(InstructionVisitor& v) override;
};

class RegExpGroupInstr: public Instr
{
  public:
    RegExpGroupInstr(ConstantInt* groupId, const std::string& name):
        Instr { LiteralType::String, { groupId }, name }
    {
    }

    [[nodiscard]] ConstantInt* groupId() const { return static_cast<ConstantInt*>(operand(0)); }

    [[nodiscard]] std::string to_string() const override;
    [[nodiscard]] std::unique_ptr<Instr> clone() override;
    void accept(InstructionVisitor& v) override;
};

class LoadInstr: public Instr
{
  public:
    LoadInstr(Value* variable, const std::string& name): Instr(variable->type(), { variable }, name) {}

    [[nodiscard]] Value* variable() const { return operand(0); }

    [[nodiscard]] std::string to_string() const override;
    [[nodiscard]] std::unique_ptr<Instr> clone() override;
    void accept(InstructionVisitor& v) override;
};

class CallInstr: public Instr
{
  public:
    CallInstr(const std::vector<Value*>& args, const std::string& name);
    CallInstr(IRBuiltinFunction* callee, const std::vector<Value*>& args, const std::string& name);

    [[nodiscard]] IRBuiltinFunction* callee() const { return (IRBuiltinFunction*) operand(0); }

    [[nodiscard]] std::string to_string() const override;
    [[nodiscard]] std::unique_ptr<Instr> clone() override;
    void accept(InstructionVisitor& v) override;
};

class HandlerCallInstr: public Instr
{
  public:
    explicit HandlerCallInstr(const std::vector<Value*>& args);
    HandlerCallInstr(IRBuiltinHandler* callee, const std::vector<Value*>& args);

    [[nodiscard]] IRBuiltinHandler* callee() const { return (IRBuiltinHandler*) operand(0); }

    [[nodiscard]] std::string to_string() const override;
    [[nodiscard]] std::unique_ptr<Instr> clone() override;
    void accept(InstructionVisitor& v) override;
};

class CastInstr: public Instr
{
  public:
    CastInstr(LiteralType resultType, Value* op, const std::string& name): Instr(resultType, { op }, name) {}

    [[nodiscard]] Value* source() const { return operand(0); }

    [[nodiscard]] std::string to_string() const override;
    [[nodiscard]] std::unique_ptr<Instr> clone() override;
    void accept(InstructionVisitor& v) override;
};

template <const UnaryOperator Operator, const LiteralType ResultType>
class UnaryInstr: public Instr
{
  public:
    UnaryInstr(Value* op, const std::string& name): Instr(ResultType, { op }, name), _operator(Operator) {}

    [[nodiscard]] UnaryOperator op() const { return _operator; }

    [[nodiscard]] std::string to_string() const override { return formatOne(cstr(_operator)); }

    [[nodiscard]] std::unique_ptr<Instr> clone() override
    {
        return std::make_unique<UnaryInstr<Operator, ResultType>>(operand(0), name());
    }

    void accept(InstructionVisitor& v) override { v.visit(*this); }

  private:
    UnaryOperator _operator;
};

template <const BinaryOperator Operator, const LiteralType ResultType>
class BinaryInstr: public Instr
{
  public:
    BinaryInstr(Value* lhs, Value* rhs, const std::string& name):
        Instr(ResultType, { lhs, rhs }, name), _operator(Operator)
    {
    }

    [[nodiscard]] BinaryOperator op() const { return _operator; }

    [[nodiscard]] std::string to_string() const override { return formatOne(cstr(_operator)); }

    [[nodiscard]] std::unique_ptr<Instr> clone() override
    {
        return std::make_unique<BinaryInstr<Operator, ResultType>>(operand(0), operand(1), name());
    }

    void accept(InstructionVisitor& v) override { v.visit(*this); }

  private:
    BinaryOperator _operator;
};

/**
 * Creates a PHI (phoney) instruction.
 *
 * Creates a synthetic instruction that purely informs the target register
 *allocator
 * to allocate the very same register for all given operands,
 * which is then used across all their basic blocks.
 */
class PhiNode: public Instr
{
  public:
    PhiNode(const std::vector<Value*>& ops, const std::string& name);

    [[nodiscard]] std::string to_string() const override;
    [[nodiscard]] std::unique_ptr<Instr> clone() override;
    void accept(InstructionVisitor& v) override;
};

class TerminateInstr: public Instr
{
  protected:
    TerminateInstr(const TerminateInstr& v) = default;

  public:
    TerminateInstr(const std::vector<Value*>& ops): Instr(LiteralType::Void, ops, "") {}
};

/**
 * Conditional branch instruction.
 *
 * Creates a terminate instruction that transfers control to one of the two
 * given alternate basic blocks, depending on the given input condition.
 */
class CondBrInstr: public TerminateInstr
{
  public:
    /**
     * Initializes the object.
     *
     * @param cond input condition that (if true) causes \p trueBlock to be jumped
     *to, \p falseBlock otherwise.
     * @param trueBlock basic block to run if input condition evaluated to true.
     * @param falseBlock basic block to run if input condition evaluated to false.
     */
    CondBrInstr(Value* cond, BasicBlock* trueBlock, BasicBlock* falseBlock);

    [[nodiscard]] Value* condition() const { return operands()[0]; }
    [[nodiscard]] BasicBlock* trueBlock() const { return (BasicBlock*) operands()[1]; }
    [[nodiscard]] BasicBlock* falseBlock() const { return (BasicBlock*) operands()[2]; }

    [[nodiscard]] std::string to_string() const override;
    [[nodiscard]] std::unique_ptr<Instr> clone() override;
    void accept(InstructionVisitor& v) override;
};

/**
 * Unconditional jump instruction.
 */
class BrInstr: public TerminateInstr
{
  public:
    explicit BrInstr(BasicBlock* targetBlock);

    [[nodiscard]] BasicBlock* targetBlock() const { return (BasicBlock*) operands()[0]; }

    [[nodiscard]] std::string to_string() const override;
    [[nodiscard]] std::unique_ptr<Instr> clone() override;
    void accept(InstructionVisitor& v) override;
};

/**
 * handler-return instruction.
 */
class RetInstr: public TerminateInstr
{
  public:
    RetInstr(Value* result);

    [[nodiscard]] std::string to_string() const override;
    [[nodiscard]] std::unique_ptr<Instr> clone() override;
    void accept(InstructionVisitor& v) override;
};

/**
 * Match instruction, implementing the CoreVM match-keyword.
 *
 * <li>operand[0] - condition</li>
 * <li>operand[1] - default block</li>
 * <li>operand[2n+2] - case label</li>
 * <li>operand[2n+3] - case block</li>
 */
class MatchInstr: public TerminateInstr
{
  public:
    MatchInstr(const MatchInstr&);
    MatchInstr(MatchClass op, Value* cond);

    [[nodiscard]] MatchClass op() const { return _op; }

    [[nodiscard]] Value* condition() const { return operand(0); }

    void addCase(Constant* label, BasicBlock* code);
    [[nodiscard]] std::vector<std::pair<Constant*, BasicBlock*>> cases() const;

    [[nodiscard]] BasicBlock* elseBlock() const;
    void setElseBlock(BasicBlock* code);

    [[nodiscard]] std::string to_string() const override;
    [[nodiscard]] std::unique_ptr<Instr> clone() override;
    void accept(InstructionVisitor& v) override;

  private:
    MatchClass _op;
    std::vector<std::pair<Constant*, BasicBlock*>> _cases;
};

} // namespace CoreVM
