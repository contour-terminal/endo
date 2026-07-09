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

/**
 * Promotes SSA values that are used outside their defining basic block into allocas
 * (store after definition, reload at each using block). The target code generator only
 * preserves allocas across block boundaries, so this is a required lowering step (not an
 * optimization) and must run for every function before target code generation.
 */
bool materializeCrossBlockValues(IRFunction* function);

} // namespace CoreVM::transform
