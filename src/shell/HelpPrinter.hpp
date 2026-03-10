// SPDX-License-Identifier: Apache-2.0
#pragma once

namespace endo
{

/// Prints the help message to stdout with optional ANSI color.
/// Detects color support via isatty(STDOUT_FILENO) and NO_COLOR.
void printHelp();

/// Prints the version information to stdout.
void printVersion();

} // namespace endo
