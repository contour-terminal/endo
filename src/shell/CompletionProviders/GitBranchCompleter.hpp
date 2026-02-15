// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <shell/CompletionProvider.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace endo
{

/// @brief Completion provider for git branch names.
///
/// Activates when the command is `git` and the subcommand expects a branch argument
/// (e.g., `checkout`, `switch`, `merge`, `rebase`). Queries local and remote branches
/// via `git branch` and applies fuzzy scoring against the current prefix.
class GitBranchCompleter: public CompletionProvider
{
  public:
    [[nodiscard]] std::vector<CompletionItem> complete(CompletionContext const& context) override;
    [[nodiscard]] bool canHandle(CompletionContextType type) const override;

    /// @brief Priority 85 — higher than FileCompleter (50), lower than CommandCompleter (100).
    [[nodiscard]] int priority() const override { return 85; }

  private:
    /// @brief Parsed information about the git command line up to the cursor.
    struct GitCommandInfo
    {
        std::string subcommand;        ///< The git subcommand (e.g., "checkout", "push").
        std::vector<std::string> args; ///< Non-option args before cursor (excluding subcommand).
        bool hasDeleteFlag = false;    ///< -d, -D, or --delete present.
        bool hasMoveFlag = false;      ///< -m, -M, or --move present.
    };

    /// @brief Parses the git command line to extract subcommand and argument context.
    /// @param fullInput The complete input line.
    /// @param cursorPosition The cursor byte offset.
    /// @return Parsed git command info, or nullopt if not parseable.
    [[nodiscard]] static std::optional<GitCommandInfo> parseGitCommand(std::string_view fullInput,
                                                                       size_t cursorPosition);

    /// @brief Checks whether the current argument position expects a branch name.
    /// @param info The parsed git command info.
    /// @return true if a branch name is expected.
    [[nodiscard]] static bool expectsBranchAtPosition(GitCommandInfo const& info);

    /// @brief Queries local and remote branch names from git.
    /// @return Sorted, deduplicated list of branch names.
    [[nodiscard]] static std::vector<std::string> queryBranches();
};

} // namespace endo
