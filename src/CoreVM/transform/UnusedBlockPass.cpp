// SPDX-License-Identifier: Apache-2.0
module;
#include <list>

module CoreVM;
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
