// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>

namespace endo::agent
{

/// Builds a system prompt string from shell environment context and project information.
///
/// The system prompt gives the LLM information about the user's environment
/// (working directory, git branch, shell), project structure, rules, and
/// agent memory so it can provide contextual responses.
///
/// Sections are omitted when empty, maintaining backward compatibility.
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

    /// @brief Sets custom base instructions, replacing the built-in default.
    /// @param instructions The base instruction text for the agent.
    void setBaseInstructions(std::string instructions);

    /// @brief Sets global agent rules (from ~/.config/endo/agent-rules/*.md).
    /// @param rules List of global rule file contents.
    void setGlobalRules(std::vector<std::string> rules);

    /// @brief Sets project-level rules (from CLAUDE.md, AGENT.md, etc.).
    /// @param rules List of project rule file contents.
    void setProjectRules(std::vector<std::string> rules);

    /// @brief Sets the condensed project file tree.
    /// @param fileTree The indented file tree string.
    void setFileTree(std::string fileTree);

    /// @brief Sets agent memory file contents.
    /// @param memory List of memory file contents.
    void setMemoryFiles(std::vector<std::string> memory);

    /// @brief Assembles and returns the complete system prompt.
    /// @return The built system prompt string.
    [[nodiscard]] auto build() const -> std::string;

  private:
    std::string _workingDirectory;
    std::string _gitBranch;
    std::string _gitStatus;
    std::string _shellInfo;
    std::string _baseInstructions;
    std::vector<std::string> _globalRules;
    std::vector<std::string> _projectRules;
    std::string _fileTree;
    std::vector<std::string> _memoryFiles;
};

} // namespace endo::agent
