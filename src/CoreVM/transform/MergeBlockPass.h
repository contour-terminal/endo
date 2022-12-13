// SPDX-License-Identifier: Apache-2.0

#pragma once

namespace CoreVM
{
class IRHandler;
}

namespace CoreVM::transform
{

/**
 * Merges equal blocks into one, eliminating duplicated blocks.
 *
 * A block is equal if their instructions and their successors are equal.
 */
bool mergeSameBlocks(IRHandler* handler);

} // namespace CoreVM::transform
