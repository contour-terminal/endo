// SPDX-License-Identifier: Apache-2.0
#include <CoreVM/CoreVM.hpp>

#include <list>

namespace CoreVM::transform
{

bool emptyBlockElimination(IRFunction* function)
{
    std::list<BasicBlock*> eliminated;

    for (BasicBlock* bb: function->basicBlocks())
    {
        if (bb->size() != 1)
            continue;

        if (auto* br = dynamic_cast<BrInstr*>(bb->getTerminator()))
        {
            BasicBlock* newSuccessor = br->targetBlock();
            eliminated.push_back(bb);
            if (bb == function->getEntryBlock())
            {
                function->setEntryBlock(bb);
                break;
            }
            else
            {
                for (BasicBlock* pred: bb->predecessors())
                {
                    pred->getTerminator()->replaceOperand(bb, newSuccessor);
                }
            }
        }
    }

    for (BasicBlock* bb: eliminated)
    {
        bb->getFunction()->erase(bb);
    }

    return !eliminated.empty();
}

} // namespace CoreVM::transform
