// SPDX-License-Identifier: Apache-2.0
#pragma once

namespace CoreVM
{
class IRFunction;
}

namespace CoreVM::transform
{

/**
 * Eliminates empty blocks, that are just jumping to the next block.
 */
bool emptyBlockElimination(IRFunction* function);
bool rewriteCondBrToSameBranches(IRFunction* function);
bool eliminateUnusedInstr(IRFunction* function);
bool eliminateLinearBr(IRFunction* function);
bool foldConstantCondBr(IRFunction* function);
bool rewriteBrToExit(IRFunction* function);

/**
 * Merges equal blocks into one, eliminating duplicated blocks.
 *
 * A block is equal if their instructions and their successors are equal.
 */
bool mergeSameBlocks(IRFunction* function);

/**
 * Eliminates empty blocks, that are just jumping to the next block.
 */
bool eliminateUnusedBlocks(IRFunction* function);

} // namespace CoreVM::transform
