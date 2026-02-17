// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>

namespace endo::agent
{

/// Builds a system prompt string from shell environment context.
///
/// The system prompt gives the LLM information about the user's environment
/// (working directory, git branch, shell) so it can provide contextual responses.
class SystemPromptBuilder
{
  public:
    SystemPromptBuilder() = default;

    /// @brief Sets the current working directory.
    /// @param cwd The absolute path of the current working directory.
    void setWorkingDirectory(std::string cwd);

    /// @brief Sets the current git branch name.
    /// @param branch The git branch name (empty if not in a git repo).
    void setGitBranch(std::string branch);

    /// @brief Sets a short git status summary.
    /// @param status The git status (e.g. "clean", "3 modified").
    void setGitStatus(std::string status);

    /// @brief Sets the shell information string.
    /// @param info The shell name and version (e.g. "endo 0.1").
    void setShellInfo(std::string info);

    /// @brief Assembles and returns the complete system prompt.
    /// @return The built system prompt string.
    [[nodiscard]] auto build() const -> std::string;

  private:
    std::string _workingDirectory;
    std::string _gitBranch;
    std::string _gitStatus;
    std::string _shellInfo;
};

} // namespace endo::agent
