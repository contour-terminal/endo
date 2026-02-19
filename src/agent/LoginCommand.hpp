// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string_view>

namespace endo::agent
{

/// Runs the interactive `endo agent login` flow.
/// @param providerHint Optional provider name from CLI args (empty = prompt user).
/// @return EXIT_SUCCESS on success, EXIT_FAILURE on error.
[[nodiscard]] auto runLoginCommand(std::string_view providerHint = {}) -> int;

/// Runs `endo agent status` — shows configured providers and authentication status.
/// @return EXIT_SUCCESS on success, EXIT_FAILURE on error.
[[nodiscard]] auto runStatusCommand() -> int;

/// Runs `endo agent logout` — removes stored API key for a provider.
/// @param providerHint Optional provider name (empty = prompt user).
/// @return EXIT_SUCCESS on success, EXIT_FAILURE on error.
[[nodiscard]] auto runLogoutCommand(std::string_view providerHint = {}) -> int;

} // namespace endo::agent
