// SPDX-License-Identifier: Apache-2.0

#pragma once

namespace CoreVM
{
class IRHandler;
}

namespace CoreVM::transform
{

bool rewriteCondBrToSameBranches(IRHandler* handler);
bool eliminateUnusedInstr(IRHandler* handler);
bool eliminateLinearBr(IRHandler* handler);
bool foldConstantCondBr(IRHandler* handler);
bool rewriteBrToExit(IRHandler* handler);

} // namespace CoreVM::transform
