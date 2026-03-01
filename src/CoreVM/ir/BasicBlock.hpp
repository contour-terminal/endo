// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <CoreVM/ir/Value.hpp>

#include <memory>
#include <string>
#include <vector>

namespace CoreVM
{

class Instr;
class TerminateInstr;
class IRFunction;
class IRBuilder;

/**
 * An SSA based instruction basic block.
 *
 * @see Instr, IRFunction, IRBuilder
 */
class BasicBlock: public Value
{
  public:
    BasicBlock(const std::string& name, IRFunction* parent);
    ~BasicBlock() override;

    [[nodiscard]] IRFunction* getFunction() const { return _function; }

    void setParent(IRFunction* function) { _function = function; }

    /*!
     * Retrieves the last terminating instruction in this basic block.
     *
     * @see BrInstr, CondBrInstr, MatchInstr, RetInstr
     */
    [[nodiscard]] TerminateInstr* getTerminator() const;

    /**
     * Checks whether this BasicBlock is assured to terminate, hence, complete.
     */
    [[nodiscard]] bool isComplete() const;

    /**
     * Retrieves the linear ordered list of instructions in this basic block.
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
     */
    Instr* push_back(std::unique_ptr<Instr> instr);

    /**
     * Inserts an instruction before the terminator (if present) or at the end.
     */
    Instr* insertBeforeTerminator(std::unique_ptr<Instr> instr);

    /**
     * Inserts an alloca instruction after the last existing alloca in this block.
     */
    Instr* insertAfterAllocas(std::unique_ptr<Instr> instr);

    /**
     * Removes given instruction from this basic block.
     */
    std::unique_ptr<Instr> remove(Instr* childInstr);

    /**
     * Replaces given @p oldInstr with @p newInstr.
     */
    std::unique_ptr<Instr> replace(Instr* oldInstr, std::unique_ptr<Instr> newInstr);

    /**
     * Merges given basic block's instructions into this ones end.
     */
    void merge_back(BasicBlock* bb);

    void moveAfter(const BasicBlock* otherBB);
    void moveBefore(const BasicBlock* otherBB);
    bool isAfter(const BasicBlock* otherBB) const;

    void linkSuccessor(BasicBlock* successor);
    void unlinkSuccessor(BasicBlock* successor);

    [[nodiscard]] std::vector<BasicBlock*>& predecessors() { return _predecessors; }

    [[nodiscard]] std::vector<BasicBlock*>& successors() { return _successors; }

    [[nodiscard]] const std::vector<BasicBlock*>& successors() const { return _successors; }

    [[nodiscard]] std::vector<BasicBlock*> dominators();
    [[nodiscard]] std::vector<BasicBlock*> immediateDominators();

    void dump();
    [[nodiscard]] std::string dumpToString() const;

    void verify();

  private:
    void collectIDom(std::vector<BasicBlock*>& output);

  private:
    IRFunction* _function;
    std::vector<std::unique_ptr<Instr>> _code;
    std::vector<BasicBlock*> _predecessors;
    std::vector<BasicBlock*> _successors;

    friend class IRBuilder;
    friend class Instr;
};

} // namespace CoreVM
