// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <array>
#include <cstdio>
#include <format>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <unordered_set>

#include <platform/ProjectFileTree.hpp>

namespace endo::platform
{

namespace
{
    /// Runs a command and captures its stdout output.
    [[nodiscard]] auto runCommand(std::string const& command) -> std::string
    {
#if defined(_WIN32)
        auto* pipe = _popen(command.c_str(), "r");
#else
        auto* pipe = popen(command.c_str(), "r");
#endif
        if (!pipe)
            return {};

        auto output = std::string {};
        auto buffer = std::array<char, 4096> {};
        while (auto const bytesRead = fread(buffer.data(), 1, buffer.size(), pipe))
            output.append(buffer.data(), bytesRead);

#if defined(_WIN32)
        _pclose(pipe);
#else
        pclose(pipe);
#endif

        // Trim trailing newline
        while (!output.empty() && output.back() == '\n')
            output.pop_back();

        return output;
    }
} // namespace

ProjectFileTree::ProjectFileTree(FileTreeConfig config): _config(config)
{
}

auto ProjectFileTree::generate(std::filesystem::path const& rootPath) const -> std::string
{
    if (_config.respectGitignore && isGitRepo(rootPath))
    {
        auto files = getGitTrackedFiles(rootPath);
        if (!files.empty())
            return buildTreeFromPaths(files);
    }
    return scanDirectory(rootPath);
}

auto ProjectFileTree::filePaths(std::filesystem::path const& rootPath) const -> std::vector<std::string>
{
    static constexpr size_t FilePathLimit = 5000;

    auto files = std::vector<std::string> {};

    if (_config.respectGitignore && isGitRepo(rootPath))
    {
        files = getGitTrackedFiles(rootPath);
        if (files.size() > FilePathLimit)
            files.resize(FilePathLimit);

        // Also include root-level files that may be excluded via .git/info/exclude
        // but are still useful navigation targets (e.g. TODO.md, CLAUDE.md).
        auto const fileSet = std::unordered_set<std::string>(files.begin(), files.end());
        for (auto const& entry: std::filesystem::directory_iterator(
                 rootPath, std::filesystem::directory_options::skip_permission_denied))
        {
            if (files.size() >= FilePathLimit)
                break;
            if (!entry.is_regular_file())
                continue;
            auto const name = entry.path().filename().string();
            if (name.starts_with('.'))
                continue;
            if (!fileSet.contains(name))
                files.push_back(name);
        }
    }
    else
    {
        // Non-git fallback: collect paths with filesystem traversal
        try
        {
            auto const options = std::filesystem::directory_options::skip_permission_denied;
            for (auto const& entry: std::filesystem::recursive_directory_iterator(rootPath, options))
            {
                if (files.size() >= FilePathLimit)
                    break;

                if (!entry.is_regular_file())
                    continue;

                auto const relPath = std::filesystem::relative(entry.path(), rootPath);
                auto const firstComponent = relPath.begin()->string();
                if (firstComponent.starts_with('.') && firstComponent != ".")
                    continue;
                if (firstComponent == "build" || firstComponent == "node_modules")
                    continue;

                files.push_back(relPath.generic_string());
            }
        }
        catch (std::filesystem::filesystem_error const&) // NOLINT(bugprone-empty-catch)
        {
            // Permission denied or other FS errors — return what we have
        }
    }

    // Extract unique directory paths from file paths (with trailing '/')
    auto dirs = std::set<std::string> {};
    for (auto const& filePath: files)
    {
        auto p = std::filesystem::path(filePath);
        for (auto parent = p.parent_path(); !parent.empty(); parent = parent.parent_path())
            dirs.insert(parent.generic_string() + "/");
    }

    // Merge directories into the result
    files.insert(files.end(), dirs.begin(), dirs.end());
    std::ranges::sort(files);
    return files;
}

auto ProjectFileTree::isGitRepo(std::filesystem::path const& path) -> bool
{
    auto const result =
        runCommand(std::format("git -C \"{}\" rev-parse --is-inside-work-tree 2>/dev/null", path.string()));
    return result == "true";
}

auto ProjectFileTree::getGitTrackedFiles(std::filesystem::path const& path) const -> std::vector<std::string>
{
    auto const output = runCommand(std::format(
        "git -C \"{}\" ls-files --cached --others --exclude-standard 2>/dev/null", path.string()));

    auto files = std::vector<std::string> {};
    auto stream = std::istringstream(output);
    auto line = std::string {};
    while (std::getline(stream, line))
    {
        if (!line.empty())
            files.push_back(line);
    }

    // Sort for consistent output
    std::ranges::sort(files);

    // Limit total entries
    if (files.size() > _config.maxEntries)
        files.resize(_config.maxEntries);

    return files;
}

auto ProjectFileTree::buildTreeFromPaths(std::vector<std::string> const& paths) const -> std::string
{
    // Build a tree structure from paths using a nested map
    struct TreeNode
    {
        std::map<std::string, TreeNode> children;
        bool isFile = false;
    };

    auto root = TreeNode {};
    auto totalEntries = size_t { 0 };

    for (auto const& filePath: paths)
    {
        if (totalEntries >= _config.maxEntries)
            break;

        auto* current = &root;
        auto const p = std::filesystem::path(filePath);
        auto parts = std::vector<std::string> {};

        for (auto const& part: p)
            parts.push_back(part.generic_string());

        // Apply depth limit
        if (parts.size() > _config.maxDepth + 1) // +1 because the file itself counts
            continue;

        for (auto i = size_t { 0 }; i < parts.size(); ++i)
        {
            auto& child = current->children[parts[i]];
            if (i == parts.size() - 1)
                child.isFile = true;
            current = &child;
        }
        ++totalEntries;
    }

    // Render the tree as indented text
    auto result = std::string {};
    auto truncatedDirs = std::map<std::string, size_t> {};

    std::function<void(TreeNode const&, size_t)> renderTree = [&](TreeNode const& node, size_t depth) {
        for (auto const& [name, child]: node.children)
        {
            result += std::string(depth * 2, ' ');
            if (child.isFile && child.children.empty())
            {
                result += name;
                result += '\n';
            }
            else
            {
                result += name;
                result += "/\n";
                if (depth < _config.maxDepth)
                    renderTree(child, depth + 1);
            }
        }
    };

    renderTree(root, 0);
    return result;
}

auto ProjectFileTree::scanDirectory(std::filesystem::path const& path) const -> std::string
{
    auto paths = std::vector<std::string> {};

    try
    {
        auto const options = std::filesystem::directory_options::skip_permission_denied;
        for (auto const& entry: std::filesystem::recursive_directory_iterator(path, options))
        {
            // Only include regular files — directories are implied by file paths
            if (!entry.is_regular_file())
                continue;

            auto const relPath = std::filesystem::relative(entry.path(), path);
            auto const depth = static_cast<size_t>(std::distance(relPath.begin(), relPath.end()));

            if (depth > _config.maxDepth + 1) // +1 because the file counts as one level
                continue;

            // Skip hidden directories and common build artifacts
            auto const firstComponent = relPath.begin()->string();
            if (firstComponent.starts_with('.') && firstComponent != ".")
                continue;
            if (firstComponent == "build" || firstComponent == "node_modules")
                continue;

            paths.push_back(relPath.generic_string());
        }
    }
    catch (std::filesystem::filesystem_error const&) // NOLINT(bugprone-empty-catch)
    {
        // Permission denied or other FS errors — return what we have
    }

    // Sort before truncating so the surviving entries are deterministic and not
    // dependent on the (unspecified) filesystem iteration order — matching the
    // sort-then-resize behavior of getGitTrackedFiles().
    std::ranges::sort(paths);
    if (paths.size() > _config.maxEntries)
        paths.resize(_config.maxEntries);

    return buildTreeFromPaths(paths);
}

} // namespace endo::platform
