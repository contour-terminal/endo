// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_test_macros.hpp>

#include "CompletionProviders/GitBranchCompleter.hpp"

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

/// @brief Helper to check if a specific text is in the completions.
bool hasCompletion(std::vector<tui::CompletionItem> const& items, std::string const& text)
{
    for (auto const& item: items)
        if (item.text == text)
            return true;
    return false;
}

} // namespace

// ============================================================================
// canHandle tests
// ============================================================================

TEST_CASE("GitBranchCompleter.canHandle_argument_only")
{
    endo::GitBranchCompleter completer;

    CHECK(completer.canHandle(endo::CompletionContextType::Argument));
    CHECK_FALSE(completer.canHandle(endo::CompletionContextType::Command));
    CHECK_FALSE(completer.canHandle(endo::CompletionContextType::FilePath));
    CHECK_FALSE(completer.canHandle(endo::CompletionContextType::Variable));
    CHECK_FALSE(completer.canHandle(endo::CompletionContextType::Option));
}

TEST_CASE("GitBranchCompleter.priority")
{
    endo::GitBranchCompleter completer;
    CHECK(completer.priority() == 85);
}

// ============================================================================
// Guard: non-git commands
// ============================================================================

TEST_CASE("GitBranchCompleter.non_git_command_returns_empty")
{
    endo::GitBranchCompleter completer;
    auto ctx = makeGitContext("ls -la ", "", "ls");
    auto results = completer.complete(ctx);
    CHECK(results.empty());
}

// ============================================================================
// Guard: no subcommand
// ============================================================================

TEST_CASE("GitBranchCompleter.no_subcommand_returns_empty")
{
    endo::GitBranchCompleter completer;
    auto ctx = makeGitContext("git ");
    auto results = completer.complete(ctx);
    CHECK(results.empty());
}

// ============================================================================
// Checkout: always suggests branches
// ============================================================================

TEST_CASE("GitBranchCompleter.checkout_suggests_branches")
{
    endo::GitBranchCompleter completer;
    auto ctx = makeGitContext("git checkout ");
    auto results = completer.complete(ctx);

    // We're running in a git repo, so there should be at least one branch
    CHECK_FALSE(results.empty());
    CHECK(hasCompletion(results, "master"));
}

TEST_CASE("GitBranchCompleter.checkout_prefix_filters")
{
    endo::GitBranchCompleter completer;
    auto ctx = makeGitContext("git checkout mas", "mas");
    auto results = completer.complete(ctx);

    CHECK_FALSE(results.empty());
    CHECK(hasCompletion(results, "master"));
}

// ============================================================================
// Switch: always suggests branches
// ============================================================================

TEST_CASE("GitBranchCompleter.switch_suggests_branches")
{
    endo::GitBranchCompleter completer;
    auto ctx = makeGitContext("git switch ");
    auto results = completer.complete(ctx);

    CHECK_FALSE(results.empty());
    CHECK(hasCompletion(results, "master"));
}

// ============================================================================
// Merge: always suggests branches
// ============================================================================

TEST_CASE("GitBranchCompleter.merge_suggests_branches")
{
    endo::GitBranchCompleter completer;
    auto ctx = makeGitContext("git merge ");
    auto results = completer.complete(ctx);

    CHECK_FALSE(results.empty());
}

// ============================================================================
// Push: needs remote argument first
// ============================================================================

TEST_CASE("GitBranchCompleter.push_needs_remote_first")
{
    endo::GitBranchCompleter completer;
    auto ctx = makeGitContext("git push ");
    auto results = completer.complete(ctx);

    CHECK(results.empty());
}

TEST_CASE("GitBranchCompleter.push_after_remote_suggests")
{
    endo::GitBranchCompleter completer;
    auto ctx = makeGitContext("git push origin ");
    auto results = completer.complete(ctx);

    CHECK_FALSE(results.empty());
}

// ============================================================================
// Unknown subcommand
// ============================================================================

TEST_CASE("GitBranchCompleter.unknown_subcommand_returns_empty")
{
    endo::GitBranchCompleter completer;
    auto ctx = makeGitContext("git config ");
    auto results = completer.complete(ctx);

    CHECK(results.empty());
}

// ============================================================================
// Branch with flags
// ============================================================================

TEST_CASE("GitBranchCompleter.branch_delete_suggests")
{
    endo::GitBranchCompleter completer;
    auto ctx = makeGitContext("git branch -d ");
    auto results = completer.complete(ctx);

    CHECK_FALSE(results.empty());
}

TEST_CASE("GitBranchCompleter.branch_without_flag_empty")
{
    endo::GitBranchCompleter completer;
    auto ctx = makeGitContext("git branch ");
    auto results = completer.complete(ctx);

    CHECK(results.empty());
}

TEST_CASE("GitBranchCompleter.branch_move_suggests")
{
    endo::GitBranchCompleter completer;
    auto ctx = makeGitContext("git branch -m ");
    auto results = completer.complete(ctx);

    CHECK_FALSE(results.empty());
}

// ============================================================================
// Rebase: always suggests branches
// ============================================================================

TEST_CASE("GitBranchCompleter.rebase_suggests_branches")
{
    endo::GitBranchCompleter completer;
    auto ctx = makeGitContext("git rebase ");
    auto results = completer.complete(ctx);

    CHECK_FALSE(results.empty());
}

// ============================================================================
// Options before subcommand
// ============================================================================

TEST_CASE("GitBranchCompleter.options_before_subcommand")
{
    endo::GitBranchCompleter completer;
    // git -C /some/path checkout <tab>
    auto ctx = makeGitContext("git -C /some/path checkout ");
    auto results = completer.complete(ctx);

    // -C and /some/path are options/option-args, "checkout" is the subcommand
    // This test verifies parsing skips options correctly
    CHECK_FALSE(results.empty());
}
