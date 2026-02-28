// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <span>

namespace endo::agent::local
{

/// Runs the `endo agent models` CLI subcommand.
///
/// Subcommands:
/// - `list`           — Show available and downloaded models
/// - `download <name> [--quant Q4_K_M]` — Download a curated model
/// - `remove <name>`  — Delete a downloaded model
/// - `info <name>`    — Show detailed model information
///
/// @param args Command-line arguments after "endo agent models".
/// @return EXIT_SUCCESS on success, EXIT_FAILURE on error.
[[nodiscard]] auto runModelsCommand(std::span<char const* const> args) -> int;

} // namespace endo::agent::local
