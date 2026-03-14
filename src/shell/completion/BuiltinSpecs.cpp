// SPDX-License-Identifier: Apache-2.0
#include "BuiltinSpecs.hpp"

namespace endo
{

std::vector<CommandSpec> createBuiltinSpecs()
{
    return {
        // --- System info ---
        CommandSpec {
            .command = "whoami",
            .description = "Print current username",
            .globalOptions = {
                OptionDef { .longName = "--help", .shortName = "-h", .description = "Show help" },
            },
        },
        CommandSpec {
            .command = "hostname",
            .description = "Print machine hostname",
            .globalOptions = {
                OptionDef { .longName = "--help", .shortName = "-h", .description = "Show help" },
            },
        },
        CommandSpec {
            .command = "date",
            .description = "Print current date and time",
            .globalOptions = {
                OptionDef { .longName = "--format", .shortName = "-f", .description = "Output format string",
                            .valueKind = OptionValueKind::String },
                OptionDef { .longName = "--epoch", .description = "Print seconds since Unix epoch" },
                OptionDef { .longName = "--iso", .description = "Print in ISO 8601 format" },
                OptionDef { .longName = "--utc", .shortName = "-u", .description = "Use UTC instead of local time" },
                OptionDef { .longName = "--date", .shortName = "-d", .description = "Display given date string",
                            .valueKind = OptionValueKind::String },
                OptionDef { .longName = "--help", .shortName = "-h", .description = "Show help" },
            },
        },
        CommandSpec {
            .command = "uname",
            .description = "Print system information",
            .globalOptions = {
                OptionDef { .longName = "--all", .shortName = "-a", .description = "Print all information" },
                OptionDef { .longName = "--kernel-name", .shortName = "-s", .description = "Print kernel name" },
                OptionDef { .longName = "--nodename", .shortName = "-n", .description = "Print network node hostname" },
                OptionDef { .longName = "--kernel-release", .shortName = "-r", .description = "Print kernel release" },
                OptionDef { .longName = "--machine", .shortName = "-m", .description = "Print machine hardware name" },
                OptionDef { .longName = "--help", .description = "Show help" },
            },
        },

        // --- Path utilities ---
        CommandSpec {
            .command = "basename",
            .description = "Strip directory from path",
            .globalOptions = {
                OptionDef { .longName = "--help", .description = "Show help" },
            },
            .positionalArgs = {
                ArgDef { .kind = ArgKind::Path, .description = "Path to extract filename from" },
                ArgDef { .kind = ArgKind::Any, .description = "Suffix to remove" },
            },
        },
        CommandSpec {
            .command = "dirname",
            .description = "Strip last component from path",
            .globalOptions = {
                OptionDef { .longName = "--help", .description = "Show help" },
            },
            .positionalArgs = {
                ArgDef { .kind = ArgKind::Path, .description = "Path to extract directory from" },
            },
        },
        CommandSpec {
            .command = "realpath",
            .description = "Resolve to absolute canonical path",
            .globalOptions = {
                OptionDef { .longName = "--help", .shortName = "-h", .description = "Show help" },
            },
            .positionalArgs = {
                ArgDef { .kind = ArgKind::Path, .description = "Path to resolve", .repeatable = true },
            },
        },
        CommandSpec {
            .command = "touch",
            .description = "Create file or update timestamps",
            .globalOptions = {
                OptionDef { .longName = "--no-create", .shortName = "-c", .description = "Do not create files" },
                OptionDef { .longName = "--help", .shortName = "-h", .description = "Show help" },
            },
            .positionalArgs = {
                ArgDef { .kind = ArgKind::Path, .description = "File(s) to touch", .repeatable = true },
            },
        },
        CommandSpec {
            .command = "ln",
            .description = "Create links between files",
            .globalOptions = {
                OptionDef { .longName = "--symbolic", .shortName = "-s", .description = "Create symbolic link" },
                OptionDef { .longName = "--force", .shortName = "-f", .description = "Remove existing destination" },
                OptionDef { .longName = "--verbose", .shortName = "-v", .description = "Explain what is being done" },
                OptionDef { .longName = "--help", .description = "Show help" },
            },
            .positionalArgs = {
                ArgDef { .kind = ArgKind::Path, .description = "Target" },
                ArgDef { .kind = ArgKind::Path, .description = "Link name" },
            },
        },
        CommandSpec {
            .command = "mktemp",
            .description = "Create temporary file or directory",
            .globalOptions = {
                OptionDef { .longName = "--directory", .shortName = "-d", .description = "Create a directory" },
                OptionDef { .longName = "--tmpdir", .shortName = "-p", .description = "Use DIR as base",
                            .valueKind = OptionValueKind::Path },
                OptionDef { .longName = "--help", .description = "Show help" },
            },
        },

        // --- Text processing ---
        CommandSpec {
            .command = "head",
            .description = "Output first lines of files",
            .globalOptions = {
                OptionDef { .longName = "--lines", .shortName = "-n", .description = "Number of lines",
                            .valueKind = OptionValueKind::String },
                OptionDef { .longName = "--help", .shortName = "-h", .description = "Show help" },
            },
            .positionalArgs = {
                ArgDef { .kind = ArgKind::Path, .description = "File(s)", .repeatable = true },
            },
        },
        CommandSpec {
            .command = "tail",
            .description = "Output last lines of files",
            .globalOptions = {
                OptionDef { .longName = "--lines", .shortName = "-n", .description = "Number of lines",
                            .valueKind = OptionValueKind::String },
                OptionDef { .longName = "--follow", .shortName = "-f", .description = "Follow file changes" },
                OptionDef { .longName = "--help", .shortName = "-h", .description = "Show help" },
            },
            .positionalArgs = {
                ArgDef { .kind = ArgKind::Path, .description = "File(s)", .repeatable = true },
            },
        },
        CommandSpec {
            .command = "wc",
            .description = "Count lines, words, and characters",
            .globalOptions = {
                OptionDef { .longName = "--lines", .shortName = "-l", .description = "Print line count" },
                OptionDef { .longName = "--words", .shortName = "-w", .description = "Print word count" },
                OptionDef { .longName = "--chars", .shortName = "-c", .description = "Print character count" },
                OptionDef { .longName = "--help", .description = "Show help" },
            },
            .positionalArgs = {
                ArgDef { .kind = ArgKind::Path, .description = "File(s)", .repeatable = true },
            },
        },
        CommandSpec {
            .command = "sort",
            .description = "Sort lines of text",
            .globalOptions = {
                OptionDef { .longName = "--reverse", .shortName = "-r", .description = "Reverse sort order" },
                OptionDef { .longName = "--numeric-sort", .shortName = "-n", .description = "Compare according to string numerical value" },
                OptionDef { .longName = "--unique", .shortName = "-u", .description = "Output only unique lines" },
                OptionDef { .longName = "--key", .shortName = "-k", .description = "Sort by key field",
                            .valueKind = OptionValueKind::String },
                OptionDef { .longName = "--help", .description = "Show help" },
            },
            .positionalArgs = {
                ArgDef { .kind = ArgKind::Path, .description = "File(s)", .repeatable = true },
            },
        },
        CommandSpec {
            .command = "uniq",
            .description = "Filter duplicate adjacent lines",
            .globalOptions = {
                OptionDef { .longName = "--count", .shortName = "-c", .description = "Prefix lines with occurrence count" },
                OptionDef { .longName = "--repeated", .shortName = "-d", .description = "Only print duplicate lines" },
                OptionDef { .longName = "--ignore-case", .shortName = "-i", .description = "Ignore case" },
                OptionDef { .longName = "--help", .description = "Show help" },
            },
            .positionalArgs = {
                ArgDef { .kind = ArgKind::Path, .description = "File(s)", .repeatable = true },
            },
        },
        CommandSpec {
            .command = "cut",
            .description = "Extract fields or characters",
            .globalOptions = {
                OptionDef { .longName = "--delimiter", .shortName = "-d", .description = "Field delimiter",
                            .valueKind = OptionValueKind::String },
                OptionDef { .longName = "--fields", .shortName = "-f", .description = "Select fields",
                            .valueKind = OptionValueKind::String },
                OptionDef { .longName = "--characters", .shortName = "-c", .description = "Select characters",
                            .valueKind = OptionValueKind::String },
                OptionDef { .longName = "--help", .description = "Show help" },
            },
            .positionalArgs = {
                ArgDef { .kind = ArgKind::Path, .description = "File(s)", .repeatable = true },
            },
        },
        CommandSpec {
            .command = "tr",
            .description = "Translate or delete characters",
            .globalOptions = {
                OptionDef { .longName = "--delete", .shortName = "-d", .description = "Delete characters in SET1" },
                OptionDef { .longName = "--squeeze-repeats", .shortName = "-s", .description = "Squeeze repeated characters" },
                OptionDef { .longName = "--help", .description = "Show help" },
            },
        },
        CommandSpec {
            .command = "tee",
            .description = "Read stdin, write to stdout and files",
            .globalOptions = {
                OptionDef { .longName = "--append", .shortName = "-a", .description = "Append to files" },
                OptionDef { .longName = "--help", .shortName = "-h", .description = "Show help" },
            },
            .positionalArgs = {
                ArgDef { .kind = ArgKind::Path, .description = "File(s) to write to", .repeatable = true },
            },
        },
    };
}

} // namespace endo
