// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <shell/completion/CommandSpec.hpp>

namespace endo
{

/// @brief Creates the dirconfig CommandSpec with subcommand definitions.
///
/// Defines subcommands: allow, deny, list, revoke, reload.
/// Subcommands that accept a path (allow, deny, revoke) complete directory paths.
[[nodiscard]] CommandSpec createDirconfigSpec();

} // namespace endo
