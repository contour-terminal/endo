// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace endo::platform
{

/// Configuration for project file tree generation.
struct FileTreeConfig
{
    size_t maxDepth = 4;          ///< Maximum directory depth to traverse.
    size_t maxEntries = 200;      ///< Maximum total file/directory entries to include.
    bool respectGitignore = true; ///< Whether to use git ls-files in git repositories.
};

/// Generates a condensed file tree representation of a project directory.
///
/// In git repositories, uses `git ls-files` to respect .gitignore rules.
/// Falls back to filesystem traversal for non-git directories.
class ProjectFileTree
{
  public:
    /// @brief Constructs a file tree generator with the given configuration.
    /// @param config The file tree generation settings.
    explicit ProjectFileTree(FileTreeConfig config = {});

    /// @brief Generates an indented file tree string for the given root directory.
    /// @param rootPath The project root directory to generate a tree for.
    /// @return An indented text representation of the file tree.
    [[nodiscard]] auto generate(std::filesystem::path const& rootPath) const -> std::string;

    /// @brief Returns the flat list of relative file paths (no tree formatting).
    ///
    /// Uses `git ls-files` for git repositories, or filesystem traversal otherwise.
    /// Uses a higher entry limit than the tree display for comprehensive @-mention completion.
    ///
    /// @param rootPath The project root directory.
    /// @return Sorted vector of relative file path strings.
    [[nodiscard]] auto filePaths(std::filesystem::path const& rootPath) const -> std::vector<std::string>;

  private:
    /// Checks whether the given path is inside a git repository.
    [[nodiscard]] static auto isGitRepo(std::filesystem::path const& path) -> bool;

    /// Gets the list of tracked and untracked files from git.
    [[nodiscard]] auto getGitTrackedFiles(std::filesystem::path const& path) const
        -> std::vector<std::string>;

    /// Builds an indented tree string from a sorted list of relative file paths.
    [[nodiscard]] auto buildTreeFromPaths(std::vector<std::string> const& paths) const -> std::string;

    /// Scans the directory using filesystem traversal (non-git fallback).
    [[nodiscard]] auto scanDirectory(std::filesystem::path const& path) const -> std::string;

    FileTreeConfig _config;
};

} // namespace endo::platform
