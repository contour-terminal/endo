// SPDX-License-Identifier: Apache-2.0

#include <CoreVM/ir/BasicBlock.h>
#include <CoreVM/ir/IRHandler.h>
#include <CoreVM/ir/Instructions.h>
#include <CoreVM/transform/UnusedBlockPass.h>

#include <list>

namespace CoreVM::transform
{

bool eliminateUnusedBlocks(IRHandler* handler)
{
    std::list<BasicBlock*> unused;

    for (BasicBlock* bb: handler->basicBlocks())
    {
        if (bb == handler->getEntryBlock())
            continue;

        if (!bb->predecessors().empty())
            continue;

        unused.push_back(bb);
    }

    for (BasicBlock* bb: unused)
    {
        // COREVM_TRACE("CoreVM: removing unused BasicBlock {}", bb->name());
        handler->erase(bb);
    }

    return !unused.empty();
}

} // namespace CoreVM::transform
