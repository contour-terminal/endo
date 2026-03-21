// SPDX-License-Identifier: Apache-2.0
#include "HistorySpec.hpp"

namespace endo
{

CommandSpec createHistorySpec()
{
    return CommandSpec {
        .command = "history",
        .description = "Display or manage command history",
        .globalOptions = {
            OptionDef { .longName = "--help", .shortName = "-h", .description = "Show help" },
        },
        .subcommands = {
            SubcommandDef {
                .name = "clear",
                .description = "Clear all history entries",
            },
            SubcommandDef {
                .name = "search",
                .description = "Search entries by prefix",
                .options = {},
                .positionalArgs = { ArgDef {
                    .kind = ArgKind::Any,
                    .description = "Search pattern",
                } },
            },
        },
    };
}

} // namespace endo
