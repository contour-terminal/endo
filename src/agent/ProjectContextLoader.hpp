// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace endo::agent
{

/// Collected project context information for system prompt assembly.
struct ProjectContext
{
    std::string fileTree;                 ///< Condensed file tree of the project.
    std::vector<std::string> rulesFiles;  ///< Contents of project rules files (CLAUDE.md, AGENT.md, etc.).
    std::vector<std::string> memoryFiles; ///< Contents of agent memory files.
    std::vector<std::string> globalRules; ///< Contents of global agent rules files.
};

/// Loads project context from the filesystem for system prompt assembly.
///
/// Searches for project rules files in the project root, global rules in
/// ~/.config/endo/agent-rules/, and memory files in ~/.config/endo/agent-memory/.
/// Also generates a condensed file tree of the project.
class ProjectContextLoader
{
  public:
    /// @brief Loads all available project context for the given project root.
    /// @param projectRoot The root directory of the current project.
    /// @return The collected project context.
    [[nodiscard]] auto load(std::filesystem::path const& projectRoot) const -> ProjectContext;

  private:
    /// Loads project rules files from the project root.
    /// Search order: CLAUDE.md, AGENT.md, .endo/agent-rules.md
    [[nodiscard]] auto loadRulesFiles(std::filesystem::path const& root) const -> std::vector<std::string>;

    /// Loads global agent rules from ~/.config/endo/agent-rules/*.md
    [[nodiscard]] auto loadGlobalRules() const -> std::vector<std::string>;

    /// Loads agent memory files from ~/.config/endo/agent-memory/*.md
    [[nodiscard]] auto loadMemoryFiles() const -> std::vector<std::string>;
};

} // namespace endo::agent
