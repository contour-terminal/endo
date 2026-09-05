// SPDX-License-Identifier: Apache-2.0
#include "BuiltinSpecs.hpp"
#include <shell/Shell.hpp>
#include <shell/builtins/InlineCommandDescriptor.hpp>
#include <shell/builtins/WhichDescriptor.hpp>

namespace endo
{

std::vector<CommandSpec> createBuiltinSpecs()
{
    return generateBuiltinCompletionSpecs(Shell::inlineCommandDescriptors());
}

CommandSpec createWhichSpec()
{
    return specFromInlineDescriptor(whichDescriptor());
}

} // namespace endo
