// SPDX-License-Identifier: Apache-2.0
#include <format>

#include <agent/SystemPromptBuilder.hpp>

namespace endo::agent
{

namespace
{
    constexpr auto DefaultBaseInstructions =
        "You are an AI assistant integrated into the endo shell. "
        "Help the user with shell commands, programming, system administration, and general questions. "
        "Be concise and practical in your responses.";
} // namespace

void SystemPromptBuilder::setWorkingDirectory(std::string cwd)
{
    _workingDirectory = std::move(cwd);
}

void SystemPromptBuilder::setGitBranch(std::string branch)
{
    _gitBranch = std::move(branch);
}

void SystemPromptBuilder::setGitStatus(std::string status)
{
    _gitStatus = std::move(status);
}

void SystemPromptBuilder::setShellInfo(std::string info)
{
    _shellInfo = std::move(info);
}

void SystemPromptBuilder::setBaseInstructions(std::string instructions)
{
    _baseInstructions = std::move(instructions);
}

void SystemPromptBuilder::setGlobalRules(std::vector<std::string> rules)
{
    _globalRules = std::move(rules);
}

void SystemPromptBuilder::setProjectRules(std::vector<std::string> rules)
{
    _projectRules = std::move(rules);
}

void SystemPromptBuilder::setFileTree(std::string fileTree)
{
    _fileTree = std::move(fileTree);
}

void SystemPromptBuilder::setMemoryFiles(std::vector<std::string> memory)
{
    _memoryFiles = std::move(memory);
}

auto SystemPromptBuilder::build() const -> std::string
{
    auto result = std::string {};

    // Base instructions
    if (!_baseInstructions.empty())
        result += _baseInstructions;
    else
        result += DefaultBaseInstructions;
    result += '\n';

    // Global rules section
    if (!_globalRules.empty())
    {
        result += "\n## Global Rules\n";
        for (auto i = size_t { 0 }; i < _globalRules.size(); ++i)
        {
            if (i > 0)
                result += "\n---\n";
            result += _globalRules[i];
            if (!_globalRules[i].empty() && _globalRules[i].back() != '\n')
                result += '\n';
        }
    }

    // Project rules section
    if (!_projectRules.empty())
    {
        result += "\n## Project Rules\n";
        for (auto i = size_t { 0 }; i < _projectRules.size(); ++i)
        {
            if (i > 0)
                result += "\n---\n";
            result += _projectRules[i];
            if (!_projectRules[i].empty() && _projectRules[i].back() != '\n')
                result += '\n';
        }
    }

    // Environment section
    result += "\n## Environment\n";

    if (!_workingDirectory.empty())
        result += std::format("- Working directory: {}\n", _workingDirectory);

    if (!_gitBranch.empty())
    {
        result += std::format("- Git branch: {}\n", _gitBranch);
        if (!_gitStatus.empty())
            result += std::format("- Git status: {}\n", _gitStatus);
    }

    if (!_shellInfo.empty())
        result += std::format("- Shell: {}\n", _shellInfo);

    // Project structure section
    if (!_fileTree.empty())
    {
        result += "\n## Project Structure\n";
        result += _fileTree;
        if (!_fileTree.empty() && _fileTree.back() != '\n')
            result += '\n';
    }

    // Agent memory section
    if (!_memoryFiles.empty())
    {
        result += "\n## Agent Memory\n";
        for (auto const& memory: _memoryFiles)
        {
            result += memory;
            if (!memory.empty() && memory.back() != '\n')
                result += '\n';
        }
    }

    return result;
}

} // namespace endo::agent
