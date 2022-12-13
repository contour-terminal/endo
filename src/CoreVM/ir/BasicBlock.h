// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <CoreVM/MatchClass.h>
#include <CoreVM/Signature.h>
#include <CoreVM/ir/InstructionVisitor.h>
#include <CoreVM/ir/Value.h>
#include <CoreVM/util/unbox.h>
#include <CoreVM/vm/Instruction.h>

#include <string>
#include <vector>

namespace CoreVM
{

class Instr;
class TerminateInstr;
class IRHandler;
class IRBuilder;

/**
 * An SSA based instruction basic block.
 *
 * @see Instr, IRHandler, IRBuilder
 */
class BasicBlock: public Value
{
  public:
    BasicBlock(const std::string& name, IRHandler* parent);
    ~BasicBlock() override;

    [[nodiscard]] IRHandler* getHandler() const { return _handler; }
    void setParent(IRHandler* handler) { _handler = handler; }

    /*!
     * Retrieves the last terminating instruction in this basic block.
     *
     * This instruction must be a termination instruction, such as
     * a branching instruction or a handler terminating instruction.
     *
     * @see BrInstr, CondBrInstr, MatchInstr, RetInstr
     */
    [[nodiscard]] TerminateInstr* getTerminator() const;

    /**
     * Checks whether this BasicBlock is assured to terminate, hence, complete.
     *
     * This is either achieved by having a TerminateInstr at the end or a NativeCallback
     * that never returns.
     */
    [[nodiscard]] bool isComplete() const;

    /**
     * Retrieves the linear ordered list of instructions of instructions in this
     * basic block.
     */
    auto instructions() { return util::unbox(_code); }
    Instr* instruction(size_t i) { return _code[i].get(); }

    [[nodiscard]] Instr* front() const { return _code.front().get(); }
    [[nodiscard]] Instr* back() const { return _code.back().get(); }

    [[nodiscard]] size_t size() const { return _code.size(); }
    [[nodiscard]] bool empty() const { return _code.empty(); }

    [[nodiscard]] Instr* back(size_t sub) const
    {
        if (sub + 1 <= _code.size())
            return _code[_code.size() - (1 + sub)].get();
        else
            return nullptr;
    }

    /**
     * Appends a new instruction, \p instr, to this basic block.
     *
     * The basic block will take over ownership of the given instruction.
     */
    Instr* push_back(std::unique_ptr<Instr> instr);

    /**
     * Removes given instruction from this basic block.
     *
     * The basic block will pass ownership of the given instruction to the caller.
     * That means, the caller has to either delete \p childInstr or transfer it to
     * another basic block.
     *
     * @see push_back()
     */
    std::unique_ptr<Instr> remove(Instr* childInstr);

    /**
     * Replaces given @p oldInstr with @p newInstr.
     *
     * @return returns given @p oldInstr.
     */
    std::unique_ptr<Instr> replace(Instr* oldInstr, std::unique_ptr<Instr> newInstr);

    /**
     * Merges given basic block's instructions into this ones end.
     *
     * The passed basic block's instructions <b>WILL</b> be touched, that is
     * - all instructions will have been moved.
     * - any successor BBs will have been relinked
     */
    void merge_back(BasicBlock* bb);

    /**
     * Moves this basic block after the other basic block, \p otherBB.
     *
     * @param otherBB the future prior basic block.
     *
     * In a function, all basic blocks (starting from the entry block)
     * will be aligned linear into the execution segment.
     *
     * This function moves this basic block directly after
     * the other basic block, \p otherBB.
     *
     * @see moveBefore()
     */
    void moveAfter(const BasicBlock* otherBB);

    /**
     * Moves this basic block before the other basic block, \p otherBB.
     *
     * @see moveAfter()
     */
    void moveBefore(const BasicBlock* otherBB);

    /**
     * Tests whether or not given block is straight-line located after this block.
     *
     * @retval true \p otherBB is straight-line located after this block.
     * @retval false \p otherBB is not straight-line located after this block.
     *
     * @see moveAfter()
     */
    bool isAfter(const BasicBlock* otherBB) const;

    /**
     * Links given \p successor basic block to this predecessor.
     *
     * @param successor the basic block to link as an successor of this basic
     *                  block.
     *
     * This will also automatically link this basic block as
     * future predecessor of the \p successor.
     *
     * @see unlinkSuccessor()
     * @see successors(), predecessors()
     */
    void linkSuccessor(BasicBlock* successor);

    /**
     * Unlink given \p successor basic block from this predecessor.
     *
     * @see linkSuccessor()
     * @see successors(), predecessors()
     */
    void unlinkSuccessor(BasicBlock* successor);

    /** Retrieves all predecessors of given basic block. */
    [[nodiscard]] std::vector<BasicBlock*>& predecessors() { return _predecessors; }

    /** Retrieves all uccessors of the given basic block. */
    [[nodiscard]] std::vector<BasicBlock*>& successors() { return _successors; }
    [[nodiscard]] const std::vector<BasicBlock*>& successors() const { return _successors; }

    /** Retrieves all dominators of given basic block. */
    [[nodiscard]] std::vector<BasicBlock*> dominators();

    /** Retrieves all immediate dominators of given basic block. */
    [[nodiscard]] std::vector<BasicBlock*> immediateDominators();

    void dump();

    /**
     * Performs sanity checks on internal data structures.
     *
     * This call does not return any success or failure as every failure is
     * considered fatal and will cause the program to exit with diagnostics
     * as this is most likely caused by an application programming error.
     *
     * @note This function is automatically invoked by IRHandler::verify()
     *
     * @see IRHandler::verify()
     */
    void verify();

  private:
    void collectIDom(std::vector<BasicBlock*>& output);

  private:
    IRHandler* _handler;
    std::vector<std::unique_ptr<Instr>> _code;
    std::vector<BasicBlock*> _predecessors;
    std::vector<BasicBlock*> _successors;

    friend class IRBuilder;
    friend class Instr;
};

} // namespace CoreVM
