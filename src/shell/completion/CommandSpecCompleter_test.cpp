// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

#include "CommandLineParser.hpp"
#include "CommandSpecCompleter.hpp"
#include "GitSpec.hpp"
#include "QueryCache.hpp"

using namespace std::string_literals;

namespace
{

/// @brief Helper to create an Argument-type CompletionContext for git commands.
endo::CompletionContext makeGitContext(std::string fullInput,
                                       std::string prefix = "",
                                       std::string command = "git")
{
    auto const cursor = fullInput.size();
    auto const prefixStart = cursor - prefix.size();
    return endo::CompletionContext {
        .type = endo::CompletionContextType::Argument,
        .prefix = std::move(prefix),
        .prefixStart = prefixStart,
        .cursorPosition = cursor,
        .command = std::move(command),
        .fullInput = std::move(fullInput),
    };
}

/// @brief Helper to create an Option-type CompletionContext.
endo::CompletionContext makeOptionContext(std::string fullInput,
                                          std::string prefix,
                                          std::string command = "git")
{
    auto const cursor = fullInput.size();
    auto const prefixStart = cursor - prefix.size();
    return endo::CompletionContext {
        .type = endo::CompletionContextType::Option,
        .prefix = std::move(prefix),
        .prefixStart = prefixStart,
        .cursorPosition = cursor,
        .command = std::move(command),
        .fullInput = std::move(fullInput),
    };
}

/// @brief Helper to check if a specific text is in the completions.
bool hasCompletion(std::vector<tui::CompletionItem> const& items, std::string const& text)
{
    for (auto const& item: items)
        if (item.text == text)
            return true;
    return false;
}

/// @brief Mock query provider for testing without running git commands.
class MockQueryProvider: public endo::CommandQueryProvider
{
  public:
    std::vector<endo::QueryResult> query(std::string_view queryTag) override
    {
        if (queryTag == "branches")
            return { { "main", "local branch" },
                     { "develop", "local branch" },
                     { "origin/develop", "remote branch" },
                     { "feature/test", "remote branch" } };
        if (queryTag == "local-branches")
            return { { "main", "local branch" }, { "develop", "local branch" } };
        if (queryTag == "remotes")
            return { { "origin", "remote" }, { "upstream", "remote" } };
        if (queryTag == "tags")
            return { { "v1.0", "tag" }, { "v2.0", "tag" } };
        if (queryTag == "stashes")
            return { { "stash@{0}", "WIP on main" }, { "stash@{1}", "Fix bugs" } };
        if (queryTag == "status-files")
            return { { "src/main.cpp", "modified" }, { "README.md", "untracked" } };
        if (queryTag == "config-keys")
            return { { "user.name", "config key" }, { "user.email", "config key" } };
        if (queryTag == "aliases")
            return { { "co", "alias: checkout" }, { "br", "alias: branch" }, { "st", "alias: status" } };
        if (queryTag == "recent-commits")
            return { { "abc1234", "Fix the bug" }, { "def5678", "Add feature" } };
        if (queryTag == "tracked-files")
            return { { "src/main.cpp", "tracked file" }, { "CMakeLists.txt", "tracked file" } };
        if (queryTag == "worktrees")
            return { { "wt-feature", "worktree [feature]" },
                     { "wt-bugfix", "worktree [bugfix]" } };
        return {};
    }
};

/// @brief Creates a CommandSpecCompleter with git spec + mock provider for testing.
endo::CommandSpecCompleter createMockGitCompleter()
{
    auto completer = endo::CommandSpecCompleter {};
    completer.registerCommand(endo::createGitSpec(), std::make_unique<MockQueryProvider>());
    return completer;
}

} // namespace

// ============================================================================
// canHandle tests
// ============================================================================

TEST_CASE("CommandSpecCompleter.canHandle_argument_and_option")
{
    auto completer = createMockGitCompleter();

    CHECK(completer.canHandle(endo::CompletionContextType::Argument));
    CHECK(completer.canHandle(endo::CompletionContextType::Option));
    CHECK_FALSE(completer.canHandle(endo::CompletionContextType::Command));
    CHECK_FALSE(completer.canHandle(endo::CompletionContextType::FilePath));
    CHECK_FALSE(completer.canHandle(endo::CompletionContextType::Variable));
}

TEST_CASE("CommandSpecCompleter.priority")
{
    auto completer = createMockGitCompleter();
    CHECK(completer.priority() == 85);
}

// ============================================================================
// Guard: non-registered commands
// ============================================================================

TEST_CASE("CommandSpecCompleter.non_registered_command_returns_empty")
{
    auto completer = createMockGitCompleter();
    auto ctx = makeGitContext("ls -la ", "", "ls");
    auto results = completer.complete(ctx);
    CHECK(results.empty());
}

TEST_CASE("CommandSpecCompleter.no_command_returns_empty")
{
    auto completer = createMockGitCompleter();
    auto ctx = endo::CompletionContext {
        .type = endo::CompletionContextType::Argument,
        .cursorPosition = 0,
    };
    auto results = completer.complete(ctx);
    CHECK(results.empty());
}

// ============================================================================
// Subcommand completion
// ============================================================================

TEST_CASE("CommandSpecCompleter.git_subcommand_completion")
{
    auto completer = createMockGitCompleter();
    auto ctx = makeGitContext("git ");
    auto results = completer.complete(ctx);

    CHECK_FALSE(results.empty());
    CHECK(hasCompletion(results, "checkout"));
    CHECK(hasCompletion(results, "commit"));
    CHECK(hasCompletion(results, "push"));
    CHECK(hasCompletion(results, "pull"));
    CHECK(hasCompletion(results, "branch"));
    CHECK(hasCompletion(results, "merge"));
    CHECK(hasCompletion(results, "rebase"));
    CHECK(hasCompletion(results, "stash"));
    CHECK(hasCompletion(results, "status"));
}

TEST_CASE("CommandSpecCompleter.git_subcommand_prefix_filters")
{
    auto completer = createMockGitCompleter();
    auto ctx = makeGitContext("git ch", "ch");
    auto results = completer.complete(ctx);

    CHECK_FALSE(results.empty());
    CHECK(hasCompletion(results, "checkout"));
    CHECK(hasCompletion(results, "cherry-pick"));
    CHECK_FALSE(hasCompletion(results, "commit"));
    CHECK_FALSE(hasCompletion(results, "push"));
}

TEST_CASE("CommandSpecCompleter.git_subcommand_includes_aliases")
{
    auto completer = createMockGitCompleter();
    auto ctx = makeGitContext("git ");
    auto results = completer.complete(ctx);

    // Aliases from MockQueryProvider
    CHECK(hasCompletion(results, "co"));
    CHECK(hasCompletion(results, "st"));
}

// ============================================================================
// Nested subcommand completion
// ============================================================================

TEST_CASE("CommandSpecCompleter.git_stash_subcommands")
{
    auto completer = createMockGitCompleter();
    auto ctx = makeGitContext("git stash ");
    auto results = completer.complete(ctx);

    CHECK_FALSE(results.empty());
    CHECK(hasCompletion(results, "push"));
    CHECK(hasCompletion(results, "pop"));
    CHECK(hasCompletion(results, "apply"));
    CHECK(hasCompletion(results, "drop"));
    CHECK(hasCompletion(results, "list"));
    CHECK(hasCompletion(results, "show"));
}

TEST_CASE("CommandSpecCompleter.git_remote_subcommands")
{
    auto completer = createMockGitCompleter();
    auto ctx = makeGitContext("git remote ");
    auto results = completer.complete(ctx);

    CHECK_FALSE(results.empty());
    CHECK(hasCompletion(results, "add"));
    CHECK(hasCompletion(results, "remove"));
    CHECK(hasCompletion(results, "rename"));
    CHECK(hasCompletion(results, "show"));
}

// ============================================================================
// Branch argument completion (replaces old GitBranchCompleter tests)
// ============================================================================

TEST_CASE("CommandSpecCompleter.checkout_suggests_branches")
{
    auto completer = createMockGitCompleter();
    auto ctx = makeGitContext("git checkout ");
    auto results = completer.complete(ctx);

    CHECK_FALSE(results.empty());
    CHECK(hasCompletion(results, "main"));
    CHECK(hasCompletion(results, "develop"));
    CHECK(hasCompletion(results, "feature/test"));
}

TEST_CASE("CommandSpecCompleter.checkout_prefix_filters")
{
    auto completer = createMockGitCompleter();
    auto ctx = makeGitContext("git checkout ma", "ma");
    auto results = completer.complete(ctx);

    CHECK_FALSE(results.empty());
    CHECK(hasCompletion(results, "main"));
    CHECK_FALSE(hasCompletion(results, "develop"));
}

TEST_CASE("CommandSpecCompleter.switch_suggests_branches")
{
    auto completer = createMockGitCompleter();
    auto ctx = makeGitContext("git switch ");
    auto results = completer.complete(ctx);

    CHECK_FALSE(results.empty());
    CHECK(hasCompletion(results, "main"));
    CHECK(hasCompletion(results, "develop"));
}

TEST_CASE("CommandSpecCompleter.merge_suggests_branches")
{
    auto completer = createMockGitCompleter();
    auto ctx = makeGitContext("git merge ");
    auto results = completer.complete(ctx);

    CHECK_FALSE(results.empty());
    CHECK(hasCompletion(results, "main"));
}

TEST_CASE("CommandSpecCompleter.rebase_suggests_branches")
{
    auto completer = createMockGitCompleter();
    auto ctx = makeGitContext("git rebase ");
    auto results = completer.complete(ctx);

    CHECK_FALSE(results.empty());
    CHECK(hasCompletion(results, "main"));
    CHECK(hasCompletion(results, "origin/develop"));
}

// ============================================================================
// isExclusiveFor: DynamicQuery args suppress FileCompleter
// ============================================================================

TEST_CASE("CommandSpecCompleter.rebase_exclusive_suppresses_files")
{
    auto completer = createMockGitCompleter();
    auto ctx = makeGitContext("git rebase ");
    CHECK(completer.isExclusiveFor(ctx));
}

TEST_CASE("CommandSpecCompleter.commit_path_arg_not_exclusive_allows_files")
{
    auto completer = createMockGitCompleter();
    auto ctx = makeGitContext("git commit ");
    CHECK_FALSE(completer.isExclusiveFor(ctx));
}

// ============================================================================
// Push: remote then branch
// ============================================================================

TEST_CASE("CommandSpecCompleter.push_first_arg_suggests_remotes")
{
    auto completer = createMockGitCompleter();
    auto ctx = makeGitContext("git push ");
    auto results = completer.complete(ctx);

    CHECK_FALSE(results.empty());
    CHECK(hasCompletion(results, "origin"));
    CHECK(hasCompletion(results, "upstream"));
}

TEST_CASE("CommandSpecCompleter.push_after_remote_suggests_branches")
{
    auto completer = createMockGitCompleter();
    auto ctx = makeGitContext("git push origin ");
    auto results = completer.complete(ctx);

    CHECK_FALSE(results.empty());
    CHECK(hasCompletion(results, "main"));
    CHECK(hasCompletion(results, "develop"));
}

// ============================================================================
// Add: status files
// ============================================================================

TEST_CASE("CommandSpecCompleter.add_suggests_status_files")
{
    auto completer = createMockGitCompleter();
    auto ctx = makeGitContext("git add ");
    auto results = completer.complete(ctx);

    CHECK_FALSE(results.empty());
    CHECK(hasCompletion(results, "src/main.cpp"));
    CHECK(hasCompletion(results, "README.md"));
}

// ============================================================================
// Stash pop: stash entries
// ============================================================================

TEST_CASE("CommandSpecCompleter.stash_pop_suggests_stashes")
{
    auto completer = createMockGitCompleter();
    auto ctx = makeGitContext("git stash pop ");
    auto results = completer.complete(ctx);

    CHECK_FALSE(results.empty());
    CHECK(hasCompletion(results, "stash@{0}"));
    CHECK(hasCompletion(results, "stash@{1}"));
}

// ============================================================================
// Config: config keys
// ============================================================================

TEST_CASE("CommandSpecCompleter.config_suggests_keys")
{
    auto completer = createMockGitCompleter();
    auto ctx = makeGitContext("git config ");
    auto results = completer.complete(ctx);

    CHECK_FALSE(results.empty());
    CHECK(hasCompletion(results, "user.name"));
    CHECK(hasCompletion(results, "user.email"));
}

// ============================================================================
// Tag completion
// ============================================================================

TEST_CASE("CommandSpecCompleter.tag_suggests_tags")
{
    auto completer = createMockGitCompleter();
    auto ctx = makeGitContext("git tag ");
    auto results = completer.complete(ctx);

    CHECK_FALSE(results.empty());
    CHECK(hasCompletion(results, "v1.0"));
    CHECK(hasCompletion(results, "v2.0"));
}

// ============================================================================
// Option completion
// ============================================================================

TEST_CASE("CommandSpecCompleter.commit_options")
{
    auto completer = createMockGitCompleter();
    auto ctx = makeOptionContext("git commit --", "--");
    auto results = completer.complete(ctx);

    CHECK_FALSE(results.empty());
    CHECK(hasCompletion(results, "--message"));
    CHECK(hasCompletion(results, "--amend"));
    CHECK(hasCompletion(results, "--all"));
    CHECK(hasCompletion(results, "--no-edit"));
}

TEST_CASE("CommandSpecCompleter.commit_short_options")
{
    auto completer = createMockGitCompleter();
    auto ctx = makeOptionContext("git commit -", "-");
    auto results = completer.complete(ctx);

    CHECK_FALSE(results.empty());
    CHECK(hasCompletion(results, "-m"));
    CHECK(hasCompletion(results, "-a"));
}

TEST_CASE("CommandSpecCompleter.checkout_options")
{
    auto completer = createMockGitCompleter();
    auto ctx = makeOptionContext("git checkout --", "--");
    auto results = completer.complete(ctx);

    CHECK_FALSE(results.empty());
    CHECK(hasCompletion(results, "--branch"));
    CHECK(hasCompletion(results, "--force"));
    CHECK(hasCompletion(results, "--track"));
}

TEST_CASE("CommandSpecCompleter.push_options")
{
    auto completer = createMockGitCompleter();
    auto ctx = makeOptionContext("git push --", "--");
    auto results = completer.complete(ctx);

    CHECK_FALSE(results.empty());
    CHECK(hasCompletion(results, "--force"));
    CHECK(hasCompletion(results, "--set-upstream"));
    CHECK(hasCompletion(results, "--force-with-lease"));
    CHECK(hasCompletion(results, "--tags"));
}

// ============================================================================
// Options before subcommand
// ============================================================================

TEST_CASE("CommandSpecCompleter.options_before_subcommand")
{
    auto completer = createMockGitCompleter();
    // git -C /some/path checkout <tab>
    auto ctx = makeGitContext("git -C /some/path checkout ");
    auto results = completer.complete(ctx);

    CHECK_FALSE(results.empty());
    CHECK(hasCompletion(results, "main"));
}

// ============================================================================
// CommandLineParser tests
// ============================================================================

TEST_CASE("CommandLineParser.basic_subcommand_detection")
{
    auto const spec = endo::createGitSpec();
    auto const state = endo::parseCommandLine(spec, "git checkout ", 13, "");

    REQUIRE(state.has_value());
    CHECK(state->command == "git");
    CHECK(state->subcommandChain.size() == 1);
    CHECK(state->subcommandChain[0] == "checkout");
    CHECK(state->phase == endo::CompletionPhase::Argument);
}

TEST_CASE("CommandLineParser.subcommand_completion_phase")
{
    auto const spec = endo::createGitSpec();
    auto const state = endo::parseCommandLine(spec, "git ", 4, "");

    REQUIRE(state.has_value());
    CHECK(state->subcommandChain.empty());
    CHECK(state->phase == endo::CompletionPhase::Subcommand);
}

TEST_CASE("CommandLineParser.option_phase")
{
    auto const spec = endo::createGitSpec();
    auto const state = endo::parseCommandLine(spec, "git commit --am", 15, "--am");

    REQUIRE(state.has_value());
    CHECK(state->phase == endo::CompletionPhase::Option);
}

TEST_CASE("CommandLineParser.nested_subcommand")
{
    auto const spec = endo::createGitSpec();
    auto const state = endo::parseCommandLine(spec, "git stash pop ", 14, "");

    REQUIRE(state.has_value());
    CHECK(state->subcommandChain.size() == 2);
    CHECK(state->subcommandChain[0] == "stash");
    CHECK(state->subcommandChain[1] == "pop");
    CHECK(state->phase == endo::CompletionPhase::Argument);
}

TEST_CASE("CommandLineParser.global_option_with_value")
{
    auto const spec = endo::createGitSpec();
    auto const state = endo::parseCommandLine(spec, "git -C /path checkout ", 21, "");

    REQUIRE(state.has_value());
    CHECK(state->subcommandChain.size() == 1);
    CHECK(state->subcommandChain[0] == "checkout");
}

TEST_CASE("CommandLineParser.positional_args_tracking")
{
    auto const spec = endo::createGitSpec();
    auto const state = endo::parseCommandLine(spec, "git push origin ", 16, "");

    REQUIRE(state.has_value());
    CHECK(state->subcommandChain.size() == 1);
    CHECK(state->subcommandChain[0] == "push");
    CHECK(state->positionalArgs.size() == 1);
    CHECK(state->positionalArgs[0] == "origin");
    CHECK(state->positionalArgIndex == 1);
}

TEST_CASE("CommandLineParser.non_git_command_returns_nullopt")
{
    auto const spec = endo::createGitSpec();
    auto const state = endo::parseCommandLine(spec, "ls -la ", 7, "");
    CHECK_FALSE(state.has_value());
}

// ============================================================================
// QueryCache tests
// ============================================================================

TEST_CASE("QueryCache.caches_results")
{
    auto provider = std::make_unique<MockQueryProvider>();
    auto cache = endo::QueryCache(std::move(provider));

    auto results1 = cache.query("branches");
    auto results2 = cache.query("branches");

    CHECK(results1.size() == results2.size());
    CHECK(results1[0].text == results2[0].text);
}

TEST_CASE("QueryCache.invalidate_clears_cache")
{
    auto provider = std::make_unique<MockQueryProvider>();
    auto cache = endo::QueryCache(std::move(provider));

    (void) cache.query("branches");
    cache.invalidateAll();

    // After invalidation, should still return results (re-queries)
    auto results = cache.query("branches");
    CHECK_FALSE(results.empty());
}

// ============================================================================
// Live git tests (require running in a git repo)
// ============================================================================

TEST_CASE("CommandSpecCompleter.live_git_checkout_suggests_branches")
{
    auto completer = endo::CommandSpecCompleter {};
    completer.registerCommand(endo::createGitSpec(), std::make_unique<endo::GitQueryProvider>());

    auto ctx = makeGitContext("git checkout ");
    auto results = completer.complete(ctx);

    // Running in a git repo, should have at least one branch.
    // Note: We don't check for a specific branch name like "master" because
    // CI runners may only fetch the PR branch via shallow clone.
    CHECK_FALSE(results.empty());
}

TEST_CASE("CommandSpecCompleter.live_git_subcommand_completion")
{
    auto completer = endo::CommandSpecCompleter {};
    completer.registerCommand(endo::createGitSpec(), std::make_unique<endo::GitQueryProvider>());

    auto ctx = makeGitContext("git ");
    auto results = completer.complete(ctx);

    CHECK_FALSE(results.empty());
    CHECK(hasCompletion(results, "checkout"));
    CHECK(hasCompletion(results, "commit"));
    CHECK(hasCompletion(results, "status"));
}

// ============================================================================
// Multiple command registration
// ============================================================================

TEST_CASE("CommandSpecCompleter.multiple_commands")
{
    auto completer = endo::CommandSpecCompleter {};

    // Register git
    completer.registerCommand(endo::createGitSpec(), std::make_unique<MockQueryProvider>());

    // Register a simple mock command
    auto dockerSpec = endo::CommandSpec {
        .command = "docker",
        .description = "Container runtime",
        .subcommands = {
            { .name = "run", .description = "Run a container" },
            { .name = "build", .description = "Build an image" },
            { .name = "ps", .description = "List containers" },
        },
    };
    completer.registerCommand(std::move(dockerSpec));

    // Git completions work
    auto gitCtx = makeGitContext("git ");
    auto gitResults = completer.complete(gitCtx);
    CHECK(hasCompletion(gitResults, "checkout"));

    // Docker completions work
    auto dockerCtx = makeGitContext("docker ", "", "docker");
    auto dockerResults = completer.complete(dockerCtx);
    CHECK(hasCompletion(dockerResults, "run"));
    CHECK(hasCompletion(dockerResults, "build"));
    CHECK(hasCompletion(dockerResults, "ps"));
}

// ============================================================================
// Regression: stdlib pollution in subcommand completions
// ============================================================================

TEST_CASE("CommandSpecCompleter.git_subcommand_no_stdlib_pollution")
{
    // Verify that F# stdlib functions like "startsWith" don't leak
    // into git subcommand completions for prefix "sta"
    auto completer = createMockGitCompleter();
    auto ctx = makeGitContext("git sta", "sta");
    auto results = completer.complete(ctx);

    CHECK(hasCompletion(results, "stash"));
    CHECK(hasCompletion(results, "status"));
    CHECK_FALSE(hasCompletion(results, "startsWith"));
}

// ============================================================================
// Restore: tracked files
// ============================================================================

TEST_CASE("CommandSpecCompleter.restore_suggests_tracked_files")
{
    auto completer = createMockGitCompleter();
    auto ctx = makeGitContext("git restore ");
    auto results = completer.complete(ctx);

    CHECK_FALSE(results.empty());
    CHECK(hasCompletion(results, "src/main.cpp"));
    CHECK(hasCompletion(results, "CMakeLists.txt"));
}

// ============================================================================
// Git alias resolution tests
// ============================================================================

TEST_CASE("CommandSpecCompleter.alias_br_completes_branches")
{
    auto completer = createMockGitCompleter();
    auto ctx = makeGitContext("git br ");
    auto results = completer.complete(ctx);

    CHECK_FALSE(results.empty());
    CHECK(hasCompletion(results, "main"));
    CHECK(hasCompletion(results, "develop"));
    CHECK(hasCompletion(results, "feature/test"));
}

TEST_CASE("CommandSpecCompleter.alias_br_completes_branch_options")
{
    auto completer = createMockGitCompleter();
    auto ctx = makeOptionContext("git br --", "--");
    auto results = completer.complete(ctx);

    CHECK_FALSE(results.empty());
    CHECK(hasCompletion(results, "--delete"));
    CHECK(hasCompletion(results, "--move"));
    CHECK(hasCompletion(results, "--all"));
    CHECK(hasCompletion(results, "--remotes"));
}

TEST_CASE("CommandSpecCompleter.alias_br_delete_completes_only_local_branches")
{
    auto completer = createMockGitCompleter();
    auto ctx = makeGitContext("git br -d ");
    auto results = completer.complete(ctx);

    CHECK_FALSE(results.empty());
    CHECK(hasCompletion(results, "main"));
    CHECK(hasCompletion(results, "develop"));
    // Remote branches should NOT appear for delete operations
    CHECK_FALSE(hasCompletion(results, "origin/develop"));
    CHECK_FALSE(hasCompletion(results, "feature/test"));
}

TEST_CASE("CommandSpecCompleter.branch_delete_long_flag_completes_only_local_branches")
{
    auto completer = createMockGitCompleter();
    auto ctx = makeGitContext("git branch --delete ");
    auto results = completer.complete(ctx);

    CHECK_FALSE(results.empty());
    CHECK(hasCompletion(results, "main"));
    CHECK(hasCompletion(results, "develop"));
    CHECK_FALSE(hasCompletion(results, "origin/develop"));
    CHECK_FALSE(hasCompletion(results, "feature/test"));
}

TEST_CASE("CommandSpecCompleter.branch_force_delete_completes_only_local_branches")
{
    auto completer = createMockGitCompleter();
    auto ctx = makeGitContext("git branch -D ");
    auto results = completer.complete(ctx);

    CHECK_FALSE(results.empty());
    CHECK(hasCompletion(results, "main"));
    CHECK(hasCompletion(results, "develop"));
    CHECK_FALSE(hasCompletion(results, "origin/develop"));
    CHECK_FALSE(hasCompletion(results, "feature/test"));
}

TEST_CASE("CommandSpecCompleter.branch_without_delete_completes_all_branches")
{
    auto completer = createMockGitCompleter();
    auto ctx = makeGitContext("git branch ");
    auto results = completer.complete(ctx);

    CHECK_FALSE(results.empty());
    CHECK(hasCompletion(results, "main"));
    CHECK(hasCompletion(results, "develop"));
    CHECK(hasCompletion(results, "origin/develop"));
    CHECK(hasCompletion(results, "feature/test"));
}

TEST_CASE("CommandSpecCompleter.alias_co_completes_branches")
{
    auto completer = createMockGitCompleter();
    auto ctx = makeGitContext("git co ");
    auto results = completer.complete(ctx);

    CHECK_FALSE(results.empty());
    CHECK(hasCompletion(results, "main"));
    CHECK(hasCompletion(results, "develop"));
    CHECK(hasCompletion(results, "feature/test"));
}

TEST_CASE("CommandLineParser.alias_resolver_resolves_subcommand")
{
    auto const spec = endo::createGitSpec();
    auto resolver = [](std::string_view alias) -> std::optional<std::string> {
        if (alias == "br")
            return "branch";
        return std::nullopt;
    };
    auto const state = endo::parseCommandLine(spec, "git br ", 7, "", resolver);

    REQUIRE(state.has_value());
    CHECK(state->subcommandChain.size() == 1);
    CHECK(state->subcommandChain[0] == "branch");
    CHECK(state->phase == endo::CompletionPhase::Argument);
}

TEST_CASE("CommandSpecCompleter.worktree_add_branch_completion")
{
    auto completer = createMockGitCompleter();
    auto ctx = makeGitContext("git worktree add /tmp/wt ");
    auto results = completer.complete(ctx);

    CHECK_FALSE(results.empty());
    CHECK(hasCompletion(results, "main"));
    CHECK(hasCompletion(results, "develop"));
}

TEST_CASE("CommandSpecCompleter.worktree_remove_completion")
{
    auto completer = createMockGitCompleter();
    auto ctx = makeGitContext("git worktree remove ");
    auto results = completer.complete(ctx);

    CHECK_FALSE(results.empty());
    CHECK(hasCompletion(results, "wt-feature"));
    CHECK(hasCompletion(results, "wt-bugfix"));
}

TEST_CASE("CommandSpecCompleter.worktree_remove_options")
{
    auto completer = createMockGitCompleter();
    auto ctx = makeGitContext("git worktree remove -");
    auto results = completer.complete(ctx);

    CHECK_FALSE(results.empty());
    CHECK(hasCompletion(results, "--force"));
}

TEST_CASE("CommandSpecCompleter.worktree_lock_completion")
{
    auto completer = createMockGitCompleter();
    auto ctx = makeGitContext("git worktree lock ");
    auto results = completer.complete(ctx);

    CHECK_FALSE(results.empty());
    CHECK(hasCompletion(results, "wt-feature"));
    CHECK(hasCompletion(results, "wt-bugfix"));
}

TEST_CASE("CommandSpecCompleter.worktree_unlock_completion")
{
    auto completer = createMockGitCompleter();
    auto ctx = makeGitContext("git worktree unlock ");
    auto results = completer.complete(ctx);

    CHECK_FALSE(results.empty());
    CHECK(hasCompletion(results, "wt-bugfix"));
}

TEST_CASE("CommandSpecCompleter.worktree_move_completion")
{
    auto completer = createMockGitCompleter();
    auto ctx = makeGitContext("git worktree move ");
    auto results = completer.complete(ctx);

    // First positional arg: worktree name
    CHECK_FALSE(results.empty());
    CHECK(hasCompletion(results, "wt-feature"));
}

TEST_CASE("CommandSpecCompleter.worktree_list_options")
{
    auto completer = createMockGitCompleter();
    auto ctx = makeGitContext("git worktree list -");
    auto results = completer.complete(ctx);

    CHECK_FALSE(results.empty());
    CHECK(hasCompletion(results, "--porcelain"));
    CHECK(hasCompletion(results, "--verbose"));
}

TEST_CASE("CommandSpecCompleter.worktree_prune_options")
{
    auto completer = createMockGitCompleter();
    auto ctx = makeGitContext("git worktree prune -");
    auto results = completer.complete(ctx);

    CHECK_FALSE(results.empty());
    CHECK(hasCompletion(results, "--dry-run"));
    CHECK(hasCompletion(results, "--verbose"));
}
