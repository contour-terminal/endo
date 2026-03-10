// SPDX-License-Identifier: Apache-2.0
#include "GitSpec.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <set>

#if defined(_WIN32)
    #define popen  _popen
    #define pclose _pclose
#endif

namespace endo
{

namespace
{

    // ========================================================================
    // Helper: common option definitions reused across subcommands
    // ========================================================================

    [[nodiscard]] auto verboseOpt() -> OptionDef
    {
        return { .longName = "--verbose", .shortName = "-v", .description = "Be more verbose" };
    }

    [[nodiscard]] auto quietOpt() -> OptionDef
    {
        return { .longName = "--quiet", .shortName = "-q", .description = "Be more quiet" };
    }

    [[nodiscard]] auto forceOpt() -> OptionDef
    {
        return { .longName = "--force", .shortName = "-f", .description = "Force operation" };
    }

    [[nodiscard]] auto dryRunOpt() -> OptionDef
    {
        return { .longName = "--dry-run", .shortName = "-n", .description = "Dry run" };
    }

    // ========================================================================
    // Tier 1: Full subcommand definitions (20 most common)
    // ========================================================================

    [[nodiscard]] auto addSubcommand() -> SubcommandDef
    {
        return {
            .name = "add",
            .description = "Add file contents to the index",
            .options = {
                dryRunOpt(),
                verboseOpt(),
                forceOpt(),
                { .longName = "--interactive", .shortName = "-i", .description = "Interactive picking" },
                { .longName = "--patch", .shortName = "-p", .description = "Interactively choose hunks" },
                { .longName = "--edit", .shortName = "-e", .description = "Edit diff before staging" },
                { .longName = "--all", .shortName = "-A", .description = "Add all changes" },
                { .longName = "--update", .shortName = "-u", .description = "Update tracked files" },
                { .longName = "--intent-to-add", .shortName = "-N", .description = "Record intent to add" },
            },
            .positionalArgs = {
                { .kind = ArgKind::DynamicQuery, .description = "Files to add", .queryTag = "status-files", .repeatable = true },
            },
        };
    }

    [[nodiscard]] auto bisectSubcommand() -> SubcommandDef
    {
        return {
            .name = "bisect",
            .description = "Use binary search to find the commit that introduced a bug",
            .subcommands = {
                { .name = "start", .description = "Start bisection" },
                { .name = "bad", .description = "Mark current or given revision as bad",
                  .positionalArgs = {{ .kind = ArgKind::DynamicQuery, .description = "Commit", .queryTag = "recent-commits" }} },
                { .name = "good", .description = "Mark current or given revision as good",
                  .positionalArgs = {{ .kind = ArgKind::DynamicQuery, .description = "Commit", .queryTag = "recent-commits" }} },
                { .name = "reset", .description = "Finish bisection" },
                { .name = "skip", .description = "Skip current revision" },
                { .name = "log", .description = "Show bisect log" },
                { .name = "replay", .description = "Replay bisect log" },
                { .name = "run", .description = "Run bisect with a script" },
            },
        };
    }

    [[nodiscard]] auto branchSubcommand() -> SubcommandDef
    {
        return {
            .name = "branch",
            .description = "List, create, or delete branches",
            .options = {
                { .longName = "--delete", .shortName = "-d", .description = "Delete branch" },
                { .longName = "", .shortName = "-D", .description = "Force delete branch" },
                { .longName = "--move", .shortName = "-m", .description = "Rename branch" },
                { .longName = "", .shortName = "-M", .description = "Force rename branch" },
                { .longName = "--copy", .shortName = "-c", .description = "Copy branch" },
                { .longName = "--list", .shortName = "-l", .description = "List branches" },
                { .longName = "--all", .shortName = "-a", .description = "List local and remote branches" },
                { .longName = "--remotes", .shortName = "-r", .description = "List remote branches" },
                verboseOpt(),
                forceOpt(),
                { .longName = "--set-upstream-to", .description = "Set upstream branch",
                  .valueKind = OptionValueKind::DynamicQuery, .queryTag = "branches" },
                { .longName = "--track", .shortName = "-t", .description = "Set up tracking" },
                { .longName = "--no-track", .description = "Do not set up tracking" },
                { .longName = "--merged", .description = "List merged branches" },
                { .longName = "--no-merged", .description = "List unmerged branches" },
            },
            .positionalArgs = {
                { .kind = ArgKind::DynamicQuery,
                  .description = "Branch name",
                  .queryTag = "branches",
                  .optionQueryOverrides = { { "-d", "local-branches" },
                                            { "--delete", "local-branches" },
                                            { "-D", "local-branches" } } },
            },
        };
    }

    [[nodiscard]] auto checkoutSubcommand() -> SubcommandDef
    {
        return {
            .name = "checkout",
            .description = "Switch branches or restore working tree files",
            .options = {
                { .longName = "--branch", .shortName = "-b", .description = "Create and checkout new branch",
                  .valueKind = OptionValueKind::String },
                { .longName = "", .shortName = "-B", .description = "Create/reset and checkout branch",
                  .valueKind = OptionValueKind::String },
                forceOpt(),
                { .longName = "--track", .shortName = "-t", .description = "Set up tracking" },
                { .longName = "--no-track", .description = "Do not set up tracking" },
                { .longName = "--detach", .description = "Detach HEAD" },
                { .longName = "--orphan", .description = "Create orphan branch",
                  .valueKind = OptionValueKind::String },
                { .longName = "--ours", .description = "Checkout our version for conflicts" },
                { .longName = "--theirs", .description = "Checkout their version for conflicts" },
                { .longName = "--merge", .shortName = "-m", .description = "Merge local modifications" },
                { .longName = "--patch", .shortName = "-p", .description = "Interactively select hunks" },
            },
            .positionalArgs = {
                { .kind = ArgKind::DynamicQuery, .description = "Branch or path", .queryTag = "branches", .repeatable = true },
            },
        };
    }

    [[nodiscard]] auto cherryPickSubcommand() -> SubcommandDef
    {
        return {
            .name = "cherry-pick",
            .description = "Apply the changes introduced by some existing commits",
            .options = {
                { .longName = "--edit", .shortName = "-e", .description = "Edit commit message" },
                { .longName = "--no-commit", .shortName = "-n", .description = "Apply without committing" },
                { .longName = "--signoff", .shortName = "-s", .description = "Add Signed-off-by" },
                { .longName = "--mainline", .shortName = "-m", .description = "Parent number for merge commits",
                  .valueKind = OptionValueKind::String },
                { .longName = "--abort", .description = "Abort cherry-pick" },
                { .longName = "--continue", .description = "Continue cherry-pick" },
                { .longName = "--skip", .description = "Skip current commit" },
            },
            .positionalArgs = {
                { .kind = ArgKind::DynamicQuery, .description = "Commit", .queryTag = "recent-commits", .repeatable = true },
            },
        };
    }

    [[nodiscard]] auto cloneSubcommand() -> SubcommandDef
    {
        return {
            .name = "clone",
            .description = "Clone a repository into a new directory",
            .options = {
                { .longName = "--branch", .shortName = "-b", .description = "Checkout branch",
                  .valueKind = OptionValueKind::String },
                { .longName = "--depth", .description = "Shallow clone depth",
                  .valueKind = OptionValueKind::String },
                { .longName = "--single-branch", .description = "Clone single branch only" },
                { .longName = "--recurse-submodules", .description = "Initialize submodules" },
                { .longName = "--shallow-submodules", .description = "Shallow clone submodules" },
                { .longName = "--bare", .description = "Create bare repository" },
                { .longName = "--mirror", .description = "Set up mirror" },
                quietOpt(),
                verboseOpt(),
            },
        };
    }

    [[nodiscard]] auto commitSubcommand() -> SubcommandDef
    {
        return {
            .name = "commit",
            .description = "Record changes to the repository",
            .options = {
                { .longName = "--message", .shortName = "-m", .description = "Commit message",
                  .valueKind = OptionValueKind::String },
                { .longName = "--all", .shortName = "-a", .description = "Stage all modified files" },
                { .longName = "--amend", .description = "Amend previous commit" },
                { .longName = "--no-edit", .description = "Use previous commit message" },
                { .longName = "--signoff", .shortName = "-s", .description = "Add Signed-off-by" },
                { .longName = "--gpg-sign", .shortName = "-S", .description = "GPG sign commit" },
                { .longName = "--no-verify", .description = "Skip pre-commit hooks" },
                { .longName = "--allow-empty", .description = "Allow empty commit" },
                { .longName = "--allow-empty-message", .description = "Allow empty message" },
                { .longName = "--fixup", .description = "Fixup commit",
                  .valueKind = OptionValueKind::DynamicQuery, .queryTag = "recent-commits" },
                { .longName = "--squash", .description = "Squash commit",
                  .valueKind = OptionValueKind::DynamicQuery, .queryTag = "recent-commits" },
                verboseOpt(),
                quietOpt(),
                dryRunOpt(),
                { .longName = "--author", .description = "Override author",
                  .valueKind = OptionValueKind::String },
                { .longName = "--date", .description = "Override date",
                  .valueKind = OptionValueKind::String },
                { .longName = "--file", .shortName = "-F", .description = "Read message from file",
                  .valueKind = OptionValueKind::Path },
                { .longName = "--template", .shortName = "-t", .description = "Message template file",
                  .valueKind = OptionValueKind::Path },
            },
            .positionalArgs = {
                { .kind = ArgKind::Path, .description = "Files to commit", .repeatable = true },
            },
        };
    }

    [[nodiscard]] auto configSubcommand() -> SubcommandDef
    {
        return {
            .name = "config",
            .description = "Get and set repository or global options",
            .options = {
                { .longName = "--global", .description = "Use global config" },
                { .longName = "--system", .description = "Use system config" },
                { .longName = "--local", .description = "Use repository config" },
                { .longName = "--get", .description = "Get value" },
                { .longName = "--get-all", .description = "Get all values" },
                { .longName = "--unset", .description = "Unset value" },
                { .longName = "--unset-all", .description = "Unset all values" },
                { .longName = "--list", .shortName = "-l", .description = "List all settings" },
                { .longName = "--edit", .shortName = "-e", .description = "Open config in editor" },
                { .longName = "--type", .description = "Ensure value is of given type",
                  .valueKind = OptionValueKind::Enum,
                  .enumValues = { "bool", "int", "bool-or-int", "path", "expiry-date", "color" } },
            },
            .positionalArgs = {
                { .kind = ArgKind::DynamicQuery, .description = "Config key", .queryTag = "config-keys" },
            },
        };
    }

    [[nodiscard]] auto diffSubcommand() -> SubcommandDef
    {
        return {
            .name = "diff",
            .description = "Show changes between commits, commit and working tree, etc.",
            .options = {
                { .longName = "--cached", .description = "Show staged changes" },
                { .longName = "--staged", .description = "Show staged changes" },
                { .longName = "--stat", .description = "Show diffstat" },
                { .longName = "--shortstat", .description = "Show only summary line" },
                { .longName = "--name-only", .description = "Show only names of changed files" },
                { .longName = "--name-status", .description = "Show names and status of changed files" },
                { .longName = "--no-index", .description = "Compare two paths outside git" },
                { .longName = "--word-diff", .description = "Show word diff" },
                { .longName = "--color-words", .description = "Show colored word diff" },
                { .longName = "--check", .description = "Warn on whitespace errors" },
            },
            .positionalArgs = {
                { .kind = ArgKind::DynamicQuery, .description = "Commit or path", .queryTag = "branches", .repeatable = true },
            },
        };
    }

    [[nodiscard]] auto fetchSubcommand() -> SubcommandDef
    {
        return {
            .name = "fetch",
            .description = "Download objects and refs from another repository",
            .options = {
                { .longName = "--all", .description = "Fetch all remotes" },
                { .longName = "--prune", .shortName = "-p", .description = "Prune stale tracking refs" },
                { .longName = "--tags", .shortName = "-t", .description = "Fetch all tags" },
                { .longName = "--no-tags", .description = "Don't fetch tags" },
                { .longName = "--depth", .description = "Deepen shallow clone",
                  .valueKind = OptionValueKind::String },
                { .longName = "--unshallow", .description = "Convert shallow to full clone" },
                dryRunOpt(),
                forceOpt(),
                verboseOpt(),
                quietOpt(),
            },
            .positionalArgs = {
                { .kind = ArgKind::DynamicQuery, .description = "Remote", .queryTag = "remotes" },
                { .kind = ArgKind::DynamicQuery, .description = "Branch", .queryTag = "branches" },
            },
        };
    }

    [[nodiscard]] auto logSubcommand() -> SubcommandDef
    {
        return {
            .name = "log",
            .description = "Show commit logs",
            .options = {
                { .longName = "--oneline", .description = "One line per commit" },
                { .longName = "--graph", .description = "Show ASCII graph" },
                { .longName = "--all", .description = "Show all branches" },
                { .longName = "--stat", .description = "Show diffstat" },
                { .longName = "--patch", .shortName = "-p", .description = "Show patch" },
                { .longName = "--follow", .description = "Follow file renames" },
                { .longName = "--first-parent", .description = "Follow only first parent" },
                { .longName = "--no-merges", .description = "Skip merge commits" },
                { .longName = "--merges", .description = "Show only merge commits" },
                { .longName = "--author", .description = "Filter by author",
                  .valueKind = OptionValueKind::String },
                { .longName = "--since", .description = "Show commits since date",
                  .valueKind = OptionValueKind::String },
                { .longName = "--until", .description = "Show commits until date",
                  .valueKind = OptionValueKind::String },
                { .longName = "--grep", .description = "Search commit messages",
                  .valueKind = OptionValueKind::String },
                { .longName = "--format", .description = "Pretty format",
                  .valueKind = OptionValueKind::Enum,
                  .enumValues = { "oneline", "short", "medium", "full", "fuller", "email", "raw" } },
                { .longName = "", .shortName = "-n", .description = "Number of commits",
                  .valueKind = OptionValueKind::String },
            },
            .positionalArgs = {
                { .kind = ArgKind::DynamicQuery, .description = "Revision range", .queryTag = "branches", .repeatable = true },
            },
        };
    }

    [[nodiscard]] auto mergeSubcommand() -> SubcommandDef
    {
        return {
            .name = "merge",
            .description = "Join two or more development histories together",
            .options = {
                { .longName = "--no-ff", .description = "Create merge commit even if fast-forward" },
                { .longName = "--ff-only", .description = "Only fast-forward" },
                { .longName = "--squash", .description = "Squash commits" },
                { .longName = "--no-commit", .description = "Merge without committing" },
                { .longName = "--edit", .shortName = "-e", .description = "Edit merge message" },
                { .longName = "--no-edit", .description = "Accept auto-generated message" },
                { .longName = "--abort", .description = "Abort merge" },
                { .longName = "--continue", .description = "Continue merge" },
                { .longName = "--strategy", .shortName = "-s", .description = "Merge strategy",
                  .valueKind = OptionValueKind::Enum,
                  .enumValues = { "ort", "recursive", "resolve", "octopus", "ours", "subtree" } },
                { .longName = "--signoff", .description = "Add Signed-off-by" },
                verboseOpt(),
                quietOpt(),
            },
            .positionalArgs = {
                { .kind = ArgKind::DynamicQuery, .description = "Branch to merge", .queryTag = "branches", .repeatable = true },
            },
        };
    }

    [[nodiscard]] auto pullSubcommand() -> SubcommandDef
    {
        return {
            .name = "pull",
            .description = "Fetch from and integrate with another repository or branch",
            .options = {
                { .longName = "--rebase", .shortName = "-r", .description = "Rebase instead of merge" },
                { .longName = "--no-rebase", .description = "Merge (do not rebase)" },
                { .longName = "--ff-only", .description = "Only fast-forward" },
                { .longName = "--no-ff", .description = "Create merge commit" },
                { .longName = "--autostash", .description = "Stash/unstash around pull" },
                { .longName = "--no-autostash", .description = "Do not autostash" },
                { .longName = "--all", .description = "Fetch all remotes" },
                { .longName = "--tags", .shortName = "-t", .description = "Fetch all tags" },
                { .longName = "--prune", .shortName = "-p", .description = "Prune stale refs" },
                verboseOpt(),
                quietOpt(),
            },
            .positionalArgs = {
                { .kind = ArgKind::DynamicQuery, .description = "Remote", .queryTag = "remotes" },
                { .kind = ArgKind::DynamicQuery, .description = "Branch", .queryTag = "branches" },
            },
        };
    }

    [[nodiscard]] auto pushSubcommand() -> SubcommandDef
    {
        return {
            .name = "push",
            .description = "Update remote refs along with associated objects",
            .options = {
                forceOpt(),
                { .longName = "--force-with-lease", .description = "Safe force push" },
                { .longName = "--set-upstream", .shortName = "-u", .description = "Set upstream for branch" },
                { .longName = "--all", .description = "Push all branches" },
                { .longName = "--tags", .description = "Push all tags" },
                { .longName = "--delete", .shortName = "-d", .description = "Delete remote branch" },
                { .longName = "--prune", .description = "Remove remote branches without local counterpart" },
                dryRunOpt(),
                verboseOpt(),
                quietOpt(),
                { .longName = "--no-verify", .description = "Skip pre-push hooks" },
            },
            .positionalArgs = {
                { .kind = ArgKind::DynamicQuery, .description = "Remote", .queryTag = "remotes" },
                { .kind = ArgKind::DynamicQuery, .description = "Branch", .queryTag = "branches" },
            },
        };
    }

    [[nodiscard]] auto rebaseSubcommand() -> SubcommandDef
    {
        return {
            .name = "rebase",
            .description = "Reapply commits on top of another base tip",
            .options = {
                { .longName = "--interactive", .shortName = "-i", .description = "Interactive rebase" },
                { .longName = "--onto", .description = "Rebase onto branch",
                  .valueKind = OptionValueKind::DynamicQuery, .queryTag = "branches" },
                { .longName = "--abort", .description = "Abort rebase" },
                { .longName = "--continue", .description = "Continue rebase" },
                { .longName = "--skip", .description = "Skip current patch" },
                { .longName = "--autosquash", .description = "Auto-squash fixup commits" },
                { .longName = "--no-autosquash", .description = "Do not auto-squash" },
                { .longName = "--autostash", .description = "Stash/unstash around rebase" },
                { .longName = "--signoff", .description = "Add Signed-off-by" },
                forceOpt(),
                quietOpt(),
                verboseOpt(),
            },
            .positionalArgs = {
                { .kind = ArgKind::DynamicQuery, .description = "Upstream branch", .queryTag = "branches" },
            },
        };
    }

    [[nodiscard]] auto remoteSubcommand() -> SubcommandDef
    {
        return {
            .name = "remote",
            .description = "Manage set of tracked repositories",
            .options = {
                verboseOpt(),
            },
            .subcommands = {
                { .name = "add", .description = "Add a remote",
                  .options = {
                    { .longName = "--fetch", .shortName = "-f", .description = "Fetch after adding" },
                    { .longName = "--tags", .shortName = "-t", .description = "Import tags" },
                  },
                },
                { .name = "remove", .description = "Remove a remote",
                  .positionalArgs = {{ .kind = ArgKind::DynamicQuery, .description = "Remote", .queryTag = "remotes" }} },
                { .name = "rename", .description = "Rename a remote",
                  .positionalArgs = {{ .kind = ArgKind::DynamicQuery, .description = "Remote", .queryTag = "remotes" }} },
                { .name = "show", .description = "Show remote details",
                  .positionalArgs = {{ .kind = ArgKind::DynamicQuery, .description = "Remote", .queryTag = "remotes" }} },
                { .name = "prune", .description = "Remove stale refs",
                  .positionalArgs = {{ .kind = ArgKind::DynamicQuery, .description = "Remote", .queryTag = "remotes" }} },
                { .name = "set-url", .description = "Change remote URL",
                  .positionalArgs = {{ .kind = ArgKind::DynamicQuery, .description = "Remote", .queryTag = "remotes" }} },
                { .name = "get-url", .description = "Get remote URL",
                  .positionalArgs = {{ .kind = ArgKind::DynamicQuery, .description = "Remote", .queryTag = "remotes" }} },
                { .name = "update", .description = "Fetch updates for remotes" },
            },
        };
    }

    [[nodiscard]] auto resetSubcommand() -> SubcommandDef
    {
        return {
            .name = "reset",
            .description = "Reset current HEAD to the specified state",
            .options = {
                { .longName = "--soft", .description = "Keep changes staged" },
                { .longName = "--mixed", .description = "Unstage changes (default)" },
                { .longName = "--hard", .description = "Discard all changes" },
                { .longName = "--merge", .description = "Reset with merge semantics" },
                { .longName = "--keep", .description = "Reset, keeping local changes" },
                { .longName = "--patch", .shortName = "-p", .description = "Interactively select hunks" },
                quietOpt(),
            },
            .positionalArgs = {
                { .kind = ArgKind::DynamicQuery, .description = "Commit", .queryTag = "branches", .repeatable = true },
            },
        };
    }

    [[nodiscard]] auto revertSubcommand() -> SubcommandDef
    {
        return {
            .name = "revert",
            .description = "Revert some existing commits",
            .options = {
                { .longName = "--edit", .shortName = "-e", .description = "Edit commit message" },
                { .longName = "--no-edit", .description = "Use default message" },
                { .longName = "--no-commit", .shortName = "-n", .description = "Revert without committing" },
                { .longName = "--signoff", .shortName = "-s", .description = "Add Signed-off-by" },
                { .longName = "--mainline", .shortName = "-m", .description = "Parent number for merge",
                  .valueKind = OptionValueKind::String },
                { .longName = "--abort", .description = "Abort revert" },
                { .longName = "--continue", .description = "Continue revert" },
                { .longName = "--skip", .description = "Skip current commit" },
            },
            .positionalArgs = {
                { .kind = ArgKind::DynamicQuery, .description = "Commit", .queryTag = "recent-commits", .repeatable = true },
            },
        };
    }

    [[nodiscard]] auto stashSubcommand() -> SubcommandDef
    {
        return {
            .name = "stash",
            .description = "Stash the changes in a dirty working directory away",
            .subcommands = {
                { .name = "push", .description = "Stash changes",
                  .options = {
                    { .longName = "--message", .shortName = "-m", .description = "Stash message",
                      .valueKind = OptionValueKind::String },
                    { .longName = "--keep-index", .shortName = "-k", .description = "Keep staged changes" },
                    { .longName = "--include-untracked", .shortName = "-u", .description = "Include untracked files" },
                    { .longName = "--all", .shortName = "-a", .description = "Include ignored files" },
                    { .longName = "--patch", .shortName = "-p", .description = "Interactively select hunks" },
                  },
                  .positionalArgs = {{ .kind = ArgKind::Path, .description = "Files to stash", .repeatable = true }} },
                { .name = "pop", .description = "Apply and remove stash",
                  .positionalArgs = {{ .kind = ArgKind::DynamicQuery, .description = "Stash entry", .queryTag = "stashes" }} },
                { .name = "apply", .description = "Apply stash",
                  .positionalArgs = {{ .kind = ArgKind::DynamicQuery, .description = "Stash entry", .queryTag = "stashes" }} },
                { .name = "drop", .description = "Remove stash entry",
                  .positionalArgs = {{ .kind = ArgKind::DynamicQuery, .description = "Stash entry", .queryTag = "stashes" }} },
                { .name = "show", .description = "Show stash contents",
                  .options = {
                    { .longName = "--patch", .shortName = "-p", .description = "Show as patch" },
                    { .longName = "--stat", .description = "Show diffstat" },
                  },
                  .positionalArgs = {{ .kind = ArgKind::DynamicQuery, .description = "Stash entry", .queryTag = "stashes" }} },
                { .name = "list", .description = "List stash entries" },
                { .name = "clear", .description = "Remove all stash entries" },
                { .name = "branch", .description = "Create branch from stash",
                  .positionalArgs = {
                    { .kind = ArgKind::Any, .description = "Branch name" },
                    { .kind = ArgKind::DynamicQuery, .description = "Stash entry", .queryTag = "stashes" },
                  }},
            },
        };
    }

    [[nodiscard]] auto switchSubcommand() -> SubcommandDef
    {
        return {
            .name = "switch",
            .description = "Switch branches",
            .options = {
                { .longName = "--create", .shortName = "-c", .description = "Create and switch to new branch",
                  .valueKind = OptionValueKind::String },
                { .longName = "", .shortName = "-C", .description = "Create/reset and switch to branch",
                  .valueKind = OptionValueKind::String },
                { .longName = "--detach", .shortName = "-d", .description = "Detach HEAD" },
                forceOpt(),
                { .longName = "--discard-changes", .description = "Discard local changes" },
                { .longName = "--track", .shortName = "-t", .description = "Set up tracking" },
                { .longName = "--no-track", .description = "Do not set up tracking" },
                { .longName = "--guess", .description = "Guess remote branch (default)" },
                { .longName = "--no-guess", .description = "Do not guess remote branch" },
                quietOpt(),
            },
            .positionalArgs = {
                { .kind = ArgKind::DynamicQuery, .description = "Branch", .queryTag = "branches" },
            },
        };
    }

    [[nodiscard]] auto tagSubcommand() -> SubcommandDef
    {
        return {
            .name = "tag",
            .description = "Create, list, delete or verify a tag object",
            .options = {
                { .longName = "--annotate", .shortName = "-a", .description = "Create annotated tag" },
                { .longName = "--sign", .shortName = "-s", .description = "Create GPG-signed tag" },
                { .longName = "--delete", .shortName = "-d", .description = "Delete tag" },
                { .longName = "--verify", .shortName = "-v", .description = "Verify GPG signature" },
                { .longName = "--list", .shortName = "-l", .description = "List tags" },
                { .longName = "--message", .shortName = "-m", .description = "Tag message",
                  .valueKind = OptionValueKind::String },
                { .longName = "--file", .shortName = "-F", .description = "Message from file",
                  .valueKind = OptionValueKind::Path },
                forceOpt(),
                { .longName = "--sort", .description = "Sort order",
                  .valueKind = OptionValueKind::String },
                { .longName = "--contains", .description = "Tags containing commit",
                  .valueKind = OptionValueKind::DynamicQuery, .queryTag = "recent-commits" },
            },
            .positionalArgs = {
                { .kind = ArgKind::DynamicQuery, .description = "Tag name", .queryTag = "tags" },
            },
        };
    }

    // ========================================================================
    // Tier 2: Key options (15 more subcommands)
    // ========================================================================

    [[nodiscard]] auto amSubcommand() -> SubcommandDef
    {
        return {
            .name = "am",
            .description = "Apply a series of patches from a mailbox",
            .options = {
                { .longName = "--signoff", .shortName = "-s", .description = "Add Signed-off-by" },
                { .longName = "--abort", .description = "Abort am" },
                { .longName = "--continue", .description = "Continue am" },
                { .longName = "--skip", .description = "Skip current patch" },
                { .longName = "--3way", .description = "Fall back to 3-way merge" },
                quietOpt(),
            },
        };
    }

    [[nodiscard]] auto applySubcommand() -> SubcommandDef
    {
        return {
            .name = "apply",
            .description = "Apply a patch to files and/or to the index",
            .options = {
                { .longName = "--stat", .description = "Show diffstat" },
                { .longName = "--check", .description = "Check if patch applies" },
                { .longName = "--index", .description = "Apply to both index and working tree" },
                { .longName = "--cached", .description = "Apply to index only" },
                { .longName = "--reverse", .shortName = "-R", .description = "Reverse the patch" },
                { .longName = "--3way", .description = "Fall back to 3-way merge" },
                verboseOpt(),
            },
        };
    }

    [[nodiscard]] auto blameSubcommand() -> SubcommandDef
    {
        return {
            .name = "blame",
            .description = "Show what revision and author last modified each line",
            .options = {
                { .longName = "--porcelain", .shortName = "-p", .description = "Machine-readable output" },
                { .longName = "--line-porcelain", .description = "Per-line porcelain" },
                { .longName = "--show-email", .shortName = "-e", .description = "Show author email" },
                { .longName = "", .shortName = "-w", .description = "Ignore whitespace" },
                { .longName = "", .shortName = "-L", .description = "Limit to line range",
                  .valueKind = OptionValueKind::String },
            },
            .positionalArgs = {
                { .kind = ArgKind::Path, .description = "File to blame" },
            },
        };
    }

    [[nodiscard]] auto cleanSubcommand() -> SubcommandDef
    {
        return {
            .name = "clean",
            .description = "Remove untracked files from the working tree",
            .options = {
                forceOpt(),
                { .longName = "", .shortName = "-d", .description = "Also remove directories" },
                { .longName = "", .shortName = "-x", .description = "Also remove ignored files" },
                { .longName = "", .shortName = "-X", .description = "Remove only ignored files" },
                dryRunOpt(),
                { .longName = "--interactive", .shortName = "-i", .description = "Interactive mode" },
                quietOpt(),
            },
        };
    }

    [[nodiscard]] auto describeSubcommand() -> SubcommandDef
    {
        return {
            .name = "describe",
            .description = "Give an object a human readable name based on an available ref",
            .options = {
                { .longName = "--all", .description = "Use any ref" },
                { .longName = "--tags", .description = "Use any tag" },
                { .longName = "--long", .description = "Always use long format" },
                { .longName = "--abbrev", .description = "Abbreviation length",
                  .valueKind = OptionValueKind::String },
                { .longName = "--exact-match", .description = "Only exact matches" },
                { .longName = "--dirty", .description = "Append dirty marker" },
                { .longName = "--always", .description = "Show uniquely abbreviated commit" },
            },
        };
    }

    [[nodiscard]] auto formatPatchSubcommand() -> SubcommandDef
    {
        return {
            .name = "format-patch",
            .description = "Prepare patches for e-mail submission",
            .options = {
                { .longName = "--stdout", .description = "Output to stdout" },
                { .longName = "--output-directory", .shortName = "-o", .description = "Output directory",
                  .valueKind = OptionValueKind::Path },
                { .longName = "--numbered", .shortName = "-n", .description = "Name output as numbered sequence" },
                { .longName = "--cover-letter", .description = "Generate cover letter" },
                { .longName = "--signoff", .shortName = "-s", .description = "Add Signed-off-by" },
            },
            .positionalArgs = {
                { .kind = ArgKind::DynamicQuery, .description = "Revision range", .queryTag = "branches" },
            },
        };
    }

    [[nodiscard]] auto grepSubcommand() -> SubcommandDef
    {
        return {
            .name = "grep",
            .description = "Print lines matching a pattern",
            .options = {
                { .longName = "--ignore-case", .shortName = "-i", .description = "Case insensitive" },
                { .longName = "--word-regexp", .shortName = "-w", .description = "Match whole words" },
                { .longName = "--extended-regexp", .shortName = "-E", .description = "Extended regex" },
                { .longName = "--perl-regexp", .shortName = "-P", .description = "Perl regex" },
                { .longName = "--count", .shortName = "-c", .description = "Show match count" },
                { .longName = "--line-number", .shortName = "-n", .description = "Show line numbers" },
                { .longName = "--files-with-matches", .shortName = "-l", .description = "Show only filenames" },
                { .longName = "--invert-match", .shortName = "-v", .description = "Invert match" },
            },
        };
    }

    [[nodiscard]] auto initSubcommand() -> SubcommandDef
    {
        return {
            .name = "init",
            .description = "Create an empty Git repository or reinitialize an existing one",
            .options = {
                { .longName = "--bare", .description = "Create bare repository" },
                { .longName = "--initial-branch", .shortName = "-b", .description = "Initial branch name",
                  .valueKind = OptionValueKind::String },
                { .longName = "--template", .description = "Template directory",
                  .valueKind = OptionValueKind::Path },
                quietOpt(),
            },
        };
    }

    [[nodiscard]] auto mvSubcommand() -> SubcommandDef
    {
        return {
            .name = "mv",
            .description = "Move or rename a file, a directory, or a symlink",
            .options = {
                forceOpt(),
                dryRunOpt(),
                verboseOpt(),
                { .longName = "", .shortName = "-k", .description = "Skip errors" },
            },
            .positionalArgs = {
                { .kind = ArgKind::Path, .description = "Source", .repeatable = true },
            },
        };
    }

    [[nodiscard]] auto notesSubcommand() -> SubcommandDef
    {
        return {
            .name = "notes",
            .description = "Add or inspect object notes",
            .subcommands = {
                { .name = "add", .description = "Add note" },
                { .name = "append", .description = "Append to note" },
                { .name = "edit", .description = "Edit note" },
                { .name = "show", .description = "Show note" },
                { .name = "remove", .description = "Remove note" },
                { .name = "list", .description = "List notes" },
                { .name = "prune", .description = "Remove notes for nonexistent objects" },
            },
        };
    }

    [[nodiscard]] auto reflogSubcommand() -> SubcommandDef
    {
        return {
            .name = "reflog",
            .description = "Manage reflog information",
            .subcommands = {
                { .name = "show", .description = "Show reflog entries" },
                { .name = "expire", .description = "Prune old reflog entries" },
                { .name = "delete", .description = "Delete reflog entries" },
            },
        };
    }

    [[nodiscard]] auto restoreSubcommand() -> SubcommandDef
    {
        return {
            .name = "restore",
            .description = "Restore working tree files",
            .options = {
                { .longName = "--source", .shortName = "-s", .description = "Restore from source",
                  .valueKind = OptionValueKind::DynamicQuery, .queryTag = "branches" },
                { .longName = "--staged", .shortName = "-S", .description = "Restore index" },
                { .longName = "--worktree", .shortName = "-W", .description = "Restore working tree" },
                { .longName = "--patch", .shortName = "-p", .description = "Interactive hunks" },
                { .longName = "--ours", .description = "Checkout our version" },
                { .longName = "--theirs", .description = "Checkout their version" },
                { .longName = "--merge", .shortName = "-m", .description = "Recreate conflict" },
            },
            .positionalArgs = {
                { .kind = ArgKind::DynamicQuery, .description = "Files to restore", .queryTag = "tracked-files", .repeatable = true },
            },
        };
    }

    [[nodiscard]] auto rmSubcommand() -> SubcommandDef
    {
        return {
            .name = "rm",
            .description = "Remove files from the working tree and from the index",
            .options = {
                forceOpt(),
                dryRunOpt(),
                { .longName = "", .shortName = "-r", .description = "Recursive removal" },
                { .longName = "--cached", .description = "Remove from index only" },
                quietOpt(),
            },
            .positionalArgs = {
                { .kind = ArgKind::DynamicQuery, .description = "Files to remove", .queryTag = "tracked-files", .repeatable = true },
            },
        };
    }

    [[nodiscard]] auto showSubcommand() -> SubcommandDef
    {
        return {
            .name = "show",
            .description = "Show various types of objects",
            .options = {
                { .longName = "--stat", .description = "Show diffstat" },
                { .longName = "--name-only", .description = "Show only names of changed files" },
                { .longName = "--name-status", .description = "Show names and status" },
                { .longName = "--format", .description = "Pretty format",
                  .valueKind = OptionValueKind::Enum,
                  .enumValues = { "oneline", "short", "medium", "full", "fuller", "email", "raw" } },
            },
            .positionalArgs = {
                { .kind = ArgKind::DynamicQuery, .description = "Object", .queryTag = "branches", .repeatable = true },
            },
        };
    }

    [[nodiscard]] auto worktreeSubcommand() -> SubcommandDef
    {
        return {
            .name = "worktree",
            .description = "Manage multiple working trees",
            .subcommands = {
                { .name = "add", .description = "Create new worktree",
                  .options = {
                    { .longName = "--force", .shortName = "-f", .description = "Force checkout even if already checked out" },
                    { .longName = "--detach", .shortName = "-d", .description = "Detach HEAD at named commit" },
                    { .longName = "--checkout", .description = "Populate the new working tree" },
                    { .longName = "--lock", .description = "Keep the new working tree locked" },
                    { .longName = "--reason", .description = "Reason for locking",
                      .valueKind = OptionValueKind::String },
                    { .longName = "--orphan", .description = "Create unborn branch" },
                    { .longName = "--track", .description = "Set up tracking mode" },
                    { .longName = "--guess-remote", .description = "Try to match branch with remote" },
                    { .longName = "--quiet", .shortName = "-q", .description = "Suppress progress reporting" },
                    { .longName = "", .shortName = "-b", .description = "Create new branch",
                      .valueKind = OptionValueKind::String },
                    { .longName = "", .shortName = "-B", .description = "Create or reset branch",
                      .valueKind = OptionValueKind::String },
                  },
                  .positionalArgs = {
                    { .kind = ArgKind::Path, .description = "Worktree path" },
                    { .kind = ArgKind::DynamicQuery, .description = "Branch", .queryTag = "branches" },
                  },
                },
                { .name = "list", .description = "List worktrees",
                  .options = {
                    { .longName = "--porcelain", .description = "Machine-readable output" },
                    { .longName = "--verbose", .shortName = "-v", .description = "Show extended annotations" },
                    { .longName = "--expire", .description = "Mark worktrees older than time as prunable",
                      .valueKind = OptionValueKind::String },
                    { .longName = "", .shortName = "-z", .description = "Terminate records with NUL" },
                  },
                },
                { .name = "remove", .description = "Remove worktree",
                  .options = {
                    { .longName = "--force", .shortName = "-f", .description = "Force removal even if dirty or locked" },
                  },
                  .positionalArgs = {
                    { .kind = ArgKind::DynamicQuery, .description = "Worktree", .queryTag = "worktrees" },
                  },
                },
                { .name = "prune", .description = "Prune stale worktree info",
                  .options = {
                    { .longName = "--dry-run", .shortName = "-n", .description = "Do not remove anything" },
                    { .longName = "--verbose", .shortName = "-v", .description = "Report pruned worktrees" },
                    { .longName = "--expire", .description = "Only expire worktrees older than time",
                      .valueKind = OptionValueKind::String },
                  },
                },
                { .name = "lock", .description = "Lock worktree",
                  .options = {
                    { .longName = "--reason", .description = "Reason for locking",
                      .valueKind = OptionValueKind::String },
                  },
                  .positionalArgs = {
                    { .kind = ArgKind::DynamicQuery, .description = "Worktree", .queryTag = "worktrees" },
                  },
                },
                { .name = "unlock", .description = "Unlock worktree",
                  .positionalArgs = {
                    { .kind = ArgKind::DynamicQuery, .description = "Worktree", .queryTag = "worktrees" },
                  },
                },
                { .name = "move", .description = "Move worktree to new path",
                  .options = {
                    { .longName = "--force", .shortName = "-f", .description = "Force move even if dirty or locked" },
                  },
                  .positionalArgs = {
                    { .kind = ArgKind::DynamicQuery, .description = "Worktree", .queryTag = "worktrees" },
                    { .kind = ArgKind::Path, .description = "New path" },
                  },
                },
                { .name = "repair", .description = "Repair worktree links",
                  .positionalArgs = {
                    { .kind = ArgKind::Path, .description = "Path", .repeatable = true },
                  },
                },
            },
        };
    }

    // ========================================================================
    // Tier 3: Basic name + description only
    // ========================================================================

    [[nodiscard]] auto tier3Subcommands() -> std::vector<SubcommandDef>
    {
        return {
            { .name = "archive", .description = "Create an archive of files from a named tree" },
            { .name = "bundle", .description = "Move objects and refs by archive" },
            { .name = "gc", .description = "Cleanup unnecessary files and optimize the local repository" },
            { .name = "help", .description = "Display help information about Git" },
            { .name = "ls-files",
              .description = "Show information about files in the index and working tree" },
            { .name = "ls-remote", .description = "List references in a remote repository" },
            { .name = "maintenance", .description = "Run tasks to optimize Git repository data" },
            { .name = "range-diff", .description = "Compare two commit ranges" },
            { .name = "shortlog", .description = "Summarize git log output" },
            { .name = "sparse-checkout",
              .description = "Reduce your working tree to a subset of tracked files" },
            { .name = "submodule", .description = "Initialize, update or inspect submodules" },
            { .name = "verify-commit", .description = "Check the GPG signature of commits" },
            { .name = "verify-tag", .description = "Check the GPG signature of tags" },
            { .name = "status", .description = "Show the working tree status" },
        };
    }

} // namespace

CommandSpec createGitSpec()
{
    auto spec = CommandSpec {};
    spec.command = "git";
    spec.description = "The stupid content tracker";

    // Global options (valid before any subcommand)
    spec.globalOptions = {
        { .longName = "--version", .description = "Print git version" },
        { .longName = "--help", .description = "Print help" },
        { .longName = "",
          .shortName = "-C",
          .description = "Run as if started in path",
          .valueKind = OptionValueKind::Path },
        { .longName = "",
          .shortName = "-c",
          .description = "Set configuration parameter",
          .valueKind = OptionValueKind::String },
        { .longName = "--git-dir",
          .description = "Set path to repository",
          .valueKind = OptionValueKind::Path },
        { .longName = "--work-tree",
          .description = "Set path to working tree",
          .valueKind = OptionValueKind::Path },
        { .longName = "--no-pager", .description = "Do not pipe output into a pager" },
        { .longName = "--bare", .description = "Treat repository as bare" },
        { .longName = "--no-replace-objects", .description = "Do not use replacement refs" },
        { .longName = "--literal-pathspecs", .description = "Treat pathspecs literally" },
    };

    // Tier 1: Full subcommand definitions
    spec.subcommands.push_back(addSubcommand());
    spec.subcommands.push_back(bisectSubcommand());
    spec.subcommands.push_back(branchSubcommand());
    spec.subcommands.push_back(checkoutSubcommand());
    spec.subcommands.push_back(cherryPickSubcommand());
    spec.subcommands.push_back(cloneSubcommand());
    spec.subcommands.push_back(commitSubcommand());
    spec.subcommands.push_back(configSubcommand());
    spec.subcommands.push_back(diffSubcommand());
    spec.subcommands.push_back(fetchSubcommand());
    spec.subcommands.push_back(logSubcommand());
    spec.subcommands.push_back(mergeSubcommand());
    spec.subcommands.push_back(pullSubcommand());
    spec.subcommands.push_back(pushSubcommand());
    spec.subcommands.push_back(rebaseSubcommand());
    spec.subcommands.push_back(remoteSubcommand());
    spec.subcommands.push_back(resetSubcommand());
    spec.subcommands.push_back(revertSubcommand());
    spec.subcommands.push_back(stashSubcommand());
    spec.subcommands.push_back(switchSubcommand());
    spec.subcommands.push_back(tagSubcommand());

    // Tier 2: Key options
    spec.subcommands.push_back(amSubcommand());
    spec.subcommands.push_back(applySubcommand());
    spec.subcommands.push_back(blameSubcommand());
    spec.subcommands.push_back(cleanSubcommand());
    spec.subcommands.push_back(describeSubcommand());
    spec.subcommands.push_back(formatPatchSubcommand());
    spec.subcommands.push_back(grepSubcommand());
    spec.subcommands.push_back(initSubcommand());
    spec.subcommands.push_back(mvSubcommand());
    spec.subcommands.push_back(notesSubcommand());
    spec.subcommands.push_back(reflogSubcommand());
    spec.subcommands.push_back(restoreSubcommand());
    spec.subcommands.push_back(rmSubcommand());
    spec.subcommands.push_back(showSubcommand());
    spec.subcommands.push_back(worktreeSubcommand());

    // Tier 3: Basic name + description
    auto const tier3 = tier3Subcommands();
    spec.subcommands.insert(spec.subcommands.end(), tier3.begin(), tier3.end());

    return spec;
}

// ============================================================================
// GitQueryProvider implementation
// ============================================================================

std::vector<std::string> GitQueryProvider::runCommand(std::string const& cmd)
{
    auto lines = std::vector<std::string> {};
    auto* fp = popen(cmd.c_str(), "r"); // NOLINT(cert-env33-c)
    if (!fp)
        return lines;

    auto buf = std::array<char, 512> {};
    auto current = std::string {};
    while (fgets(buf.data(), static_cast<int>(buf.size()), fp) != nullptr)
    {
        current += buf.data();
        while (!current.empty() && (current.back() == '\n' || current.back() == '\r'))
            current.pop_back();
        if (!current.empty())
            lines.push_back(std::move(current));
        current.clear();
    }
    pclose(fp); // NOLINT(cert-env33-c)

    return lines;
}

std::vector<QueryResult> GitQueryProvider::query(std::string_view queryTag)
{
    if (queryTag == "branches")
        return queryBranches(false, false);
    if (queryTag == "local-branches")
        return queryBranches(true, false);
    if (queryTag == "remote-branches")
        return queryBranches(false, true);
    if (queryTag == "tags")
        return queryTags();
    if (queryTag == "remotes")
        return queryRemotes();
    if (queryTag == "stashes")
        return queryStashes();
    if (queryTag == "recent-commits")
        return queryRecentCommits();
    if (queryTag == "aliases")
        return queryAliases();
    if (queryTag == "status-files")
        return queryStatusFiles();
    if (queryTag == "tracked-files")
        return queryTrackedFiles();
    if (queryTag == "config-keys")
        return queryConfigKeys();
    if (queryTag == "worktrees")
        return queryWorktrees();
    return {};
}

std::vector<QueryResult> GitQueryProvider::queryBranches(bool localOnly, bool remoteOnly)
{
    auto results = std::vector<QueryResult> {};
    auto seen = std::set<std::string> {};

#if defined(_WIN32)
    static constexpr auto localCmd = "git branch --format=\"%(refname:short)\" 2>NUL";
    static constexpr auto remoteCmd = "git branch -r --format=\"%(refname:short)\" 2>NUL";
#else
    static constexpr auto localCmd = "git branch --format='%(refname:short)' 2>/dev/null";
    static constexpr auto remoteCmd = "git branch -r --format='%(refname:short)' 2>/dev/null";
#endif

    if (!remoteOnly)
    {
        for (auto const& branch: runCommand(localCmd))
        {
            if (seen.insert(branch).second)
                results.push_back(QueryResult { .text = branch, .description = "local branch" });
        }
    }

    if (!localOnly)
    {
        for (auto const& branch: runCommand(remoteCmd))
        {
            auto stripped = branch;
            if (auto const slash = branch.find('/'); slash != std::string::npos)
                stripped = branch.substr(slash + 1);
            if (stripped == "HEAD")
                continue;
            // Always include the full remote ref (e.g., "origin/master")
            if (seen.insert(branch).second)
                results.push_back(QueryResult { .text = branch, .description = "remote branch" });
            // Also include the short name if it doesn't collide with a local branch
            if (seen.insert(stripped).second)
                results.push_back(QueryResult { .text = stripped, .description = "remote branch" });
        }
    }

    std::ranges::sort(results, {}, &QueryResult::text);
    return results;
}

std::vector<QueryResult> GitQueryProvider::queryTags()
{
#if defined(_WIN32)
    auto const lines = runCommand("git tag --list 2>NUL");
#else
    auto const lines = runCommand("git tag --list 2>/dev/null");
#endif
    auto results = std::vector<QueryResult> {};
    results.reserve(lines.size());
    for (auto const& tag: lines)
        results.push_back(QueryResult { .text = tag, .description = "tag" });
    return results;
}

std::vector<QueryResult> GitQueryProvider::queryRemotes()
{
#if defined(_WIN32)
    auto const lines = runCommand("git remote 2>NUL");
#else
    auto const lines = runCommand("git remote 2>/dev/null");
#endif
    auto results = std::vector<QueryResult> {};
    results.reserve(lines.size());
    for (auto const& remote: lines)
        results.push_back(QueryResult { .text = remote, .description = "remote" });
    return results;
}

std::vector<QueryResult> GitQueryProvider::queryStashes()
{
#if defined(_WIN32)
    auto const lines = runCommand("git stash list --format=\"%gd: %s\" 2>NUL");
#else
    auto const lines = runCommand("git stash list --format='%gd: %s' 2>/dev/null");
#endif
    auto results = std::vector<QueryResult> {};
    results.reserve(lines.size());
    for (auto const& line: lines)
    {
        // Format: "stash@{N}: message"
        if (auto const colon = line.find(':'); colon != std::string::npos)
        {
            auto ref = line.substr(0, colon);
            auto msg = line.substr(colon + 2);
            results.push_back(QueryResult { .text = std::move(ref), .description = std::move(msg) });
        }
        else
        {
            results.push_back(QueryResult { .text = line });
        }
    }
    return results;
}

std::vector<QueryResult> GitQueryProvider::queryRecentCommits()
{
#if defined(_WIN32)
    auto const lines = runCommand("git log --oneline -20 2>NUL");
#else
    auto const lines = runCommand("git log --oneline -20 2>/dev/null");
#endif
    auto results = std::vector<QueryResult> {};
    results.reserve(lines.size());
    for (auto const& line: lines)
    {
        if (auto const space = line.find(' '); space != std::string::npos)
        {
            auto hash = line.substr(0, space);
            auto msg = line.substr(space + 1);
            results.push_back(QueryResult { .text = std::move(hash), .description = std::move(msg) });
        }
    }
    return results;
}

std::vector<QueryResult> GitQueryProvider::queryAliases()
{
#if defined(_WIN32)
    auto const lines = runCommand("git config --get-regexp \"^alias\\.\" 2>NUL");
#else
    auto const lines = runCommand("git config --get-regexp '^alias\\.' 2>/dev/null");
#endif
    auto results = std::vector<QueryResult> {};
    for (auto const& line: lines)
    {
        // Format: "alias.co checkout"
        if (auto const space = line.find(' '); space != std::string::npos)
        {
            auto key = line.substr(0, space);
            auto value = line.substr(space + 1);
            // Strip "alias." prefix
            if (key.starts_with("alias."))
                key = key.substr(6);
            results.push_back(
                QueryResult { .text = std::move(key), .description = "alias: " + std::move(value) });
        }
    }
    return results;
}

std::vector<QueryResult> GitQueryProvider::queryStatusFiles()
{
#if defined(_WIN32)
    auto const lines = runCommand("git status --porcelain 2>NUL");
#else
    auto const lines = runCommand("git status --porcelain 2>/dev/null");
#endif
    auto results = std::vector<QueryResult> {};
    results.reserve(lines.size());
    for (auto const& line: lines)
    {
        if (line.size() < 4)
            continue;
        auto const status = line.substr(0, 2);
        auto file = line.substr(3);

        auto desc = std::string {};
        if (status == "??")
            desc = "untracked";
        else if (status[0] == 'M' || status[1] == 'M')
            desc = "modified";
        else if (status[0] == 'A')
            desc = "added";
        else if (status[0] == 'D' || status[1] == 'D')
            desc = "deleted";
        else if (status[0] == 'R')
            desc = "renamed";
        else if (status[0] == 'C')
            desc = "copied";
        else
            desc = "changed";

        results.push_back(QueryResult { .text = std::move(file), .description = std::move(desc) });
    }
    return results;
}

std::vector<QueryResult> GitQueryProvider::queryTrackedFiles()
{
#if defined(_WIN32)
    auto const lines = runCommand("git ls-files 2>NUL");
#else
    auto const lines = runCommand("git ls-files 2>/dev/null");
#endif
    auto results = std::vector<QueryResult> {};
    results.reserve(lines.size());
    for (auto const& file: lines)
        results.push_back(QueryResult { .text = file, .description = "tracked file" });
    return results;
}

std::vector<QueryResult> GitQueryProvider::queryConfigKeys()
{
#if defined(_WIN32)
    auto const lines = runCommand("git config --list --name-only 2>NUL");
#else
    auto const lines = runCommand("git config --list --name-only 2>/dev/null");
#endif
    auto results = std::vector<QueryResult> {};
    auto seen = std::set<std::string> {};
    for (auto const& key: lines)
    {
        if (seen.insert(key).second)
            results.push_back(QueryResult { .text = key, .description = "config key" });
    }
    return results;
}

std::vector<QueryResult> GitQueryProvider::queryWorktrees()
{
#if defined(_WIN32)
    auto const lines = runCommand("git worktree list --porcelain 2>NUL");
#else
    auto const lines = runCommand("git worktree list --porcelain 2>/dev/null");
#endif
    auto results = std::vector<QueryResult> {};
    auto path = std::string {};
    auto branch = std::string {};
    for (auto const& line: lines)
    {
        if (line.starts_with("worktree "))
        {
            path = line.substr(9);
            branch.clear();
        }
        else if (line.starts_with("branch "))
        {
            // Format: "branch refs/heads/name" — extract short name.
            if (auto const lastSlash = line.rfind('/'); lastSlash != std::string::npos)
                branch = line.substr(lastSlash + 1);
            else
                branch = line.substr(7);
        }
        else if (line.empty() && !path.empty())
        {
            results.push_back(
                QueryResult { .text = std::move(path),
                              .description = branch.empty() ? "worktree" : "worktree [" + branch + "]" });
            path.clear();
            branch.clear();
        }
    }
    // Flush last entry (porcelain output may not end with a blank line).
    if (!path.empty())
        results.push_back(
            QueryResult { .text = std::move(path),
                          .description = branch.empty() ? "worktree" : "worktree [" + branch + "]" });
    return results;
}

} // namespace endo
