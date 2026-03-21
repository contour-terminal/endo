// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <shell/completion/CommandSpec.hpp>

namespace endo
{

/// @brief Creates the history CommandSpec with subcommand definitions.
///
/// Defines subcommands: clear, search.
[[nodiscard]] CommandSpec createHistorySpec();

} // namespace endo
