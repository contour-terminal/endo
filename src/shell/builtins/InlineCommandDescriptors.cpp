// SPDX-License-Identifier: Apache-2.0
#include <shell/Shell.hpp>
#include <shell/builtins/InlineCommandDescriptor.hpp>

#include <algorithm>
#include <array>

namespace endo
{

// ---------------------------------------------------------------------------
// Option definitions (file-scope — these don't reference Shell members)
// ---------------------------------------------------------------------------

// clang-format off

static constexpr InlineOptionDef kEchoOptions[] = {
    { .shortFlag = "-n", .longFlag = {},    .description = "Do not output the trailing newline" },
    { .shortFlag = "-e", .longFlag = {},    .description = "Enable interpretation of backslash escapes" },
};

static constexpr InlineOptionDef kRmOptions[] = {
    { .shortFlag = "-r", .longFlag = "--recursive", .description = "Remove directories and their contents recursively" },
    { .shortFlag = "-f", .longFlag = "--force",     .description = "Ignore nonexistent files, never prompt" },
    { .shortFlag = "-i", .longFlag = {},            .description = "Prompt before every removal" },
    { .shortFlag = "-d", .longFlag = "--dir",       .description = "Remove empty directories" },
    { .shortFlag = "-v", .longFlag = "--verbose",   .description = "Explain what is being done" },
};

static constexpr InlineOptionDef kMkdirOptions[] = {
    { .shortFlag = "-p", .longFlag = "--parents", .description = "Create parent directories as needed" },
    { .shortFlag = "-v", .longFlag = "--verbose", .description = "Print a message for each created directory" },
};

static constexpr InlineOptionDef kCpOptions[] = {
    { .shortFlag = "-r", .longFlag = "--recursive",  .description = "Copy directories recursively" },
    { .shortFlag = "-f", .longFlag = "--force",      .description = "Force overwrite" },
    { .shortFlag = "-n", .longFlag = "--no-clobber", .description = "Do not overwrite existing files" },
    { .shortFlag = "-v", .longFlag = "--verbose",    .description = "Explain what is being done" },
};

static constexpr InlineOptionDef kMvOptions[] = {
    { .shortFlag = "-f", .longFlag = "--force",       .description = "Do not prompt before overwriting" },
    { .shortFlag = "-n", .longFlag = "--no-clobber",  .description = "Do not overwrite existing files" },
    { .shortFlag = "-v", .longFlag = "--verbose",     .description = "Explain what is being done" },
    { .shortFlag = "-i", .longFlag = "--interactive", .description = "Prompt before overwrite" },
};

static constexpr InlineOptionDef kDateOptions[] = {
    { .shortFlag = "-u", .longFlag = "--utc",    .description = "Use UTC instead of local time" },
    { .shortFlag = {},   .longFlag = "--epoch",  .description = "Print seconds since Unix epoch" },
    { .shortFlag = {},   .longFlag = "--iso",    .description = "Print in ISO 8601 format" },
    { .shortFlag = "-f", .longFlag = "--format", .description = "Use custom format (strftime)", .takesValue = true },
    { .shortFlag = "-d", .longFlag = "--date",   .description = "Display given date instead of now", .takesValue = true },
};

static constexpr InlineOptionDef kUnameOptions[] = {
    { .shortFlag = "-s", .longFlag = {}, .description = "Print kernel name" },
    { .shortFlag = "-n", .longFlag = {}, .description = "Print network node hostname" },
    { .shortFlag = "-r", .longFlag = {}, .description = "Print kernel release" },
    { .shortFlag = "-m", .longFlag = {}, .description = "Print machine hardware name" },
    { .shortFlag = "-a", .longFlag = {}, .description = "Print all information" },
};

static constexpr InlineOptionDef kNprocOptions[] = {
    { .shortFlag = {}, .longFlag = "--all",    .description = "Print the number of installed processors" },
    { .shortFlag = {}, .longFlag = "--ignore", .description = "Exclude N processing units", .takesValue = true },
};

static constexpr InlineOptionDef kTouchOptions[] = {
    { .shortFlag = "-c", .longFlag = "--no-create", .description = "Do not create files" },
};

static constexpr InlineOptionDef kLnOptions[] = {
    { .shortFlag = "-s", .longFlag = {}, .description = "Create symbolic link" },
    { .shortFlag = "-f", .longFlag = {}, .description = "Remove existing destination files" },
    { .shortFlag = "-v", .longFlag = {}, .description = "Explain what is being done" },
};

static constexpr InlineOptionDef kMktempOptions[] = {
    { .shortFlag = "-d", .longFlag = {}, .description = "Create a directory instead of a file" },
    { .shortFlag = "-p", .longFlag = {}, .description = "Use DIR as the base directory", .takesValue = true },
};

static constexpr InlineOptionDef kHeadOptions[] = {
    { .shortFlag = "-n", .longFlag = {}, .description = "Number of lines (default: 10)", .takesValue = true },
};

static constexpr InlineOptionDef kTailOptions[] = {
    { .shortFlag = "-n", .longFlag = {}, .description = "Number of lines (default: 10)", .takesValue = true },
    { .shortFlag = "-f", .longFlag = {}, .description = "Follow: output appended data as file grows" },
};

static constexpr InlineOptionDef kWcOptions[] = {
    { .shortFlag = "-l", .longFlag = {}, .description = "Print line count" },
    { .shortFlag = "-w", .longFlag = {}, .description = "Print word count" },
    { .shortFlag = "-c", .longFlag = {}, .description = "Print character count" },
};

static constexpr InlineOptionDef kSortOptions[] = {
    { .shortFlag = "-r", .longFlag = {}, .description = "Reverse sort order" },
    { .shortFlag = "-n", .longFlag = {}, .description = "Compare according to numerical value" },
    { .shortFlag = "-u", .longFlag = {}, .description = "Output only unique lines" },
    { .shortFlag = "-k", .longFlag = {}, .description = "Sort by key field number", .takesValue = true },
};

static constexpr InlineOptionDef kUniqOptions[] = {
    { .shortFlag = "-c", .longFlag = {}, .description = "Prefix lines with occurrence count" },
    { .shortFlag = "-d", .longFlag = {}, .description = "Only print duplicate lines" },
    { .shortFlag = "-i", .longFlag = {}, .description = "Ignore case when comparing" },
};

static constexpr InlineOptionDef kCutOptions[] = {
    { .shortFlag = "-d", .longFlag = {}, .description = "Field delimiter (default: tab)", .takesValue = true },
    { .shortFlag = "-f", .longFlag = {}, .description = "Select fields (e.g., 1, 1-3, 1,3)", .takesValue = true },
    { .shortFlag = "-c", .longFlag = {}, .description = "Select characters (e.g., 1-5, 3)", .takesValue = true },
};

static constexpr InlineOptionDef kTrOptions[] = {
    { .shortFlag = "-d", .longFlag = {}, .description = "Delete characters in SET1" },
    { .shortFlag = "-s", .longFlag = {}, .description = "Squeeze repeated output characters" },
};

static constexpr InlineOptionDef kTeeOptions[] = {
    { .shortFlag = "-a", .longFlag = "--append", .description = "Append to files instead of overwriting" },
};

// clang-format on

// ---------------------------------------------------------------------------
// The master descriptor table — defined inside Shell member function
// to access private member function pointers.
// ---------------------------------------------------------------------------

std::span<InlineCommandDescriptor const> Shell::inlineCommandDescriptors()
{
    // clang-format off
    /// Alphabetically sorted table of all inline builtins.
    /// Adding a new builtin: insert a single entry here (keep sort order!)
    /// + implement the executeInlineXxx function.
    static const InlineCommandDescriptor table[] = {
        { .name = ".",         .briefDescription = "Execute a script in the current shell context.",
          .usageLine = ". FILE [ARGS...]",
          .options = {}, .acceptsFileArgs = true,
          .noStdinFn = &Shell::executeInlineSource },
        { .name = "basename",  .briefDescription = "Strip directory and suffix from pathname.",
          .usageLine = "basename PATH [SUFFIX]",
          .options = {}, .acceptsFileArgs = true, .fileArgsRepeatable = false,
          .noStdinFn = &Shell::executeInlineBasename },
        { .name = "cat",       .briefDescription = "Concatenate and display files.",
          .usageLine = "cat [OPTIONS] [FILE...]",
          .options = {}, .acceptsFileArgs = true, .fileArgsRepeatable = true,
          .withStdinFn = &Shell::executeInlineCat },
        { .name = "cp",        .briefDescription = "Copy files and directories.",
          .usageLine = "cp [OPTIONS] SOURCE... DEST",
          .options = kCpOptions, .acceptsFileArgs = true, .fileArgsRepeatable = true,
          .noStdinFn = &Shell::executeInlineCp },
        { .name = "cut",       .briefDescription = "Extract fields or characters from lines.",
          .usageLine = "cut [OPTIONS] [FILE...]",
          .options = kCutOptions, .acceptsFileArgs = true, .fileArgsRepeatable = true,
          .withStdinFn = &Shell::executeInlineCut },
        { .name = "date",      .briefDescription = "Print or format the current date and time.",
          .usageLine = "date [OPTIONS]",
          .options = kDateOptions,
          .noStdinFn = &Shell::executeInlineDate },
        { .name = "dirconfig", .briefDescription = "Manage per-directory configuration.",
          .usageLine = "dirconfig SUBCOMMAND [PATH]",
          .noStdinFn = &Shell::executeInlineDirConfig },
        { .name = "dirname",   .briefDescription = "Strip last component from pathname.",
          .usageLine = "dirname PATH",
          .options = {}, .acceptsFileArgs = true, .fileArgsRepeatable = false,
          .noStdinFn = &Shell::executeInlineDirname },
        { .name = "echo",      .briefDescription = "Write arguments to standard output.",
          .usageLine = "echo [OPTIONS] [STRING...]",
          .options = kEchoOptions,
          .noStdinFn = &Shell::executeInlineEcho },
        { .name = "find",      .briefDescription = "Search for files in directory hierarchies.",
          .usageLine = "find [PATH...] [EXPRESSION]",
          .options = {}, .acceptsFileArgs = true, .fileArgsRepeatable = true,
          .noStdinFn = &Shell::executeInlineFind },
        { .name = "grep",      .briefDescription = "Search for patterns in files.",
          .usageLine = "grep [OPTIONS] PATTERN [FILE...]",
          .options = {}, .acceptsFileArgs = true, .fileArgsRepeatable = true,
          .withStdinFn = &Shell::executeInlineGrep },
        { .name = "head",      .briefDescription = "Output the first lines of files.",
          .usageLine = "head [OPTIONS] [FILE...]",
          .options = kHeadOptions, .acceptsFileArgs = true, .fileArgsRepeatable = true,
          .withStdinFn = &Shell::executeInlineHead },
        { .name = "history",   .briefDescription = "Display or manage command history.",
          .usageLine = "history [N | search PATTERN | clear]",
          .noStdinFn = &Shell::executeInlineHistory },
        { .name = "hostname",  .briefDescription = "Print the system hostname.",
          .usageLine = "hostname",
          .noStdinFn = &Shell::executeInlineHostname },
        { .name = "kill",      .briefDescription = "Send signals to processes or jobs.",
          .usageLine = "kill [-SIGNAL] PID|%JOB ...",
          .noStdinFn = &Shell::executeInlineKill },
        { .name = "ln",        .briefDescription = "Create hard or symbolic links.",
          .usageLine = "ln [OPTIONS] TARGET LINK_NAME",
          .options = kLnOptions, .acceptsFileArgs = true, .fileArgsRepeatable = true,
          .noStdinFn = &Shell::executeInlineLn },
        { .name = "mkdir",     .briefDescription = "Create directories.",
          .usageLine = "mkdir [OPTIONS] DIRECTORY...",
          .options = kMkdirOptions, .acceptsFileArgs = true, .fileArgsRepeatable = true,
          .noStdinFn = &Shell::executeInlineMkdir },
        { .name = "mktemp",    .briefDescription = "Create a temporary file or directory.",
          .usageLine = "mktemp [OPTIONS]",
          .options = kMktempOptions,
          .noStdinFn = &Shell::executeInlineMktemp },
        { .name = "mv",        .briefDescription = "Move or rename files and directories.",
          .usageLine = "mv [OPTIONS] SOURCE... DEST",
          .options = kMvOptions, .acceptsFileArgs = true, .fileArgsRepeatable = true,
          .noStdinFn = &Shell::executeInlineMv },
        { .name = "nproc",    .briefDescription = "Print the number of available processing units.",
          .usageLine = "nproc [OPTIONS]",
          .options = kNprocOptions,
          .noStdinFn = &Shell::executeInlineNproc },
        { .name = "pwd",      .briefDescription = "Print the current working directory.",
          .usageLine = "pwd",
          .options = {},
          .noStdinFn = &Shell::executeInlinePwd },
        { .name = "realpath",  .briefDescription = "Resolve path to absolute canonical form.",
          .usageLine = "realpath PATH...",
          .options = {}, .acceptsFileArgs = true, .fileArgsRepeatable = true,
          .noStdinFn = &Shell::executeInlineRealpath },
        { .name = "rm",        .briefDescription = "Remove files and directories.",
          .usageLine = "rm [OPTIONS] FILE...",
          .options = kRmOptions, .acceptsFileArgs = true, .fileArgsRepeatable = true,
          .noStdinFn = &Shell::executeInlineRm },
        { .name = "sleep",     .briefDescription = "Pause execution for a specified duration.",
          .usageLine = "sleep DURATION",
          .noStdinFn = &Shell::executeInlineSleep },
        { .name = "sort",      .briefDescription = "Sort lines of text.",
          .usageLine = "sort [OPTIONS] [FILE...]",
          .options = kSortOptions, .acceptsFileArgs = true, .fileArgsRepeatable = true,
          .withStdinFn = &Shell::executeInlineSort },
        { .name = "source",    .briefDescription = "Execute a script in the current shell context.",
          .usageLine = "source FILE [ARGS...]",
          .options = {}, .acceptsFileArgs = true,
          .noStdinFn = &Shell::executeInlineSource },
        { .name = "source-env", .briefDescription = "Import environment from an external script.",
          .usageLine = "source-env SCRIPT [ARGS...]",
          .options = {}, .acceptsFileArgs = true, .fileArgsRepeatable = false,
          .noStdinFn = &Shell::executeInlineSourceEnv },
        { .name = "tail",      .briefDescription = "Output the last lines of files.",
          .usageLine = "tail [OPTIONS] [FILE...]",
          .options = kTailOptions, .acceptsFileArgs = true, .fileArgsRepeatable = true,
          .withStdinFn = &Shell::executeInlineTail },
        { .name = "tee",       .briefDescription = "Read stdin, write to stdout and files.",
          .usageLine = "tee [OPTIONS] [FILE...]",
          .options = kTeeOptions, .acceptsFileArgs = true, .fileArgsRepeatable = true,
          .withStdinFn = &Shell::executeInlineTee },
        { .name = "timeout",   .briefDescription = "Run a command with a time limit.",
          .usageLine = "timeout [OPTIONS] DURATION COMMAND [ARG...]",
          .noStdinFn = &Shell::executeInlineTimeout },
        { .name = "touch",     .briefDescription = "Create files or update timestamps.",
          .usageLine = "touch [OPTIONS] FILE...",
          .options = kTouchOptions, .acceptsFileArgs = true, .fileArgsRepeatable = true,
          .noStdinFn = &Shell::executeInlineTouch },
        { .name = "tr",        .briefDescription = "Translate or delete characters.",
          .usageLine = "tr [OPTIONS] SET1 [SET2]",
          .options = kTrOptions,
          .withStdinFn = &Shell::executeInlineTr },
        { .name = "uname",     .briefDescription = "Print system information.",
          .usageLine = "uname [OPTIONS]",
          .options = kUnameOptions,
          .noStdinFn = &Shell::executeInlineUname },
        { .name = "uniq",      .briefDescription = "Filter adjacent duplicate lines.",
          .usageLine = "uniq [OPTIONS] [FILE]",
          .options = kUniqOptions, .acceptsFileArgs = true, .fileArgsRepeatable = true,
          .withStdinFn = &Shell::executeInlineUniq },
        { .name = "wc",        .briefDescription = "Count lines, words, and characters.",
          .usageLine = "wc [OPTIONS] [FILE...]",
          .options = kWcOptions, .acceptsFileArgs = true, .fileArgsRepeatable = true,
          .withStdinFn = &Shell::executeInlineWc },
        { .name = "whoami",    .briefDescription = "Print the current username.",
          .usageLine = "whoami",
          .noStdinFn = &Shell::executeInlineWhoami },
    };
    // clang-format on

    return table;
}

int InlineCommandDescriptor::execute(Shell& shell,
                                     CoreVM::CoreStringArray const& args,
                                     NativeHandle outputFd,
                                     NativeHandle stdinFd) const
{
    if (withStdinFn)
        return (shell.*withStdinFn)(args, outputFd, stdinFd);
    return (shell.*noStdinFn)(args, outputFd);
}

InlineCommandDescriptor const* Shell::findInlineBuiltin(std::string_view name)
{
    auto const table = inlineCommandDescriptors();
    auto const it = std::ranges::lower_bound(table, name, {}, &InlineCommandDescriptor::name);
    if (it == table.end() || it->name != name)
        return nullptr;
    return &*it;
}

} // namespace endo
