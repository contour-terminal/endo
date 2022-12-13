// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <CoreVM/MatchClass.h>
#include <CoreVM/Signature.h>
#include <CoreVM/ir/InstructionVisitor.h>
#include <CoreVM/ir/Value.h>
#include <CoreVM/vm/Instruction.h>

#include <list>
#include <string>
#include <vector>

namespace CoreVM
{

class Instr;
class BasicBlock;
class IRHandler;
class IRProgram;
class IRBuilder;

class InstructionVisitor;

/**
 * Base class for native instructions.
 *
 * An instruction is derived from base class \c Value because its result can be
 * used as an operand for other instructions.
 *
 * @see IRBuilder
 * @see BasicBlock
 * @see IRHandler
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
     *
     * This operation will potentially replace the value that has been at index \p
     *i before,
     * properly unlinking it from any uses or successor/predecessor links.
     */
    Value* setOperand(size_t i, Value* value);

    /**
     * Replaces \p old operand with \p replacement.
     *
     * @param old value to replace
     * @param replacement new value to put at the offset of \p old.
     *
     * @returns number of actual performed replacements.
     */
    size_t replaceOperand(Value* old, Value* replacement);

    /**
     * Clears out all operands.
     *
     * @see addOperand()
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
     *
     * This will not clone any of its operands but reference them.
     */
    virtual std::unique_ptr<Instr> clone() = 0;

    /**
     * Generic extension interface.
     *
     * @param v extension to pass this instruction to.
     *
     * @see InstructionVisitor
     */
    virtual void accept(InstructionVisitor& v) = 0;

  protected:
    void dumpOne(const char* mnemonic);
    [[nodiscard]] std::string formatOne(std::string mnemonic) const;

    void setParent(BasicBlock* bb) { _basicBlock = bb; }

    friend class BasicBlock;

  private:
    BasicBlock* _basicBlock;
    std::vector<Value*> _operands;
};

} // namespace CoreVM
