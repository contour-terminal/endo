// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <shell/completion/CommandSpec.hpp>

#include <vector>

namespace endo
{

/// @brief Creates CommandSpec entries for all inline builtin commands.
/// @return A vector of CommandSpec definitions for builtins.
[[nodiscard]] std::vector<CommandSpec> createBuiltinSpecs();

/// @brief Creates the CommandSpec for the `which` builtin.
///
/// `which` is a parser-level builtin rather than an inline one, so createBuiltinSpecs()
/// does not cover it; its spec is generated from whichDescriptor() instead. Its positional
/// argument resolves through the "path-commands" query tag, served by
/// PathCommandQueryProvider.
///
/// @return The completion spec for `which`.
[[nodiscard]] CommandSpec createWhichSpec();

} // namespace endo
