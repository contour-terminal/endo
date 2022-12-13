// SPDX-License-Identifier: Apache-2.0

#pragma once

namespace CoreVM
{
class IRHandler;
}

namespace CoreVM::transform
{

/**
 * Eliminates empty blocks, that are just jumping to the next block.
 */
bool emptyBlockElimination(IRHandler* handler);

} // namespace CoreVM::transform
