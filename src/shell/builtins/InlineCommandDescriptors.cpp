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

static constexpr InlineOptionDef EchoOptions[] = {
    { .shortFlag = "-n", .longFlag = {},    .description = "Do not output the trailing newline" },
    { .shortFlag = "-e", .longFlag = {},    .description = "Enable interpretation of backslash escapes" },
};

static constexpr InlineOptionDef RmOptions[] = {
    { .shortFlag = "-r", .longFlag = "--recursive", .description = "Remove directories and their contents recursively" },
    { .shortFlag = "-f", .longFlag = "--force",     .description = "Ignore nonexistent files, never prompt" },
    { .shortFlag = "-i", .longFlag = {},            .description = "Prompt before every removal" },
    { .shortFlag = "-d", .longFlag = "--dir",       .description = "Remove empty directories" },
    { .shortFlag = "-v", .longFlag = "--verbose",   .description = "Explain what is being done" },
};

static constexpr InlineOptionDef MkdirOptions[] = {
    { .shortFlag = "-p", .longFlag = "--parents", .description = "Create parent directories as needed" },
    { .shortFlag = "-v", .longFlag = "--verbose", .description = "Print a message for each created directory" },
};

static constexpr InlineOptionDef CpOptions[] = {
    { .shortFlag = "-r", .longFlag = "--recursive",  .description = "Copy directories recursively" },
    { .shortFlag = "-f", .longFlag = "--force",      .description = "Force overwrite" },
    { .shortFlag = "-n", .longFlag = "--no-clobber", .description = "Do not overwrite existing files" },
    { .shortFlag = "-v", .longFlag = "--verbose",    .description = "Explain what is being done" },
};

static constexpr InlineOptionDef MvOptions[] = {
    { .shortFlag = "-f", .longFlag = "--force",       .description = "Do not prompt before overwriting" },
    { .shortFlag = "-n", .longFlag = "--no-clobber",  .description = "Do not overwrite existing files" },
    { .shortFlag = "-v", .longFlag = "--verbose",     .description = "Explain what is being done" },
    { .shortFlag = "-i", .longFlag = "--interactive", .description = "Prompt before overwrite" },
};

static constexpr InlineOptionDef CalOptions[] = {
    { .shortFlag = "-3", .longFlag = "--three",    .description = "Show previous, current, and next month" },
    { .shortFlag = "-y", .longFlag = "--year",     .description = "Show the entire year" },
    { .shortFlag = "-m", .longFlag = "--monday",   .description = "Start the week on Monday (ISO 8601)" },
    { .shortFlag = "-s", .longFlag = "--sunday",   .description = "Start the week on Sunday" },
    { .shortFlag = "-n", .longFlag = "--no-color", .description = "Disable colorized output even on a terminal" },
};

static constexpr InlineOptionDef DateOptions[] = {
    { .shortFlag = "-u", .longFlag = "--utc",    .description = "Use UTC instead of local time" },
    { .shortFlag = {},   .longFlag = "--epoch",  .description = "Print seconds since Unix epoch" },
    { .shortFlag = {},   .longFlag = "--iso",    .description = "Print in ISO 8601 format" },
    { .shortFlag = "-f", .longFlag = "--format", .description = "Use custom format (strftime)", .takesValue = true },
    { .shortFlag = "-d", .longFlag = "--date",   .description = "Display given date instead of now", .takesValue = true },
};

static constexpr InlineOptionDef UnameOptions[] = {
    { .shortFlag = "-s", .longFlag = {}, .description = "Print kernel name" },
    { .shortFlag = "-n", .longFlag = {}, .description = "Print network node hostname" },
    { .shortFlag = "-r", .longFlag = {}, .description = "Print kernel release" },
    { .shortFlag = "-m", .longFlag = {}, .description = "Print machine hardware name" },
    { .shortFlag = "-a", .longFlag = {}, .description = "Print all information" },
};

static constexpr InlineOptionDef NprocOptions[] = {
    { .shortFlag = {}, .longFlag = "--all",    .description = "Print the number of installed processors" },
    { .shortFlag = {}, .longFlag = "--ignore", .description = "Exclude N processing units", .takesValue = true },
};

static constexpr InlineOptionDef TouchOptions[] = {
    { .shortFlag = "-c", .longFlag = "--no-create", .description = "Do not create files" },
};

static constexpr InlineOptionDef LnOptions[] = {
    { .shortFlag = "-s", .longFlag = {}, .description = "Create symbolic link" },
    { .shortFlag = "-f", .longFlag = {}, .description = "Remove existing destination (incl. dangling symlink)" },
    { .shortFlag = "-n", .longFlag = "--no-dereference",
      .description = "Treat a symlink at the destination as a normal file" },
    { .shortFlag = "-t", .longFlag = "--target-directory",
      .description = "Create all links inside the given directory", .takesValue = true },
    { .shortFlag = "-v", .longFlag = {}, .description = "Explain what is being done" },
};

static constexpr InlineOptionDef MktempOptions[] = {
    { .shortFlag = "-d", .longFlag = {}, .description = "Create a directory instead of a file" },
    { .shortFlag = "-p", .longFlag = {}, .description = "Use DIR as the base directory", .takesValue = true },
};

static constexpr InlineOptionDef HeadOptions[] = {
    { .shortFlag = "-n", .longFlag = {}, .description = "Number of lines (default: 10)", .takesValue = true },
};

static constexpr InlineOptionDef TailOptions[] = {
    { .shortFlag = "-n", .longFlag = {}, .description = "Number of lines (default: 10)", .takesValue = true },
    { .shortFlag = "-f", .longFlag = {}, .description = "Follow: output appended data as file grows" },
};

static constexpr InlineOptionDef WcOptions[] = {
    { .shortFlag = "-l", .longFlag = {}, .description = "Print line count" },
    { .shortFlag = "-w", .longFlag = {}, .description = "Print word count" },
    { .shortFlag = "-c", .longFlag = {}, .description = "Print character count" },
};

static constexpr InlineOptionDef SortOptions[] = {
    { .shortFlag = "-r", .longFlag = {}, .description = "Reverse sort order" },
    { .shortFlag = "-n", .longFlag = {}, .description = "Compare according to numerical value" },
    { .shortFlag = "-u", .longFlag = {}, .description = "Output only unique lines" },
    { .shortFlag = "-k", .longFlag = {}, .description = "Sort by key field number", .takesValue = true },
};

static constexpr InlineOptionDef UniqOptions[] = {
    { .shortFlag = "-c", .longFlag = {}, .description = "Prefix lines with occurrence count" },
    { .shortFlag = "-d", .longFlag = {}, .description = "Only print duplicate lines" },
    { .shortFlag = "-i", .longFlag = {}, .description = "Ignore case when comparing" },
};

static constexpr InlineOptionDef CutOptions[] = {
    { .shortFlag = "-d", .longFlag = {}, .description = "Field delimiter (default: tab)", .takesValue = true },
    { .shortFlag = "-f", .longFlag = {}, .description = "Select fields (e.g., 1, 1-3, 1,3)", .takesValue = true },
    { .shortFlag = "-c", .longFlag = {}, .description = "Select characters (e.g., 1-5, 3)", .takesValue = true },
};

static constexpr InlineOptionDef TrOptions[] = {
    { .shortFlag = "-d", .longFlag = {}, .description = "Delete characters in SET1" },
    { .shortFlag = "-s", .longFlag = {}, .description = "Squeeze repeated output characters" },
};

static constexpr InlineOptionDef TeeOptions[] = {
    { .shortFlag = "-a", .longFlag = "--append", .description = "Append to files instead of overwriting" },
};

static constexpr InlineOptionDef PkillOptions[] = {
    { .shortFlag = "-s", .longFlag = {}, .description = "Signal to send (name or number)", .takesValue = true },
    { .shortFlag = "-f", .longFlag = {}, .description = "Match against the full command line" },
    { .shortFlag = "-x", .longFlag = {}, .description = "Require exact (anchored) match" },
    { .shortFlag = "-i", .longFlag = {}, .description = "Case-insensitive match" },
    { .shortFlag = "-c", .longFlag = {}, .description = "Print count of matched processes" },
    { .shortFlag = "-l", .longFlag = {}, .description = "List matched processes without signalling" },
    { .shortFlag = "-n", .longFlag = {}, .description = "Match only the newest process" },
    { .shortFlag = "-o", .longFlag = {}, .description = "Match only the oldest process" },
    { .shortFlag = "-u", .longFlag = {}, .description = "Restrict to processes owned by USER[,USER]", .takesValue = true },
};

static constexpr InlineOptionDef PgrepOptions[] = {
    { .shortFlag = "-f", .longFlag = {}, .description = "Match against the full command line" },
    { .shortFlag = "-x", .longFlag = {}, .description = "Require exact (anchored) match" },
    { .shortFlag = "-i", .longFlag = {}, .description = "Case-insensitive match" },
    { .shortFlag = "-v", .longFlag = {}, .description = "Invert the match (select non-matching processes)" },
    { .shortFlag = "-c", .longFlag = {}, .description = "Print count of matched processes" },
    { .shortFlag = "-l", .longFlag = {}, .description = "Print process name along with the PID" },
    { .shortFlag = "-n", .longFlag = {}, .description = "Match only the newest process" },
    { .shortFlag = "-o", .longFlag = {}, .description = "Match only the oldest process" },
    { .shortFlag = "-u", .longFlag = {}, .description = "Restrict to processes owned by USER[,USER]", .takesValue = true },
    { .shortFlag = "-d", .longFlag = {}, .description = "Delimiter between printed PIDs", .takesValue = true },
};

static constexpr InlineOptionDef PidofOptions[] = {
    { .shortFlag = "-s", .longFlag = {}, .description = "Single shot: print at most one PID" },
    { .shortFlag = "-q", .longFlag = {}, .description = "Quiet: no output, exit status only" },
    { .shortFlag = "-S", .longFlag = "--separator", .description = "Separator between printed PIDs", .takesValue = true },
    { .shortFlag = "-d", .longFlag = {}, .description = "Separator between printed PIDs (sysvinit alias)", .takesValue = true },
    { .shortFlag = "-o", .longFlag = {}, .description = "Omit PID[,PID] from the result", .takesValue = true },
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
        { .name = "cal",       .briefDescription = "Display a colorful calendar for a month or year.",
          .usageLine = "cal [OPTIONS] [[MONTH] YEAR]",
          .options = CalOptions,
          .noStdinFn = &Shell::executeInlineCal },
        { .name = "cat",       .briefDescription = "Concatenate and display files.",
          .usageLine = "cat [OPTIONS] [FILE...]",
          .options = {}, .acceptsFileArgs = true, .fileArgsRepeatable = true,
          .withStdinFn = &Shell::executeInlineCat },
        { .name = "cp",        .briefDescription = "Copy files and directories.",
          .usageLine = "cp [OPTIONS] SOURCE... DEST",
          .options = CpOptions, .acceptsFileArgs = true, .fileArgsRepeatable = true,
          .noStdinFn = &Shell::executeInlineCp },
        { .name = "cut",       .briefDescription = "Extract fields or characters from lines.",
          .usageLine = "cut [OPTIONS] [FILE...]",
          .options = CutOptions, .acceptsFileArgs = true, .fileArgsRepeatable = true,
          .withStdinFn = &Shell::executeInlineCut },
        { .name = "date",      .briefDescription = "Print or format the current date and time.",
          .usageLine = "date [OPTIONS]",
          .options = DateOptions,
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
          .options = EchoOptions,
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
          .options = HeadOptions, .acceptsFileArgs = true, .fileArgsRepeatable = true,
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
          .usageLine = "ln [OPTIONS] TARGET [LINK_NAME] | ln [OPTIONS] TARGET... DIRECTORY",
          .options = LnOptions, .acceptsFileArgs = true, .fileArgsRepeatable = true,
          .noStdinFn = &Shell::executeInlineLn },
        { .name = "mkdir",     .briefDescription = "Create directories.",
          .usageLine = "mkdir [OPTIONS] DIRECTORY...",
          .options = MkdirOptions, .acceptsFileArgs = true, .fileArgsRepeatable = true,
          .noStdinFn = &Shell::executeInlineMkdir },
        { .name = "mktemp",    .briefDescription = "Create a temporary file or directory.",
          .usageLine = "mktemp [OPTIONS]",
          .options = MktempOptions,
          .noStdinFn = &Shell::executeInlineMktemp },
        { .name = "mv",        .briefDescription = "Move or rename files and directories.",
          .usageLine = "mv [OPTIONS] SOURCE... DEST",
          .options = MvOptions, .acceptsFileArgs = true, .fileArgsRepeatable = true,
          .noStdinFn = &Shell::executeInlineMv },
        { .name = "nproc",    .briefDescription = "Print the number of available processing units.",
          .usageLine = "nproc [OPTIONS]",
          .options = NprocOptions,
          .noStdinFn = &Shell::executeInlineNproc },
        { .name = "pgrep",     .briefDescription = "Print PIDs of processes matched by name or command-line pattern.",
          .usageLine = "pgrep [OPTIONS] PATTERN",
          .options = PgrepOptions,
          .positionalQuery = { .queryTag = "process-names", .description = "Process name pattern",
                               .overrideFlag = "-f", .overrideQueryTag = "process-command-lines" },
          .noStdinFn = &Shell::executeInlinePgrep },
        { .name = "pidof",     .briefDescription = "Print PIDs of running processes matching program names.",
          .usageLine = "pidof [OPTIONS] PROGRAM...",
          .options = PidofOptions,
          .positionalQuery = { .queryTag = "process-names", .description = "Program name", .repeatable = true },
          .noStdinFn = &Shell::executeInlinePidof },
        { .name = "pkill",     .briefDescription = "Send signals to processes matched by name or command-line pattern.",
          .usageLine = "pkill [OPTIONS] [-SIGNAL] PATTERN",
          .options = PkillOptions,
          .positionalQuery = { .queryTag = "process-names", .description = "Process name pattern",
                               .overrideFlag = "-f", .overrideQueryTag = "process-command-lines" },
          .noStdinFn = &Shell::executeInlinePkill },
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
          .options = RmOptions, .acceptsFileArgs = true, .fileArgsRepeatable = true,
          .noStdinFn = &Shell::executeInlineRm },
        { .name = "sleep",     .briefDescription = "Pause execution for a specified duration.",
          .usageLine = "sleep DURATION",
          .noStdinFn = &Shell::executeInlineSleep },
        { .name = "sort",      .briefDescription = "Sort lines of text.",
          .usageLine = "sort [OPTIONS] [FILE...]",
          .options = SortOptions, .acceptsFileArgs = true, .fileArgsRepeatable = true,
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
          .options = TailOptions, .acceptsFileArgs = true, .fileArgsRepeatable = true,
          .withStdinFn = &Shell::executeInlineTail },
        { .name = "tee",       .briefDescription = "Read stdin, write to stdout and files.",
          .usageLine = "tee [OPTIONS] [FILE...]",
          .options = TeeOptions, .acceptsFileArgs = true, .fileArgsRepeatable = true,
          .withStdinFn = &Shell::executeInlineTee },
        { .name = "timeout",   .briefDescription = "Run a command with a time limit.",
          .usageLine = "timeout [OPTIONS] DURATION COMMAND [ARG...]",
          .noStdinFn = &Shell::executeInlineTimeout },
        { .name = "touch",     .briefDescription = "Create files or update timestamps.",
          .usageLine = "touch [OPTIONS] FILE...",
          .options = TouchOptions, .acceptsFileArgs = true, .fileArgsRepeatable = true,
          .noStdinFn = &Shell::executeInlineTouch },
        { .name = "tr",        .briefDescription = "Translate or delete characters.",
          .usageLine = "tr [OPTIONS] SET1 [SET2]",
          .options = TrOptions,
          .withStdinFn = &Shell::executeInlineTr },
        { .name = "uname",     .briefDescription = "Print system information.",
          .usageLine = "uname [OPTIONS]",
          .options = UnameOptions,
          .noStdinFn = &Shell::executeInlineUname },
        { .name = "uniq",      .briefDescription = "Filter adjacent duplicate lines.",
          .usageLine = "uniq [OPTIONS] [FILE]",
          .options = UniqOptions, .acceptsFileArgs = true, .fileArgsRepeatable = true,
          .withStdinFn = &Shell::executeInlineUniq },
        { .name = "wc",        .briefDescription = "Count lines, words, and characters.",
          .usageLine = "wc [OPTIONS] [FILE...]",
          .options = WcOptions, .acceptsFileArgs = true, .fileArgsRepeatable = true,
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
