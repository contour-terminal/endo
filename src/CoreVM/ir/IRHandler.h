// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <CoreVM/ir/BasicBlock.h>
#include <CoreVM/ir/Constant.h>
#include <CoreVM/util/unbox.h>

#include <list>
#include <memory>
#include <string>

namespace CoreVM
{

class BasicBlock;
class IRProgram;
class IRBuilder;

class IRHandler: public Constant
{
  public:
    IRHandler(const std::string& name, IRProgram* parent);
    ~IRHandler() override;

    IRHandler(IRHandler&&) = delete;
    IRHandler& operator=(IRHandler&&) = delete;

    BasicBlock* createBlock(const std::string& name = "");

    [[nodiscard]] IRProgram* getProgram() const { return _program; }
    void setParent(IRProgram* prog) { _program = prog; }

    void dump();

    [[nodiscard]] bool empty() const noexcept { return _blocks.empty(); }

    auto basicBlocks() { return util::unbox(_blocks); }

    [[nodiscard]] BasicBlock* getEntryBlock() const { return _blocks.front().get(); }
    void setEntryBlock(BasicBlock* bb);

    /**
     * Unlinks and deletes given basic block \p bb from handler.
     *
     * @note \p bb will be a dangling pointer after this call.
     */
    void erase(BasicBlock* bb);

    bool isAfter(const BasicBlock* bb, const BasicBlock* afterThat) const;
    void moveAfter(const BasicBlock* moveable, const BasicBlock* afterThat);
    void moveBefore(const BasicBlock* moveable, const BasicBlock* beforeThat);

    /**
     * Performs given transformation on this handler.
     */
    template <typename TheHandlerPass, typename... Args>
    size_t transform(Args&&... args)
    {
        return TheHandlerPass(std::forward(args)...).run(this);
    }

    /**
     * Performs sanity checks on internal data structures.
     *
     * This call does not return any success or failure as every failure is
     * considered fatal and will cause the program to exit with diagnostics
     * as this is most likely caused by an application programming error.
     *
     * @note Always call this on completely defined handlers and never on
     * half-contructed ones.
     */
    void verify();

  private:
    IRProgram* _program;
    std::list<std::unique_ptr<BasicBlock>> _blocks;

    friend class IRBuilder;
};

} // namespace CoreVM
