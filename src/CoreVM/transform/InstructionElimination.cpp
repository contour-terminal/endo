// SPDX-License-Identifier: Apache-2.0
module;
#include <memory>

module CoreVM;
namespace CoreVM::transform
{

/*
 * Rewrites CONDBR (%foo, %foo) to BR (%foo) as both target branch pointers
 * point to the same branch.
 */
bool rewriteCondBrToSameBranches(IRHandler* handler)
{
    for (BasicBlock* bb: handler->basicBlocks())
    {
        // attempt to eliminate useless condbr
        if (CondBrInstr* condbr = dynamic_cast<CondBrInstr*>(bb->getTerminator()))
        {
            if (condbr->trueBlock() != condbr->falseBlock())
                return false;

            BasicBlock* nextBB = condbr->trueBlock();

            // remove old terminator
            bb->remove(condbr);

            // create new terminator
            bb->push_back(std::make_unique<BrInstr>(nextBB));

            // COREVM_TRACE("CoreVM: rewrote condbr with true-block == false-block");
            return true;
        }
    }

    return false;
}

bool eliminateUnusedInstr(IRHandler* handler)
{
    for (BasicBlock* bb: handler->basicBlocks())
    {
        for (Instr* instr: bb->instructions())
        {
            if (auto f = dynamic_cast<CallInstr*>(instr))
            {
                if (f->callee()->getNative().isReadOnly())
                {
                    if (instr->type() != LiteralType::Void && !instr->isUsed())
                    {
                        bb->remove(instr);
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

/*
 * Eliminates BR instructions to basic blocks that are only referenced by one
 * basic block
 * by eliminating the BR and merging the BR instructions target block at the end
 * of the current block.
 */
bool eliminateLinearBr(IRHandler* handler)
{
    for (BasicBlock* bb: handler->basicBlocks())
    {
        // attempt to eliminate useless linear br
        if (BrInstr* br = dynamic_cast<BrInstr*>(bb->getTerminator()))
        {
            if (br->targetBlock()->predecessors().size() != 1)
                return false;

            if (br->targetBlock()->predecessors().front() != bb)
                return false;

            // we are the only predecessor of BR's target block, so merge them
            BasicBlock* nextBB = br->targetBlock();

            // COREVM_TRACE("CoreVM: eliminate linear BR-instruction from {} to {}", bb->name(),
            // nextBB->name());

            // remove old terminator
            bb->remove(br);

            // merge nextBB
            bb->merge_back(nextBB);

            // destroy unused BB
            // bb->getHandler()->erase(nextBB);

            return true;
        }
    }

    return false;
}

bool foldConstantCondBr(IRHandler* handler)
{
    for (BasicBlock* bb: handler->basicBlocks())
    {
        if (auto condbr = dynamic_cast<CondBrInstr*>(bb->getTerminator()))
        {
            if (auto cond = dynamic_cast<ConstantBoolean*>(condbr->condition()))
            {
                // COREVM_TRACE("CoreVM: rewrite condbr %{} with constant expression %{}", condbr->name(),
                // cond->name());
                std::pair<BasicBlock*, BasicBlock*> use;

                if (cond->get())
                {
                    // COREVM_TRACE("if-condition is always true");
                    use = std::make_pair(condbr->trueBlock(), condbr->falseBlock());
                }
                else
                {
                    // COREVM_TRACE("if-condition is always false");
                    use = std::make_pair(condbr->falseBlock(), condbr->trueBlock());
                }

                auto x = bb->remove(condbr);
                x.reset(nullptr);
                bb->push_back(std::make_unique<BrInstr>(use.first));
                return true;
            }
        }
    }

    return false;
}

/*
 * Eliminates a superfluous BR instruction to a basic block that just exits.
 *
 * This will highly increase the number of exit points but reduce
 * the number of executed instructions for each path.
 */
bool rewriteBrToExit(IRHandler* handler)
{
    for (BasicBlock* bb: handler->basicBlocks())
    {
        if (BrInstr* br = dynamic_cast<BrInstr*>(bb->getTerminator()))
        {
            BasicBlock* targetBB = br->targetBlock();

            if (targetBB->size() != 1)
                return false;

            if (bb->isAfter(targetBB))
                return false;

            if (RetInstr* ret = dynamic_cast<RetInstr*>(targetBB->getTerminator()))
            {
                bb->remove(br);
                bb->push_back(ret->clone());

                // COREVM_TRACE("CoreVM: eliminate branch-to-exit block");
                return true;
            }
        }
    }

    return false;
}

} // namespace CoreVM::transform
