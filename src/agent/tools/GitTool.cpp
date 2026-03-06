// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <format>
#include <set>
#include <string>
#include <vector>

#include <agent/tools/GitTool.hpp>

namespace endo::agent
{

namespace
{
    constexpr auto MaxOutputSize = size_t { 30'000 };
    constexpr auto DefaultTimeout = std::chrono::milliseconds { 30'000 };

    /// Subcommands that are read-only and safe to auto-approve.
    auto const ReadOnlySubcommands = std::set<std::string> {
        "status", "diff", "log", "branch", "show", "rev-parse", "blame", "tag", "remote", "stash",
    };

    /// Checks whether a git command string contains blocked dangerous patterns.
    auto isBlockedPattern(std::string_view subcommand, std::vector<std::string> const& args) -> bool
    {
        // Block force push
        if (subcommand == "push")
        {
            if (std::ranges::any_of(args, [](auto const& arg) { return arg == "--force" || arg == "-f"; }))
                return true;
        }

        // Block hard reset
        if (subcommand == "reset")
        {
            if (std::ranges::any_of(args, [](auto const& arg) { return arg == "--hard"; }))
                return true;
        }

        // Block forced clean
        if (subcommand == "clean")
        {
            if (std::ranges::any_of(args, [](auto const& arg) { return arg == "-f" || arg == "--force"; }))
                return true;
        }

        // Block forced branch delete
        if (subcommand == "branch")
        {
            if (std::ranges::any_of(args, [](auto const& arg) { return arg == "-D"; }))
                return true;
        }

        return false;
    }

} // namespace

GitTool::GitTool(ShellExecuteCallback executeCallback): _executeCallback(std::move(executeCallback))
{
}

auto GitTool::name() const noexcept -> std::string_view
{
    return "git";
}

auto GitTool::definition() const -> ToolDefinition
{
    return ToolDefinition {
        .name = "git",
        .description = "Executes git operations. Read-only commands (status, diff, log, branch, show, "
                       "rev-parse, blame) are auto-approved. Dangerous patterns (push --force, "
                       "reset --hard, clean -f, branch -D) are blocked.",
        .inputSchema =
            nlohmann::json {
                { "type", "object" },
                { "properties",
                  { { "subcommand",
                      { { "type", "string" },
                        { "description", R"(The git subcommand (e.g. "status", "diff"))" } } },
                    { "args",
                      { { "type", "array" },
                        { "items", { { "type", "string" } } },
                        { "description", "Additional arguments for the subcommand" } } } } },
                { "required", nlohmann::json::array({ "subcommand" }) },
            },
    };
}

auto GitTool::execute(nlohmann::json const& arguments) -> std::expected<ToolResult, ToolError>
{
    auto const subcommand = arguments.value("subcommand", std::string {});
    if (subcommand.empty())
        return std::unexpected(ToolError { .message = "Missing required parameter: subcommand" });

    auto args = std::vector<std::string> {};
    if (arguments.contains("args") && arguments["args"].is_array())
    {
        for (auto const& arg: arguments["args"])
        {
            if (arg.is_string())
                args.push_back(arg.get<std::string>());
        }
    }

    // Check for blocked patterns
    if (isBlockedPattern(subcommand, args))
    {
        return ToolResult {
            .content =
                std::format("Blocked: git {} is a destructive operation that is not allowed.", subcommand),
            .isError = true,
        };
    }

    // Build the git command
    auto command = std::string { "git " };
    command += subcommand;
    for (auto const& arg: args)
    {
        command += ' ';
        // Simple quoting for arguments with spaces
        if (arg.find(' ') != std::string::npos)
        {
            command += '\'';
            command += arg;
            command += '\'';
        }
        else
        {
            command += arg;
        }
    }

    if (!_executeCallback)
        return std::unexpected(ToolError { .message = "Shell execution callback not configured" });

    auto result = _executeCallback(command, DefaultTimeout);

    auto output = std::move(result.output);

    // Truncate output if too large
    if (output.size() > MaxOutputSize)
    {
        auto const truncatedBytes = output.size() - MaxOutputSize;
        output.resize(MaxOutputSize);
        output += std::format("\n\n[truncated — {} bytes omitted]", truncatedBytes);
    }

    if (result.timedOut)
    {
        return ToolResult {
            .content = std::format("Git command timed out\n{}", output),
            .isError = true,
        };
    }

    return ToolResult {
        .content = std::format("Exit code: {}\n{}", result.exitCode, output),
        .isError = result.exitCode != 0,
    };
}

auto GitTool::classifyRisk(nlohmann::json const& arguments) const -> ToolRisk
{
    auto const subcommand = arguments.value("subcommand", std::string {});

    // Blocked patterns are handled by execute() — classify as Destructive here.
    auto args = std::vector<std::string> {};
    if (arguments.contains("args") && arguments["args"].is_array())
    {
        for (auto const& arg: arguments["args"])
        {
            if (arg.is_string())
                args.push_back(arg.get<std::string>());
        }
    }
    if (isBlockedPattern(subcommand, args))
        return ToolRisk::Blocked;

    // Read-only subcommands.
    if (ReadOnlySubcommands.contains(subcommand))
        return ToolRisk::ReadOnly;

    // Push is destructive (affects remote state).
    if (subcommand == "push")
        return ToolRisk::Destructive;

    // Everything else is mutating.
    return ToolRisk::Mutating;
}

} // namespace endo::agent
