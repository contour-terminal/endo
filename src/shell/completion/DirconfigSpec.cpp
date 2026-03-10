// SPDX-License-Identifier: Apache-2.0
#include "DirconfigSpec.hpp"

namespace endo
{

CommandSpec createDirconfigSpec()
{
    return CommandSpec {
        .command = "dirconfig",
        .description = "Manage per-directory configuration and trust decisions",
        .globalOptions = {},
        .subcommands = {
            SubcommandDef {
                .name = "allow",
                .description = "Trust and load a directory config",
                .options = {},
                .positionalArgs = { ArgDef {
                    .kind = ArgKind::Path,
                    .description = "Path to .local-env.endo or its directory",
                } },
            },
            SubcommandDef {
                .name = "deny",
                .description = "Deny a directory config",
                .options = {},
                .positionalArgs = { ArgDef {
                    .kind = ArgKind::Path,
                    .description = "Path to .local-env.endo or its directory",
                } },
            },
            SubcommandDef {
                .name = "list",
                .description = "List all trust decisions",
            },
            SubcommandDef {
                .name = "revoke",
                .description = "Revoke trust for a directory config",
                .options = {},
                .positionalArgs = { ArgDef {
                    .kind = ArgKind::Path,
                    .description = "Path to .local-env.endo or its directory",
                } },
            },
            SubcommandDef {
                .name = "reload",
                .description = "Reload all active directory configs",
            },
        },
    };
}

} // namespace endo
