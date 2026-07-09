// SPDX-License-Identifier: Apache-2.0
#include <CoreVM/CoreVM.hpp>

#include <memory>
#include <unordered_map>
#include <vector>

namespace CoreVM::transform
{

// Materializes every SSA value that is used outside its defining basic block into a
// stack slot (alloca): the value is stored right after it is computed, and reloaded at
// the start of each block that uses it.
//
// The target code generator lowers SSA onto a stack machine in which only allocas keep
// a fixed frame slot across basic-block boundaries — every other temporary is discarded
// at a block boundary (see TargetCodeGenerator's block-entry stack reset). A value that
// is defined before a branch (`if`, `match`, `?|`, …) and used afterwards is otherwise
// lost, which crashes the generator. Promoting such values to allocas restores the
// invariant the generator relies on, so this is a required lowering step rather than an
// optimization, and must run for every function regardless of the optimization level.
bool materializeCrossBlockValues(IRFunction* function)
{
    IRProgram* program = function->getProgram();
    BasicBlock* entry = function->getEntryBlock();
    if (program == nullptr || entry == nullptr)
        return false;

    ConstantInt* zero = program->get(static_cast<int64_t>(0));
    ConstantInt* one = program->get(static_cast<int64_t>(1));

    // Snapshot the definitions up front: the loop below mutates instruction lists.
    std::vector<Instr*> definitions;
    for (BasicBlock* bb: function->basicBlocks())
        for (Instr* instr: bb->instructions())
            definitions.push_back(instr);

    bool changed = false;

    for (Instr* def: definitions)
    {
        if (def->type() == LiteralType::Void) // nothing to preserve
            continue;
        if (dynamic_cast<AllocaInstr*>(def) != nullptr) // already frame-resident
            continue;
        if (dynamic_cast<PhiNode*>(def) != nullptr) // phis are lowered separately
            continue;

        BasicBlock* defBlock = def->getBasicBlock();
        if (defBlock == nullptr)
            continue;

        // Uses that live in a different block of the SAME function are the ones at risk.
        // Uses in another function are closure captures, handled by the capture mechanism;
        // materializing them here would create a load referencing this function's frame
        // slot from inside a different function's frame.
        std::vector<Instr*> crossBlockUsers;
        for (Instr* user: def->uses())
        {
            BasicBlock* userBlock = user->getBasicBlock();
            if (userBlock != nullptr && userBlock != defBlock
                && userBlock->getFunction() == defBlock->getFunction())
                crossBlockUsers.push_back(user);
        }
        if (crossBlockUsers.empty())
            continue;

        // Reserve a slot and store the value once computed. Storing before the block
        // terminator places the store after the defining instruction while leaving the
        // value available to same-block uses (which keep referencing `def` directly).
        auto* slot = static_cast<AllocaInstr*>(
            entry->insertAfterAllocas(std::make_unique<AllocaInstr>(def->type(), one, "r2m.slot")));
        defBlock->insertBeforeTerminator(std::make_unique<StoreInstr>(slot, zero, def, "r2m.store"));

        // Reload at the top of each using block and rewire that block's uses to it. The
        // definition dominates every use (SSA), so the store dominates every reload.
        std::unordered_map<BasicBlock*, Value*> reloadPerBlock;
        for (Instr* user: crossBlockUsers)
        {
            BasicBlock* userBlock = user->getBasicBlock();
            auto const it = reloadPerBlock.find(userBlock);
            Value* reload = nullptr;
            if (it != reloadPerBlock.end())
            {
                reload = it->second;
            }
            else
            {
                reload = userBlock->insertAfterAllocas(std::make_unique<LoadInstr>(slot, "r2m.load"));
                reloadPerBlock[userBlock] = reload;
            }
            user->replaceOperand(def, reload);
        }

        changed = true;
    }

    return changed;
}

} // namespace CoreVM::transform
