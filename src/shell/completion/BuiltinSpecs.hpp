// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <shell/completion/CommandSpec.hpp>

#include <vector>

namespace endo
{

/// @brief Creates CommandSpec entries for all inline builtin commands.
/// @return A vector of CommandSpec definitions for builtins.
std::vector<CommandSpec> createBuiltinSpecs();

} // namespace endo
