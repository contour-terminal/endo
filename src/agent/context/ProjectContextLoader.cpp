// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <cstdlib>
#include <format>
#include <fstream>
#include <sstream>

#include <agent/context/ProjectContextLoader.hpp>
#include <agent/context/ProjectFileTree.hpp>
#include <platform/UserPaths.hpp>

namespace endo::agent
{

namespace
{
    /// Reads the entire contents of a file, or returns empty string on failure.
    [[nodiscard]] auto readFileContents(std::filesystem::path const& path) -> std::string
    {
        auto stream = std::ifstream(path);
        if (!stream.is_open())
            return {};

        auto buffer = std::ostringstream {};
        buffer << stream.rdbuf();
        return buffer.str();
    }

    /// Returns the user's home directory path.
    [[nodiscard]] auto homeDirectory() -> std::filesystem::path
    {
        return platform::homeDirectory().value_or(std::filesystem::path {});
    }

    /// Loads all .md files from a directory, each wrapped with a filename header.
    [[nodiscard]] auto loadMarkdownFiles(std::filesystem::path const& dir) -> std::vector<std::string>
    {
        auto results = std::vector<std::string> {};

        if (!std::filesystem::exists(dir) || !std::filesystem::is_directory(dir))
            return results;

        auto paths = std::vector<std::filesystem::path> {};
        for (auto const& entry: std::filesystem::directory_iterator(dir))
        {
            if (entry.is_regular_file() && entry.path().extension() == ".md")
                paths.push_back(entry.path());
        }

        // Sort for deterministic output
        std::ranges::sort(paths);

        for (auto const& path: paths)
        {
            auto content = readFileContents(path);
            if (!content.empty())
                results.push_back(std::format("### {}\n{}", path.filename().string(), content));
        }

        return results;
    }
} // namespace

auto ProjectContextLoader::load(std::filesystem::path const& projectRoot) -> ProjectContext
{
    auto context = ProjectContext {};

    // Generate file tree and collect flat file paths for @-mention completion
    auto treeGen = ProjectFileTree {};
    context.fileTree = treeGen.generate(projectRoot);
    context.filePaths = treeGen.filePaths(projectRoot);

    // Load project rules
    context.rulesFiles = loadRulesFiles(projectRoot);

    // Load global rules
    context.globalRules = loadGlobalRules();

    // Load memory files
    context.memoryFiles = loadMemoryFiles();

    return context;
}

auto ProjectContextLoader::loadRulesFiles(std::filesystem::path const& root) -> std::vector<std::string>
{
    auto results = std::vector<std::string> {};

    // Search order: CLAUDE.md, AGENT.md, .endo/agent-rules.md
    auto const candidates = std::vector<std::filesystem::path> {
        root / "CLAUDE.md",
        root / "AGENT.md",
        root / ".endo" / "agent-rules.md",
    };

    for (auto const& path: candidates)
    {
        if (std::filesystem::exists(path) && std::filesystem::is_regular_file(path))
        {
            auto content = readFileContents(path);
            if (!content.empty())
                results.push_back(
                    std::format("### {}\n{}", std::filesystem::relative(path, root).string(), content));
        }
    }

    return results;
}

auto ProjectContextLoader::loadGlobalRules() -> std::vector<std::string>
{
    auto const home = homeDirectory();
    if (home.empty())
        return {};

    return loadMarkdownFiles(home / ".config" / "endo" / "agent-rules");
}

auto ProjectContextLoader::loadMemoryFiles() -> std::vector<std::string>
{
    auto const home = homeDirectory();
    if (home.empty())
        return {};

    return loadMarkdownFiles(home / ".config" / "endo" / "agent-memory");
}

} // namespace endo::agent
