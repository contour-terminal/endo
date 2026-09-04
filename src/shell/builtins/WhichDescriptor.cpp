// SPDX-License-Identifier: Apache-2.0
#include "WhichDescriptor.hpp"

#include <array>

namespace endo
{

namespace
{
    // -h/--help is implicit: generateInlineHelp() and generateBuiltinCompletionSpecs()
    // both append it, so listing it here would duplicate it.
    constexpr std::array<InlineOptionDef, 2> WhichOptions { {
        { .shortFlag = "-a",
          .longFlag = "--all",
          .description = "Print all matching executables in PATH, not just the first" },
        { .shortFlag = "-i",
          .longFlag = "--read-alias",
          .description = "Also show aliases (not yet implemented)" },
    } };
} // namespace

InlineCommandDescriptor const& whichDescriptor()
{
    static constexpr auto Descriptor = InlineCommandDescriptor {
        .name = "which",
        .briefDescription = "Locate executables in the PATH.",
        .usageLine = "which [OPTIONS] PROGRAM...",
        .options = WhichOptions,
        .positionalQuery = { .queryTag = "path-commands", .description = "Program name", .repeatable = true },
    };
    return Descriptor;
}

} // namespace endo
