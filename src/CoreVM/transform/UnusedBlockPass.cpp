// SPDX-License-Identifier: Apache-2.0
#include <CoreVM/CoreVM.hpp>

#include <list>

namespace CoreVM::transform
{

bool eliminateUnusedBlocks(IRFunction* function)
{
    std::list<BasicBlock*> unused;

    for (BasicBlock* bb: function->basicBlocks())
    {
        if (bb == function->getEntryBlock())
            continue;

        if (!bb->predecessors().empty())
            continue;

        unused.push_back(bb);
    }

    for (BasicBlock* bb: unused)
    {
        // COREVM_TRACE("CoreVM: removing unused BasicBlock {}", bb->name());
        function->erase(bb);
    }

    return !unused.empty();
}

} // namespace CoreVM::transform
