// SPDX-License-Identifier: Apache-2.0
#include <format>

#include <agent/SystemPromptBuilder.hpp>

namespace endo::agent
{

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

auto SystemPromptBuilder::build() const -> std::string
{
    auto result = std::string(
        "You are an AI assistant integrated into the endo shell. "
        "Help the user with shell commands, programming, system administration, and general questions. "
        "Be concise and practical in your responses.\n");

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

    return result;
}

} // namespace endo::agent
