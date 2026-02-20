// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

#include "CmakeSpec.hpp"
#include "CommandLineParser.hpp"
#include "CommandSpecCompleter.hpp"
#include "GitSpec.hpp"
#include "QueryCache.hpp"
#include "SshSpec.hpp"

using namespace std::string_literals;

namespace
{

/// @brief Helper to create an Argument-type CompletionContext for git commands.
endo::CompletionContext makeGitContext(std::string fullInput,
                                       std::string prefix = "",
                                       std::string command = "git")
{
    auto const cursor = fullInput.size();
    return endo::CompletionContext {
        .type = endo::CompletionContextType::Argument,
        .prefix = std::move(prefix),
        .prefixStart = cursor - prefix.size(),
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
    return endo::CompletionContext {
        .type = endo::CompletionContextType::Option,
        .prefix = std::move(prefix),
        .prefixStart = cursor - prefix.size(),
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

    // Running in a git repo, should have at least one branch
    CHECK_FALSE(results.empty());
    CHECK(hasCompletion(results, "master"));
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
// cmake / ctest completion tests
// ============================================================================

namespace
{

/// @brief Helper to create a CompletionContext for cmake/ctest commands.
endo::CompletionContext makeCmakeContext(std::string fullInput,
                                         std::string prefix = "",
                                         std::string command = "cmake")
{
    auto const cursor = fullInput.size();
    return endo::CompletionContext {
        .type = endo::CompletionContextType::Argument,
        .prefix = std::move(prefix),
        .prefixStart = cursor - prefix.size(),
        .cursorPosition = cursor,
        .command = std::move(command),
        .fullInput = std::move(fullInput),
    };
}

/// @brief Helper to create an Option-type CompletionContext for cmake/ctest.
endo::CompletionContext makeCmakeOptionContext(std::string fullInput,
                                               std::string prefix,
                                               std::string command = "cmake")
{
    auto const cursor = fullInput.size();
    return endo::CompletionContext {
        .type = endo::CompletionContextType::Option,
        .prefix = std::move(prefix),
        .prefixStart = cursor - prefix.size(),
        .cursorPosition = cursor,
        .command = std::move(command),
        .fullInput = std::move(fullInput),
    };
}

/// @brief Mock query provider returning preset names for cmake/ctest tests.
class MockCmakeQueryProvider: public endo::CommandQueryProvider
{
  public:
    std::vector<endo::QueryResult> query(std::string_view queryTag) override
    {
        if (queryTag == "presets")
            return { { "clang-debug", "Clang Debug" },
                     { "clang-release", "Clang Release" },
                     { "gcc-debug", "GCC Debug" },
                     { "gcc-release", "GCC Release" } };
        return {};
    }
};

/// @brief Creates a CommandSpecCompleter with cmake + ctest specs and mock provider.
endo::CommandSpecCompleter createMockCmakeCompleter()
{
    auto completer = endo::CommandSpecCompleter {};
    completer.registerCommand(endo::createCmakeSpec(), std::make_unique<MockCmakeQueryProvider>());
    completer.registerCommand(endo::createCtestSpec(), std::make_unique<MockCmakeQueryProvider>());
    return completer;
}

} // namespace

TEST_CASE("CmakeSpec.preset_completion")
{
    auto completer = createMockCmakeCompleter();
    auto ctx = makeCmakeContext("cmake --preset cl", "cl");
    auto results = completer.complete(ctx);

    CHECK_FALSE(results.empty());
    CHECK(hasCompletion(results, "clang-debug"));
    CHECK(hasCompletion(results, "clang-release"));
    CHECK_FALSE(hasCompletion(results, "gcc-debug"));
    CHECK_FALSE(hasCompletion(results, "gcc-release"));
}

TEST_CASE("CmakeSpec.ctest_preset_completion")
{
    auto completer = createMockCmakeCompleter();
    auto ctx = makeCmakeContext("ctest --preset ", "", "ctest");
    auto results = completer.complete(ctx);

    CHECK_FALSE(results.empty());
    CHECK(hasCompletion(results, "clang-debug"));
    CHECK(hasCompletion(results, "clang-release"));
    CHECK(hasCompletion(results, "gcc-debug"));
    CHECK(hasCompletion(results, "gcc-release"));
}

TEST_CASE("CmakeSpec.cmake_build_preset_completion")
{
    auto completer = createMockCmakeCompleter();
    auto ctx = makeCmakeContext("cmake --build --preset ", "");
    auto results = completer.complete(ctx);

    CHECK_FALSE(results.empty());
    CHECK(hasCompletion(results, "clang-debug"));
    CHECK(hasCompletion(results, "gcc-debug"));
}

TEST_CASE("CmakeSpec.cmake_options")
{
    auto completer = createMockCmakeCompleter();
    auto ctx = makeCmakeOptionContext("cmake --", "--");
    auto results = completer.complete(ctx);

    CHECK_FALSE(results.empty());
    CHECK(hasCompletion(results, "--build"));
    CHECK(hasCompletion(results, "--preset"));
    CHECK(hasCompletion(results, "--verbose"));
    CHECK(hasCompletion(results, "--config"));
    CHECK(hasCompletion(results, "--clean-first"));
}

TEST_CASE("CmakeSpec.cmake_generator_enum")
{
    auto completer = createMockCmakeCompleter();
    auto ctx = makeCmakeContext("cmake -G ", "");
    auto results = completer.complete(ctx);

    CHECK_FALSE(results.empty());
    CHECK(hasCompletion(results, "Ninja"));
    CHECK(hasCompletion(results, "Unix Makefiles"));
}

TEST_CASE("CmakeSpec.ctest_options")
{
    auto completer = createMockCmakeCompleter();
    auto ctx = makeCmakeOptionContext("ctest --", "--", "ctest");
    auto results = completer.complete(ctx);

    CHECK_FALSE(results.empty());
    CHECK(hasCompletion(results, "--preset"));
    CHECK(hasCompletion(results, "--output-on-failure"));
    CHECK(hasCompletion(results, "--stop-on-failure"));
    CHECK(hasCompletion(results, "--verbose"));
    CHECK(hasCompletion(results, "--rerun-failed"));
}

TEST_CASE("CmakeSpec.preset_completion_is_exclusive")
{
    auto completer = createMockCmakeCompleter();

    // Option value for --preset (DynamicQuery) should be exclusive
    auto presetCtx = makeCmakeContext("cmake --preset ");
    CHECK(completer.isExclusiveFor(presetCtx));

    // Option value for -S (Path) should NOT be exclusive — FileCompleter should still contribute
    auto pathCtx = makeCmakeContext("cmake -S ");
    CHECK_FALSE(completer.isExclusiveFor(pathCtx));

    // Subcommand/option phase should NOT be exclusive
    auto optCtx = makeCmakeOptionContext("cmake --", "--");
    CHECK_FALSE(completer.isExclusiveFor(optCtx));
}

TEST_CASE("CmakeSpec.live_cmake_preset_completion")
{
    // Skip if CMakePresets.json is not in the current directory
    if (!std::filesystem::exists("CMakePresets.json"))
        SKIP("CMakePresets.json not found in current directory");

    auto completer = endo::CommandSpecCompleter {};
    completer.registerCommand(endo::createCmakeSpec(), std::make_unique<endo::CmakeQueryProvider>());

    auto ctx = makeCmakeContext("cmake --preset ");
    auto results = completer.complete(ctx);

    // The project has CMakePresets.json with clang-debug, clang-release, etc.
    CHECK_FALSE(results.empty());
    CHECK(hasCompletion(results, "clang-debug"));
}

TEST_CASE("CmakeSpec.live_ctest_preset_completion")
{
    if (!std::filesystem::exists("CMakePresets.json"))
        SKIP("CMakePresets.json not found in current directory");

    auto completer = endo::CommandSpecCompleter {};
    completer.registerCommand(endo::createCtestSpec(), std::make_unique<endo::CmakeQueryProvider>());

    auto ctx = makeCmakeContext("ctest --preset ", "", "ctest");
    auto results = completer.complete(ctx);

    CHECK_FALSE(results.empty());
    CHECK(hasCompletion(results, "clang-debug"));
}

// ============================================================================
// CmakeQueryProvider: include resolution and condition filtering tests
// ============================================================================

namespace
{

/// @brief RAII helper that creates a temporary directory and removes it on destruction.
struct TmpDir
{
    std::filesystem::path path;

    TmpDir()
    {
        path = std::filesystem::temp_directory_path() / "endo-cmake-test";
        std::filesystem::remove_all(path); // Clean up any prior leftover
        std::filesystem::create_directories(path);
    }

    ~TmpDir() { std::filesystem::remove_all(path); }

    TmpDir(TmpDir const&) = delete;
    TmpDir& operator=(TmpDir const&) = delete;
};

/// @brief Helper to write a string to a file.
void writeFile(std::filesystem::path const& filePath, std::string const& content)
{
    std::filesystem::create_directories(filePath.parent_path());
    auto out = std::ofstream(filePath);
    out << content;
}

/// @brief Runs the CmakeQueryProvider in the given directory and returns preset names.
std::vector<std::string> queryPresetsInDir(std::filesystem::path const& dir)
{
    auto const oldCwd = std::filesystem::current_path();
    std::filesystem::current_path(dir);

    auto provider = endo::CmakeQueryProvider {};
    auto results = provider.query("presets");

    std::filesystem::current_path(oldCwd);

    auto names = std::vector<std::string> {};
    for (auto& r: results)
        names.push_back(std::move(r.text));
    return names;
}

} // namespace

TEST_CASE("CmakeSpec.include_resolution")
{
    auto const tmp = TmpDir {};

    // Child preset file with actual presets
    writeFile(tmp.path / "child-presets.json",
              R"({
        "version": 6,
        "configurePresets": [
            { "name": "child-debug", "displayName": "Child Debug" },
            { "name": "child-release", "displayName": "Child Release" }
        ]
    })");

    // Root preset file includes child, has no presets of its own
    writeFile(tmp.path / "CMakePresets.json",
              R"({
        "version": 6,
        "include": ["child-presets.json"]
    })");

    auto const names = queryPresetsInDir(tmp.path);
    CHECK(std::ranges::find(names, "child-debug") != names.end());
    CHECK(std::ranges::find(names, "child-release") != names.end());
}

TEST_CASE("CmakeSpec.include_nested_resolution")
{
    auto const tmp = TmpDir {};

    // Grandchild preset file
    writeFile(tmp.path / "sub" / "grandchild.json",
              R"({
        "version": 6,
        "configurePresets": [
            { "name": "grandchild-preset" }
        ]
    })");

    // Child includes grandchild (relative to child's directory)
    writeFile(tmp.path / "sub" / "child.json",
              R"({
        "version": 6,
        "include": ["grandchild.json"],
        "configurePresets": [
            { "name": "child-preset" }
        ]
    })");

    // Root includes child in sub/
    writeFile(tmp.path / "CMakePresets.json",
              R"({
        "version": 6,
        "include": ["sub/child.json"]
    })");

    auto const names = queryPresetsInDir(tmp.path);
    CHECK(std::ranges::find(names, "child-preset") != names.end());
    CHECK(std::ranges::find(names, "grandchild-preset") != names.end());
}

TEST_CASE("CmakeSpec.include_cycle_protection")
{
    auto const tmp = TmpDir {};

    // a.json includes b.json, b.json includes a.json
    writeFile(tmp.path / "CMakePresets.json",
              R"({
        "version": 6,
        "include": ["b.json"],
        "configurePresets": [
            { "name": "preset-a" }
        ]
    })");

    writeFile(tmp.path / "b.json",
              R"({
        "version": 6,
        "include": ["CMakePresets.json"],
        "configurePresets": [
            { "name": "preset-b" }
        ]
    })");

    // Should not hang — cycle is detected
    auto const names = queryPresetsInDir(tmp.path);
    CHECK(std::ranges::find(names, "preset-a") != names.end());
    CHECK(std::ranges::find(names, "preset-b") != names.end());
}

TEST_CASE("CmakeSpec.condition_filters_platform")
{
    auto const tmp = TmpDir {};

    writeFile(tmp.path / "CMakePresets.json",
              R"({
        "version": 6,
        "configurePresets": [
            {
                "name": "linux-only",
                "condition": {
                    "type": "equals",
                    "lhs": "${hostSystemName}",
                    "rhs": "Linux"
                }
            },
            {
                "name": "windows-only",
                "condition": {
                    "type": "equals",
                    "lhs": "${hostSystemName}",
                    "rhs": "Windows"
                }
            },
            {
                "name": "macos-only",
                "condition": {
                    "type": "equals",
                    "lhs": "${hostSystemName}",
                    "rhs": "Darwin"
                }
            },
            {
                "name": "unconditional"
            }
        ]
    })");

    auto const names = queryPresetsInDir(tmp.path);

    // Unconditional always shows
    CHECK(std::ranges::find(names, "unconditional") != names.end());

    // Only the current platform's preset should appear
#if defined(__linux__)
    CHECK(std::ranges::find(names, "linux-only") != names.end());
    CHECK(std::ranges::find(names, "windows-only") == names.end());
    CHECK(std::ranges::find(names, "macos-only") == names.end());
#elif defined(__APPLE__)
    CHECK(std::ranges::find(names, "macos-only") != names.end());
    CHECK(std::ranges::find(names, "linux-only") == names.end());
    CHECK(std::ranges::find(names, "windows-only") == names.end());
#elif defined(_WIN32)
    CHECK(std::ranges::find(names, "windows-only") != names.end());
    CHECK(std::ranges::find(names, "linux-only") == names.end());
    CHECK(std::ranges::find(names, "macos-only") == names.end());
#endif
}

TEST_CASE("CmakeSpec.condition_not_equals")
{
    auto const tmp = TmpDir {};

    writeFile(tmp.path / "CMakePresets.json",
              R"({
        "version": 6,
        "configurePresets": [
            {
                "name": "not-windows",
                "condition": {
                    "type": "notEquals",
                    "lhs": "${hostSystemName}",
                    "rhs": "Windows"
                }
            },
            {
                "name": "not-linux",
                "condition": {
                    "type": "notEquals",
                    "lhs": "${hostSystemName}",
                    "rhs": "Linux"
                }
            }
        ]
    })");

    auto const names = queryPresetsInDir(tmp.path);

#if defined(__linux__)
    CHECK(std::ranges::find(names, "not-windows") != names.end());
    CHECK(std::ranges::find(names, "not-linux") == names.end());
#elif defined(__APPLE__)
    CHECK(std::ranges::find(names, "not-windows") != names.end());
    CHECK(std::ranges::find(names, "not-linux") != names.end());
#elif defined(_WIN32)
    CHECK(std::ranges::find(names, "not-windows") == names.end());
    CHECK(std::ranges::find(names, "not-linux") != names.end());
#endif
}

TEST_CASE("CmakeSpec.condition_not_negation")
{
    auto const tmp = TmpDir {};

    writeFile(tmp.path / "CMakePresets.json",
              R"({
        "version": 6,
        "configurePresets": [
            {
                "name": "not-not-linux",
                "condition": {
                    "type": "not",
                    "condition": {
                        "type": "notEquals",
                        "lhs": "${hostSystemName}",
                        "rhs": "Linux"
                    }
                }
            }
        ]
    })");

    auto const names = queryPresetsInDir(tmp.path);

    // not(notEquals Linux) = equals Linux
#if defined(__linux__)
    CHECK(std::ranges::find(names, "not-not-linux") != names.end());
#else
    CHECK(std::ranges::find(names, "not-not-linux") == names.end());
#endif
}

TEST_CASE("CmakeSpec.condition_anyOf_allOf")
{
    auto const tmp = TmpDir {};

    writeFile(tmp.path / "CMakePresets.json",
              R"({
        "version": 6,
        "configurePresets": [
            {
                "name": "linux-or-macos",
                "condition": {
                    "type": "anyOf",
                    "conditions": [
                        { "type": "equals", "lhs": "${hostSystemName}", "rhs": "Linux" },
                        { "type": "equals", "lhs": "${hostSystemName}", "rhs": "Darwin" }
                    ]
                }
            },
            {
                "name": "impossible",
                "condition": {
                    "type": "allOf",
                    "conditions": [
                        { "type": "equals", "lhs": "${hostSystemName}", "rhs": "Linux" },
                        { "type": "equals", "lhs": "${hostSystemName}", "rhs": "Windows" }
                    ]
                }
            }
        ]
    })");

    auto const names = queryPresetsInDir(tmp.path);

    // "impossible" requires both Linux AND Windows — always false
    CHECK(std::ranges::find(names, "impossible") == names.end());

#if defined(__linux__) || defined(__APPLE__)
    CHECK(std::ranges::find(names, "linux-or-macos") != names.end());
#elif defined(_WIN32)
    CHECK(std::ranges::find(names, "linux-or-macos") == names.end());
#endif
}

TEST_CASE("CmakeSpec.condition_inList")
{
    auto const tmp = TmpDir {};

    writeFile(tmp.path / "CMakePresets.json",
              R"({
        "version": 6,
        "configurePresets": [
            {
                "name": "unix-only",
                "condition": {
                    "type": "inList",
                    "string": "${hostSystemName}",
                    "list": ["Linux", "Darwin"]
                }
            }
        ]
    })");

    auto const names = queryPresetsInDir(tmp.path);

#if defined(__linux__) || defined(__APPLE__)
    CHECK(std::ranges::find(names, "unix-only") != names.end());
#elif defined(_WIN32)
    CHECK(std::ranges::find(names, "unix-only") == names.end());
#endif
}

TEST_CASE("CmakeSpec.live_cmake_filters_platform_conditions")
{
    // Verify the actual project CMakePresets.json filters out Windows presets on Linux
    if (!std::filesystem::exists("CMakePresets.json"))
        SKIP("CMakePresets.json not found in current directory");

    auto provider = endo::CmakeQueryProvider {};
    auto results = provider.query("presets");

    auto names = std::vector<std::string> {};
    for (auto& r: results)
        names.push_back(std::move(r.text));

#if defined(__linux__)
    // Linux presets should be present
    CHECK(std::ranges::find(names, "clang-debug") != names.end());
    CHECK(std::ranges::find(names, "gcc-debug") != names.end());
    // Windows presets should be filtered out
    CHECK(std::ranges::find(names, "cl-debug") == names.end());
    CHECK(std::ranges::find(names, "clangcl-debug") == names.end());
#endif
}

// ============================================================================
// ssh / scp completion tests
// ============================================================================

namespace
{

/// @brief Helper to create a CompletionContext for ssh/scp commands.
endo::CompletionContext makeSshContext(std::string fullInput,
                                       std::string prefix = "",
                                       std::string command = "ssh")
{
    auto const cursor = fullInput.size();
    return endo::CompletionContext {
        .type = endo::CompletionContextType::Argument,
        .prefix = std::move(prefix),
        .prefixStart = cursor - prefix.size(),
        .cursorPosition = cursor,
        .command = std::move(command),
        .fullInput = std::move(fullInput),
    };
}

/// @brief Helper to create an Option-type CompletionContext for ssh/scp.
endo::CompletionContext makeSshOptionContext(std::string fullInput,
                                             std::string prefix,
                                             std::string command = "ssh")
{
    auto const cursor = fullInput.size();
    return endo::CompletionContext {
        .type = endo::CompletionContextType::Option,
        .prefix = std::move(prefix),
        .prefixStart = cursor - prefix.size(),
        .cursorPosition = cursor,
        .command = std::move(command),
        .fullInput = std::move(fullInput),
    };
}

/// @brief Mock query provider returning host names for ssh/scp tests.
class MockSshQueryProvider: public endo::CommandQueryProvider
{
  public:
    std::vector<endo::QueryResult> query(std::string_view queryTag) override
    {
        if (queryTag == "hosts")
            return { { "darkleon", "192.168.1.10" },
                     { "webserver", "web.example.com" },
                     { "devbox", "dev.internal" },
                     { "database", "" },
                     { "jumphost", "jump.example.com" } };
        return {};
    }
};

/// @brief Creates a CommandSpecCompleter with ssh + scp specs and mock provider.
endo::CommandSpecCompleter createMockSshCompleter()
{
    auto completer = endo::CommandSpecCompleter {};
    completer.registerCommand(endo::createSshSpec(), std::make_unique<MockSshQueryProvider>());
    completer.registerCommand(endo::createScpSpec(), std::make_unique<MockSshQueryProvider>());
    return completer;
}

} // namespace

TEST_CASE("SshSpec.host_completion")
{
    auto completer = createMockSshCompleter();
    auto ctx = makeSshContext("ssh ");
    auto results = completer.complete(ctx);

    CHECK_FALSE(results.empty());
    CHECK(hasCompletion(results, "darkleon"));
    CHECK(hasCompletion(results, "webserver"));
    CHECK(hasCompletion(results, "devbox"));
    CHECK(hasCompletion(results, "database"));
    CHECK(hasCompletion(results, "jumphost"));
}

TEST_CASE("SshSpec.host_prefix_filters")
{
    auto completer = createMockSshCompleter();
    auto ctx = makeSshContext("ssh dar", "dar");
    auto results = completer.complete(ctx);

    CHECK_FALSE(results.empty());
    CHECK(hasCompletion(results, "darkleon"));
    CHECK_FALSE(hasCompletion(results, "webserver"));
    CHECK_FALSE(hasCompletion(results, "devbox"));
}

TEST_CASE("SshSpec.ssh_options")
{
    auto completer = createMockSshCompleter();
    auto ctx = makeSshOptionContext("ssh -", "-");
    auto results = completer.complete(ctx);

    CHECK_FALSE(results.empty());
    CHECK(hasCompletion(results, "-p"));
    CHECK(hasCompletion(results, "-i"));
    CHECK(hasCompletion(results, "-v"));
    CHECK(hasCompletion(results, "-A"));
    CHECK(hasCompletion(results, "-C"));
    CHECK(hasCompletion(results, "-N"));
}

TEST_CASE("SshSpec.scp_host_completion")
{
    auto completer = createMockSshCompleter();
    auto ctx = makeSshContext("scp ", "", "scp");
    auto results = completer.complete(ctx);

    CHECK_FALSE(results.empty());
    CHECK(hasCompletion(results, "darkleon"));
    CHECK(hasCompletion(results, "webserver"));
}

TEST_CASE("SshSpec.scp_options")
{
    auto completer = createMockSshCompleter();
    auto ctx = makeSshOptionContext("scp -", "-", "scp");
    auto results = completer.complete(ctx);

    CHECK_FALSE(results.empty());
    CHECK(hasCompletion(results, "-r"));
    CHECK(hasCompletion(results, "-P"));
    CHECK(hasCompletion(results, "-v"));
    CHECK(hasCompletion(results, "-C"));
}

// ============================================================================
// SSH config parsing tests (temp files)
// ============================================================================

TEST_CASE("SshSpec.parse_ssh_config")
{
    auto const tmp = TmpDir {};
    auto const sshDir = tmp.path / ".ssh";
    std::filesystem::create_directories(sshDir);

    writeFile(sshDir / "config",
              "Host myserver\n"
              "    HostName 10.0.0.1\n"
              "    User admin\n"
              "\n"
              "Host devbox\n"
              "    HostName dev.example.com\n");

    auto results = std::vector<endo::QueryResult> {};
    auto visited = std::set<std::string> {};
    endo::SshQueryProvider::parseConfigFile(sshDir / "config", results, visited);

    CHECK(results.size() == 2);

    auto hasHost = [&](std::string const& name) {
        return std::ranges::find(results, name, &endo::QueryResult::text) != results.end();
    };
    CHECK(hasHost("myserver"));
    CHECK(hasHost("devbox"));
}

TEST_CASE("SshSpec.parse_multihost_line")
{
    auto const tmp = TmpDir {};
    auto const sshDir = tmp.path / ".ssh";
    std::filesystem::create_directories(sshDir);

    writeFile(sshDir / "config", "Host foo bar baz\n    HostName shared.example.com\n");

    auto results = std::vector<endo::QueryResult> {};
    auto visited = std::set<std::string> {};
    endo::SshQueryProvider::parseConfigFile(sshDir / "config", results, visited);

    CHECK(results.size() == 3);

    auto hasHost = [&](std::string const& name) {
        return std::ranges::find(results, name, &endo::QueryResult::text) != results.end();
    };
    CHECK(hasHost("foo"));
    CHECK(hasHost("bar"));
    CHECK(hasHost("baz"));

    // All three should share the same HostName description
    for (auto const& r: results)
        CHECK(r.description == "shared.example.com");
}

TEST_CASE("SshSpec.skip_wildcard_hosts")
{
    auto const tmp = TmpDir {};
    auto const sshDir = tmp.path / ".ssh";
    std::filesystem::create_directories(sshDir);

    writeFile(sshDir / "config",
              "Host *\n"
              "    ServerAliveInterval 60\n"
              "\n"
              "Host *.example.com\n"
              "    User deploy\n"
              "\n"
              "Host realhost\n"
              "    HostName 10.0.0.5\n"
              "\n"
              "Host test-?\n"
              "    User test\n");

    auto results = std::vector<endo::QueryResult> {};
    auto visited = std::set<std::string> {};
    endo::SshQueryProvider::parseConfigFile(sshDir / "config", results, visited);

    // Only "realhost" should be present — wildcard patterns are skipped
    CHECK(results.size() == 1);
    CHECK(results[0].text == "realhost");
}

TEST_CASE("SshSpec.parse_hostname_description")
{
    auto const tmp = TmpDir {};
    auto const sshDir = tmp.path / ".ssh";
    std::filesystem::create_directories(sshDir);

    writeFile(sshDir / "config",
              "Host myalias\n"
              "    HostName real.server.example.com\n"
              "    Port 2222\n");

    auto results = std::vector<endo::QueryResult> {};
    auto visited = std::set<std::string> {};
    endo::SshQueryProvider::parseConfigFile(sshDir / "config", results, visited);

    REQUIRE(results.size() == 1);
    CHECK(results[0].text == "myalias");
    CHECK(results[0].description == "real.server.example.com");
}

TEST_CASE("SshSpec.include_resolution")
{
    auto const tmp = TmpDir {};
    auto const sshDir = tmp.path / ".ssh";
    std::filesystem::create_directories(sshDir);
    std::filesystem::create_directories(sshDir / "config.d");

    // Main config includes a subdirectory file
    writeFile(sshDir / "config",
              "Host mainhost\n"
              "    HostName main.example.com\n"
              "\n"
              "Include config.d/extra.conf\n");

    writeFile(sshDir / "config.d" / "extra.conf",
              "Host extrahost\n"
              "    HostName extra.example.com\n");

    auto results = std::vector<endo::QueryResult> {};
    auto visited = std::set<std::string> {};
    endo::SshQueryProvider::parseConfigFile(sshDir / "config", results, visited);

    auto hasHost = [&](std::string const& name) {
        return std::ranges::find(results, name, &endo::QueryResult::text) != results.end();
    };
    CHECK(hasHost("mainhost"));
    CHECK(hasHost("extrahost"));
}

TEST_CASE("SshSpec.include_cycle_protection")
{
    auto const tmp = TmpDir {};
    auto const sshDir = tmp.path / ".ssh";
    std::filesystem::create_directories(sshDir);

    // a.conf includes b.conf, b.conf includes a.conf
    writeFile(sshDir / "a.conf",
              "Host host-a\n"
              "    HostName a.example.com\n"
              "\n"
              "Include b.conf\n");

    writeFile(sshDir / "b.conf",
              "Host host-b\n"
              "    HostName b.example.com\n"
              "\n"
              "Include a.conf\n");

    // Should not hang — cycle is detected
    auto results = std::vector<endo::QueryResult> {};
    auto visited = std::set<std::string> {};
    endo::SshQueryProvider::parseConfigFile(sshDir / "a.conf", results, visited);

    auto hasHost = [&](std::string const& name) {
        return std::ranges::find(results, name, &endo::QueryResult::text) != results.end();
    };
    CHECK(hasHost("host-a"));
    CHECK(hasHost("host-b"));
}

TEST_CASE("SshSpec.case_insensitive_directives")
{
    auto const tmp = TmpDir {};
    auto const sshDir = tmp.path / ".ssh";
    std::filesystem::create_directories(sshDir);

    // SSH config directives are case-insensitive
    writeFile(sshDir / "config",
              "host myserver\n"
              "    hostname 10.0.0.1\n"
              "\n"
              "HOST uppercasehost\n"
              "    HOSTNAME upper.example.com\n");

    auto results = std::vector<endo::QueryResult> {};
    auto visited = std::set<std::string> {};
    endo::SshQueryProvider::parseConfigFile(sshDir / "config", results, visited);

    auto hasHost = [&](std::string const& name) {
        return std::ranges::find(results, name, &endo::QueryResult::text) != results.end();
    };
    CHECK(hasHost("myserver"));
    CHECK(hasHost("uppercasehost"));
}

TEST_CASE("SshSpec.comments_and_empty_lines")
{
    auto const tmp = TmpDir {};
    auto const sshDir = tmp.path / ".ssh";
    std::filesystem::create_directories(sshDir);

    writeFile(sshDir / "config",
              "# This is a comment\n"
              "\n"
              "   # Indented comment\n"
              "\n"
              "Host realhost\n"
              "    HostName real.example.com\n"
              "\n"
              "# Another comment\n");

    auto results = std::vector<endo::QueryResult> {};
    auto visited = std::set<std::string> {};
    endo::SshQueryProvider::parseConfigFile(sshDir / "config", results, visited);

    REQUIRE(results.size() == 1);
    CHECK(results[0].text == "realhost");
}

TEST_CASE("SshSpec.live_ssh_host_completion")
{
    auto const home = std::getenv("HOME");
    if (!home)
        SKIP("HOME not set");

    auto const configPath = std::filesystem::path(home) / ".ssh" / "config";
    if (!std::filesystem::exists(configPath))
        SKIP("~/.ssh/config not found");

    auto completer = endo::CommandSpecCompleter {};
    completer.registerCommand(endo::createSshSpec(), std::make_unique<endo::SshQueryProvider>());

    auto ctx = makeSshContext("ssh ");
    auto results = completer.complete(ctx);

    // If user has a non-trivial ssh config, we should get at least one host
    CHECK_FALSE(results.empty());
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

TEST_CASE("CommandSpecCompleter.alias_br_delete_completes_branches")
{
    auto completer = createMockGitCompleter();
    auto ctx = makeGitContext("git br -d ");
    auto results = completer.complete(ctx);

    CHECK_FALSE(results.empty());
    CHECK(hasCompletion(results, "main"));
    CHECK(hasCompletion(results, "develop"));
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
