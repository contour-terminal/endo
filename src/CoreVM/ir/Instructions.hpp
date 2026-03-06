// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <CoreVM/SourceLocation.hpp>
#include <CoreVM/enums.hpp>
#include <CoreVM/ir/Value.hpp>

#include <memory>
#include <string>
#include <vector>

namespace CoreVM
{

class BasicBlock;
class IRFunction;
class IRBuilder;

// Forward declarations for all instruction types (needed by InstructionVisitor)
class NopInstr;
class AllocaInstr;
class StoreInstr;
class LoadInstr;
class CallInstr;
class PhiNode;
class CondBrInstr;
class BrInstr;
class RetInstr;
class MatchInstr;
class RegExpGroupInstr;
class CastInstr;
class FunctionCallInstr;
class FunctionRetInstr;
class TailCallInstr;
class IndirectCallInstr;
class IndirectTailCallInstr;
class FunctionRefInstr;
class LazyForceInstr;
class ObjAllocInstr;
class ObjRetainInstr;
class ObjReleaseInstr;
class ObjGetTagInstr;
class ObjSetTagInstr;
class ObjGetSlotInstr;
class ObjSetSlotInstr;
class ObjTypeIdInstr;
class ObjIsTypeInstr;
class VCmpEQInstr;
class VCmpNEInstr;
class VCmpLTInstr;
class VCmpLEInstr;
class VCmpGTInstr;
class VCmpGEInstr;

class InstructionVisitor
{
  public:
    virtual ~InstructionVisitor() = default;

    virtual void visit(NopInstr& instr) = 0;

    // storage
    virtual void visit(AllocaInstr& instr) = 0;
    virtual void visit(StoreInstr& instr) = 0;
    virtual void visit(LoadInstr& instr) = 0;
    virtual void visit(PhiNode& instr) = 0;

    // calls
    virtual void visit(CallInstr& instr) = 0;

    // user-defined function calls
    virtual void visit(FunctionCallInstr& instr) = 0;
    virtual void visit(FunctionRetInstr& instr) = 0;
    virtual void visit(TailCallInstr& instr) = 0;

    // terminator
    virtual void visit(CondBrInstr& instr) = 0;
    virtual void visit(BrInstr& instr) = 0;
    virtual void visit(RetInstr& instr) = 0;
    virtual void visit(MatchInstr& instr) = 0;

    // regexp
    virtual void visit(RegExpGroupInstr& instr) = 0;

    // type cast
    virtual void visit(CastInstr& instr) = 0;

    // numeric
    virtual void visit(INegInstr& instr) = 0;
    virtual void visit(INotInstr& instr) = 0;
    virtual void visit(IAddInstr& instr) = 0;
    virtual void visit(ISubInstr& instr) = 0;
    virtual void visit(IMulInstr& instr) = 0;
    virtual void visit(IDivInstr& instr) = 0;
    virtual void visit(IRemInstr& instr) = 0;
    virtual void visit(IPowInstr& instr) = 0;
    virtual void visit(IAndInstr& instr) = 0;
    virtual void visit(IOrInstr& instr) = 0;
    virtual void visit(IXorInstr& instr) = 0;
    virtual void visit(IShlInstr& instr) = 0;
    virtual void visit(IShrInstr& instr) = 0;
    virtual void visit(ICmpEQInstr& instr) = 0;
    virtual void visit(ICmpNEInstr& instr) = 0;
    virtual void visit(ICmpLEInstr& instr) = 0;
    virtual void visit(ICmpGEInstr& instr) = 0;
    virtual void visit(ICmpLTInstr& instr) = 0;
    virtual void visit(ICmpGTInstr& instr) = 0;

    // boolean
    virtual void visit(BNotInstr& instr) = 0;
    virtual void visit(BAndInstr& instr) = 0;
    virtual void visit(BOrInstr& instr) = 0;
    virtual void visit(BXorInstr& instr) = 0;

    // string
    virtual void visit(SLenInstr& instr) = 0;
    virtual void visit(SIsEmptyInstr& instr) = 0;
    virtual void visit(SAddInstr& instr) = 0;
    virtual void visit(SSubStrInstr& instr) = 0;
    virtual void visit(SCmpEQInstr& instr) = 0;
    virtual void visit(SCmpNEInstr& instr) = 0;
    virtual void visit(SCmpLEInstr& instr) = 0;
    virtual void visit(SCmpGEInstr& instr) = 0;
    virtual void visit(SCmpLTInstr& instr) = 0;
    virtual void visit(SCmpGTInstr& instr) = 0;
    virtual void visit(SCmpREInstr& instr) = 0;
    virtual void visit(SCmpBegInstr& instr) = 0;
    virtual void visit(SCmpEndInstr& instr) = 0;
    virtual void visit(SInInstr& instr) = 0;

    // ip
    virtual void visit(PCmpEQInstr& instr) = 0;
    virtual void visit(PCmpNEInstr& instr) = 0;
    virtual void visit(PInCidrInstr& instr) = 0;

    // float
    virtual void visit(FNegInstr& instr) = 0;
    virtual void visit(FAddInstr& instr) = 0;
    virtual void visit(FSubInstr& instr) = 0;
    virtual void visit(FMulInstr& instr) = 0;
    virtual void visit(FDivInstr& instr) = 0;
    virtual void visit(FRemInstr& instr) = 0;
    virtual void visit(FPowInstr& instr) = 0;
    virtual void visit(FCmpEQInstr& instr) = 0;
    virtual void visit(FCmpNEInstr& instr) = 0;
    virtual void visit(FCmpLEInstr& instr) = 0;
    virtual void visit(FCmpGEInstr& instr) = 0;
    virtual void visit(FCmpLTInstr& instr) = 0;
    virtual void visit(FCmpGTInstr& instr) = 0;

    // object operations
    virtual void visit(ObjAllocInstr& instr) = 0;
    virtual void visit(ObjRetainInstr& instr) = 0;
    virtual void visit(ObjReleaseInstr& instr) = 0;
    virtual void visit(ObjGetTagInstr& instr) = 0;
    virtual void visit(ObjSetTagInstr& instr) = 0;
    virtual void visit(ObjGetSlotInstr& instr) = 0;
    virtual void visit(ObjSetSlotInstr& instr) = 0;
    virtual void visit(ObjTypeIdInstr& instr) = 0;
    virtual void visit(ObjIsTypeInstr& instr) = 0;
    virtual void visit(VCmpEQInstr& instr) = 0;
    virtual void visit(VCmpNEInstr& instr) = 0;
    virtual void visit(VCmpLTInstr& instr) = 0;
    virtual void visit(VCmpLEInstr& instr) = 0;
    virtual void visit(VCmpGTInstr& instr) = 0;
    virtual void visit(VCmpGEInstr& instr) = 0;

    // indirect function calls
    virtual void visit(IndirectCallInstr& instr) = 0;
    virtual void visit(IndirectTailCallInstr& instr) = 0;

    // lazy evaluation
    virtual void visit(FunctionRefInstr& instr) = 0;
    virtual void visit(LazyForceInstr& instr) = 0;
};

// =============================================================================
// Instruction base class
// =============================================================================

/**
 * Base class for native instructions.
 *
 * An instruction is derived from base class \c Value because its result can be
 * used as an operand for other instructions.
 *
 * @see IRBuilder
 * @see BasicBlock
 * @see IRFunction
 */
class Instr: public Value
{
  protected:
    Instr(const Instr& v);

  public:
    Instr(LiteralType ty, const std::vector<Value*>& ops = {}, const std::string& name = "");
    ~Instr() override;

    /**
     * Retrieves parent basic block this instruction is part of.
     */
    [[nodiscard]] BasicBlock* getBasicBlock() const { return _basicBlock; }

    /**
     * Read-only access to operands.
     */
    [[nodiscard]] const std::vector<Value*>& operands() const { return _operands; }

    /**
     * Retrieves n'th operand at given \p index.
     */
    [[nodiscard]] Value* operand(size_t index) const { return _operands[index]; }

    /**
     * Adds given operand \p value to the end of the operand list.
     */
    void addOperand(Value* value);

    /**
     * Sets operand at index \p i to given \p value.
     */
    Value* setOperand(size_t i, Value* value);

    /**
     * Replaces \p old operand with \p replacement.
     *
     * @returns number of actual performed replacements.
     */
    size_t replaceOperand(Value* old, Value* replacement);

    /**
     * Clears out all operands.
     */
    void clearOperands();

    /**
     * Replaces this instruction with the given @p newInstr.
     *
     * @returns ownership of this instruction.
     */
    std::unique_ptr<Instr> replace(std::unique_ptr<Instr> newInstr);

    /**
     * Clones given instruction.
     */
    virtual std::unique_ptr<Instr> clone() = 0;

    /**
     * Generic extension interface.
     */
    virtual void accept(InstructionVisitor& v) = 0;

    /// Set the source location for this instruction (for error reporting)
    void setSourceLocation(SourceLocation loc) { _sourceLocation = std::move(loc); }

    /// Get the source location of this instruction
    [[nodiscard]] SourceLocation const& sourceLocation() const noexcept { return _sourceLocation; }

  protected:
    void dumpOne(const char* mnemonic);
    [[nodiscard]] std::string formatOne(std::string mnemonic) const;

    void setParent(BasicBlock* bb) { _basicBlock = bb; }

    friend class BasicBlock;

  private:
    BasicBlock* _basicBlock = nullptr;
    std::vector<Value*> _operands;
    SourceLocation _sourceLocation;
};

// =============================================================================
// Concrete instruction classes
// =============================================================================

class NopInstr: public Instr
{
  public:
    NopInstr(): Instr(LiteralType::Void, {}, "nop") {}

    [[nodiscard]] std::string to_string() const override;
    [[nodiscard]] std::unique_ptr<Instr> clone() override;
    void accept(InstructionVisitor& v) override;
};

class AllocaInstr: public Instr
{
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
    // NOLINTNEXTLINE(clang-analyzer-core.CallAndMessage)
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

// User-Defined Function Call Instructions (UCALL/URET/UTCALL)

/// Calls a user-defined function compiled as a separate IRFunction.
class FunctionCallInstr: public Instr
{
  public:
    FunctionCallInstr(IRFunction* callee,
                      const std::vector<Value*>& args,
                      const std::string& name,
                      LiteralType returnType = LiteralType::Void);

    [[nodiscard]] IRFunction* callee() const { return _callee; }

    [[nodiscard]] size_t argc() const { return operands().size(); }

    [[nodiscard]] std::string to_string() const override;
    [[nodiscard]] std::unique_ptr<Instr> clone() override;
    void accept(InstructionVisitor& v) override;

  private:
    IRFunction* _callee;
};

/// Returns from a user-defined function back to the caller.
class FunctionRetInstr: public Instr
{
  public:
    explicit FunctionRetInstr(Value* result, const std::string& name = "");

    [[nodiscard]] Value* result() const { return operands().empty() ? nullptr : operand(0); }

    [[nodiscard]] std::string to_string() const override;
    [[nodiscard]] std::unique_ptr<Instr> clone() override;
    void accept(InstructionVisitor& v) override;
};

/// Tail-calls a user-defined function, reusing the current call frame.
class TailCallInstr: public Instr
{
  public:
    TailCallInstr(IRFunction* callee, const std::vector<Value*>& args, const std::string& name);

    [[nodiscard]] IRFunction* callee() const { return _callee; }

    [[nodiscard]] size_t argc() const { return operands().size(); }

    [[nodiscard]] std::string to_string() const override;
    [[nodiscard]] std::unique_ptr<Instr> clone() override;
    void accept(InstructionVisitor& v) override;

  private:
    IRFunction* _callee;
};

/// Produces the runtime function ID of a given IRFunction as a Number value.
class FunctionRefInstr: public Instr
{
  public:
    FunctionRefInstr(IRFunction* function, const std::string& name):
        Instr(LiteralType::Number, {}, name), _function(function)
    {
    }

    [[nodiscard]] IRFunction* function() const { return _function; }

    [[nodiscard]] std::string to_string() const override;
    [[nodiscard]] std::unique_ptr<Instr> clone() override;
    void accept(InstructionVisitor& v) override;

  private:
    IRFunction* _function;
};

/// Forces evaluation of a lazy value.
class LazyForceInstr: public Instr
{
  public:
    LazyForceInstr(Value* lazyObj, const std::string& name): Instr(LiteralType::Void, { lazyObj }, name) {}

    [[nodiscard]] Value* lazyObj() const { return operand(0); }

    [[nodiscard]] std::string to_string() const override;
    [[nodiscard]] std::unique_ptr<Instr> clone() override;
    void accept(InstructionVisitor& v) override;
};

/// Indirect call through a Callable object.
class IndirectCallInstr: public Instr
{
  public:
    IndirectCallInstr(Value* callable, std::vector<Value*> args, const std::string& name);

    [[nodiscard]] Value* callable() const { return operand(0); }

    [[nodiscard]] size_t argc() const { return operands().size() - 1; }

    [[nodiscard]] std::string to_string() const override;
    [[nodiscard]] std::unique_ptr<Instr> clone() override;
    void accept(InstructionVisitor& v) override;
};

/// Indirect tail call through a Callable object.
class IndirectTailCallInstr: public Instr
{
  public:
    IndirectTailCallInstr(Value* callable, std::vector<Value*> args, const std::string& name);

    [[nodiscard]] Value* callable() const { return operand(0); }

    [[nodiscard]] size_t argc() const { return operands().size() - 1; }

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

// =============================================================================
// UnaryInstr / BinaryInstr templates
// =============================================================================

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

class CondBrInstr: public TerminateInstr
{
  public:
    CondBrInstr(Value* cond, BasicBlock* trueBlock, BasicBlock* falseBlock);

    [[nodiscard]] Value* condition() const { return operands()[0]; }

    [[nodiscard]] BasicBlock* trueBlock() const { return (BasicBlock*) operands()[1]; }

    [[nodiscard]] BasicBlock* falseBlock() const { return (BasicBlock*) operands()[2]; }

    [[nodiscard]] std::string to_string() const override;
    [[nodiscard]] std::unique_ptr<Instr> clone() override;
    void accept(InstructionVisitor& v) override;
};

class BrInstr: public TerminateInstr
{
  public:
    explicit BrInstr(BasicBlock* targetBlock);

    [[nodiscard]] BasicBlock* targetBlock() const { return (BasicBlock*) operands()[0]; }

    [[nodiscard]] std::string to_string() const override;
    [[nodiscard]] std::unique_ptr<Instr> clone() override;
    void accept(InstructionVisitor& v) override;
};

class RetInstr: public TerminateInstr
{
  public:
    RetInstr(Value* result);

    [[nodiscard]] std::string to_string() const override;
    [[nodiscard]] std::unique_ptr<Instr> clone() override;
    void accept(InstructionVisitor& v) override;
};

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

// Object Instructions

class ObjAllocInstr: public Instr
{
  public:
    ObjAllocInstr(ConstantInt* typeId, const std::string& name): Instr(LiteralType::Object, { typeId }, name)
    {
    }

    [[nodiscard]] ConstantInt* typeId() const { return static_cast<ConstantInt*>(operand(0)); }

    [[nodiscard]] std::string to_string() const override;
    [[nodiscard]] std::unique_ptr<Instr> clone() override;
    void accept(InstructionVisitor& v) override;
};

class ObjRetainInstr: public Instr
{
  public:
    ObjRetainInstr(Value* object, const std::string& name): Instr(LiteralType::Object, { object }, name) {}

    [[nodiscard]] Value* object() const { return operand(0); }

    [[nodiscard]] std::string to_string() const override;
    [[nodiscard]] std::unique_ptr<Instr> clone() override;
    void accept(InstructionVisitor& v) override;
};

class ObjReleaseInstr: public Instr
{
  public:
    ObjReleaseInstr(AllocaInstr* storage, const std::string& name):
        Instr(LiteralType::Void, { storage }, name)
    {
    }

    [[nodiscard]] AllocaInstr* storage() const { return static_cast<AllocaInstr*>(operand(0)); }

    [[nodiscard]] std::string to_string() const override;
    [[nodiscard]] std::unique_ptr<Instr> clone() override;
    void accept(InstructionVisitor& v) override;
};

class ObjGetTagInstr: public Instr
{
  public:
    ObjGetTagInstr(Value* object, const std::string& name): Instr(LiteralType::Number, { object }, name) {}

    [[nodiscard]] Value* object() const { return operand(0); }

    [[nodiscard]] std::string to_string() const override;
    [[nodiscard]] std::unique_ptr<Instr> clone() override;
    void accept(InstructionVisitor& v) override;
};

class ObjSetTagInstr: public Instr
{
  public:
    ObjSetTagInstr(Value* object, Value* tag, const std::string& name):
        Instr(LiteralType::Object, { object, tag }, name)
    {
    }

    [[nodiscard]] Value* object() const { return operand(0); }

    [[nodiscard]] Value* tag() const { return operand(1); }

    [[nodiscard]] std::string to_string() const override;
    [[nodiscard]] std::unique_ptr<Instr> clone() override;
    void accept(InstructionVisitor& v) override;
};

class ObjGetSlotInstr: public Instr
{
  public:
    ObjGetSlotInstr(Value* object, ConstantInt* slotIndex, const std::string& name):
        Instr(LiteralType::Void, { object, slotIndex }, name)
    {
    }

    [[nodiscard]] Value* object() const { return operand(0); }

    [[nodiscard]] ConstantInt* slotIndex() const { return static_cast<ConstantInt*>(operand(1)); }

    [[nodiscard]] std::string to_string() const override;
    [[nodiscard]] std::unique_ptr<Instr> clone() override;
    void accept(InstructionVisitor& v) override;
};

class ObjSetSlotInstr: public Instr
{
  public:
    ObjSetSlotInstr(Value* object, ConstantInt* slotIndex, Value* value, const std::string& name):
        Instr(LiteralType::Object, { object, slotIndex, value }, name)
    {
    }

    [[nodiscard]] Value* object() const { return operand(0); }

    [[nodiscard]] ConstantInt* slotIndex() const { return static_cast<ConstantInt*>(operand(1)); }

    [[nodiscard]] Value* value() const { return operand(2); }

    [[nodiscard]] std::string to_string() const override;
    [[nodiscard]] std::unique_ptr<Instr> clone() override;
    void accept(InstructionVisitor& v) override;
};

class ObjTypeIdInstr: public Instr
{
  public:
    ObjTypeIdInstr(Value* object, const std::string& name): Instr(LiteralType::Number, { object }, name) {}

    [[nodiscard]] Value* object() const { return operand(0); }

    [[nodiscard]] std::string to_string() const override;
    [[nodiscard]] std::unique_ptr<Instr> clone() override;
    void accept(InstructionVisitor& v) override;
};

class ObjIsTypeInstr: public Instr
{
  public:
    ObjIsTypeInstr(Value* object, ConstantInt* typeId, const std::string& name):
        Instr(LiteralType::Boolean, { object, typeId }, name)
    {
    }

    [[nodiscard]] Value* object() const { return operand(0); }

    [[nodiscard]] ConstantInt* typeId() const { return static_cast<ConstantInt*>(operand(1)); }

    [[nodiscard]] std::string to_string() const override;
    [[nodiscard]] std::unique_ptr<Instr> clone() override;
    void accept(InstructionVisitor& v) override;
};

// Dynamic value comparison instructions

class VCmpEQInstr: public Instr
{
  public:
    VCmpEQInstr(Value* lhs, Value* rhs, const std::string& name):
        Instr(LiteralType::Boolean, { lhs, rhs }, name)
    {
    }

    [[nodiscard]] Value* lhs() const { return operand(0); }

    [[nodiscard]] Value* rhs() const { return operand(1); }

    [[nodiscard]] std::string to_string() const override;
    [[nodiscard]] std::unique_ptr<Instr> clone() override;
    void accept(InstructionVisitor& v) override;
};

class VCmpNEInstr: public Instr
{
  public:
    VCmpNEInstr(Value* lhs, Value* rhs, const std::string& name):
        Instr(LiteralType::Boolean, { lhs, rhs }, name)
    {
    }

    [[nodiscard]] Value* lhs() const { return operand(0); }

    [[nodiscard]] Value* rhs() const { return operand(1); }

    [[nodiscard]] std::string to_string() const override;
    [[nodiscard]] std::unique_ptr<Instr> clone() override;
    void accept(InstructionVisitor& v) override;
};

class VCmpLTInstr: public Instr
{
  public:
    VCmpLTInstr(Value* lhs, Value* rhs, const std::string& name):
        Instr(LiteralType::Boolean, { lhs, rhs }, name)
    {
    }

    [[nodiscard]] Value* lhs() const { return operand(0); }

    [[nodiscard]] Value* rhs() const { return operand(1); }

    [[nodiscard]] std::string to_string() const override;
    [[nodiscard]] std::unique_ptr<Instr> clone() override;
    void accept(InstructionVisitor& v) override;
};

class VCmpLEInstr: public Instr
{
  public:
    VCmpLEInstr(Value* lhs, Value* rhs, const std::string& name):
        Instr(LiteralType::Boolean, { lhs, rhs }, name)
    {
    }

    [[nodiscard]] Value* lhs() const { return operand(0); }

    [[nodiscard]] Value* rhs() const { return operand(1); }

    [[nodiscard]] std::string to_string() const override;
    [[nodiscard]] std::unique_ptr<Instr> clone() override;
    void accept(InstructionVisitor& v) override;
};

class VCmpGTInstr: public Instr
{
  public:
    VCmpGTInstr(Value* lhs, Value* rhs, const std::string& name):
        Instr(LiteralType::Boolean, { lhs, rhs }, name)
    {
    }

    [[nodiscard]] Value* lhs() const { return operand(0); }

    [[nodiscard]] Value* rhs() const { return operand(1); }

    [[nodiscard]] std::string to_string() const override;
    [[nodiscard]] std::unique_ptr<Instr> clone() override;
    void accept(InstructionVisitor& v) override;
};

class VCmpGEInstr: public Instr
{
  public:
    VCmpGEInstr(Value* lhs, Value* rhs, const std::string& name):
        Instr(LiteralType::Boolean, { lhs, rhs }, name)
    {
    }

    [[nodiscard]] Value* lhs() const { return operand(0); }

    [[nodiscard]] Value* rhs() const { return operand(1); }

    [[nodiscard]] std::string to_string() const override;
    [[nodiscard]] std::unique_ptr<Instr> clone() override;
    void accept(InstructionVisitor& v) override;
};

// =============================================================================
// IsSameInstruction
// =============================================================================

class IsSameInstruction: public InstructionVisitor
{
  public:
    static bool test(Instr* a, Instr* b);
    static bool isSameOperands(Instr* a, Instr* b);

  private:
    Instr* _other;
    bool _result = false;

  protected:
    explicit IsSameInstruction(Instr* a);

    void visit(NopInstr& instr) override;
    void visit(AllocaInstr& instr) override;
    void visit(StoreInstr& instr) override;
    void visit(LoadInstr& instr) override;
    void visit(PhiNode& instr) override;
    void visit(CallInstr& instr) override;
    void visit(FunctionCallInstr& instr) override;
    void visit(FunctionRetInstr& instr) override;
    void visit(TailCallInstr& instr) override;
    void visit(IndirectCallInstr& instr) override;
    void visit(IndirectTailCallInstr& instr) override;
    void visit(CondBrInstr& instr) override;
    void visit(BrInstr& instr) override;
    void visit(RetInstr& instr) override;
    void visit(MatchInstr& instr) override;
    void visit(RegExpGroupInstr& instr) override;
    void visit(CastInstr& instr) override;
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
    void visit(BNotInstr& instr) override;
    void visit(BAndInstr& instr) override;
    void visit(BOrInstr& instr) override;
    void visit(BXorInstr& instr) override;
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
    void visit(PCmpEQInstr& instr) override;
    void visit(PCmpNEInstr& instr) override;
    void visit(PInCidrInstr& instr) override;
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
    void visit(FunctionRefInstr& instr) override;
    void visit(LazyForceInstr& instr) override;
};

} // namespace CoreVM
