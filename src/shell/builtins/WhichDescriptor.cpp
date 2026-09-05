// SPDX-License-Identifier: Apache-2.0
#include "WhichDescriptor.hpp"

namespace endo
{

// -h/--help is implicit: generateInlineHelp() and generateBuiltinCompletionSpecs()
// both append it, so listing it here would duplicate it.
static constexpr InlineOptionDef WhichOptions[] = {
    { .shortFlag = "-a",
      .longFlag = "--all",
      .description = "Print all matching executables in PATH, not just the first" },
    { .shortFlag = "-i",
      .longFlag = "--read-alias",
      .description = "Also show aliases (not yet implemented)" },
};

static constexpr InlineCommandDescriptor WhichCommand = {
    .name = "which",
    .briefDescription = "Locate executables in the PATH.",
    .usageLine = "which [OPTIONS] PROGRAM...",
    .options = WhichOptions,
    .positionalQuery = { .queryTag = "path-commands", .description = "Program name", .repeatable = true },
};

InlineCommandDescriptor const& whichDescriptor()
{
    return WhichCommand;
}

} // namespace endo
