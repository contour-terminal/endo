// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <span>
#include <string_view>

namespace endo::format
{

/// Runs the `endo format` subcommand.
///
/// Formats Endo source files according to the project's formatting conventions.
///
/// Options:
/// - `--check`: Check if files are formatted without modifying them (exit 1 if not).
/// - `--diff`: Show the diff between original and formatted output.
/// - `--stdout`: Print formatted output to stdout instead of writing in-place.
/// - `--config PATH`: Use a specific `.endo-format` config file.
///
/// @param args Arguments after `endo format` (file paths and options).
/// @return 0 on success, 1 on failure or check mismatch.
int runFormatCommand(std::span<char const* const> args);

} // namespace endo::format
