// SPDX-License-Identifier: Apache-2.0
#include <format>

#include <agent/tools/ShellExecuteTool.hpp>

namespace endo::agent
{

namespace
{
    constexpr auto DefaultTimeoutMs = 120'000;
    constexpr auto MaxTimeoutMs = 600'000;
    constexpr auto MaxOutputSize = size_t { 30'000 };
} // namespace

ShellExecuteTool::ShellExecuteTool(ShellExecuteCallback executeCallback):
    _executeCallback(std::move(executeCallback))
{
}

auto ShellExecuteTool::name() const noexcept -> std::string_view
{
    return "shell_execute";
}

auto ShellExecuteTool::definition() const -> ToolDefinition
{
    return ToolDefinition {
        .name = "shell_execute",
        .description = "Executes a shell command and returns the output (stdout + stderr combined). "
                       "Use this for running build commands, tests, installing packages, etc. "
                       "Output is truncated at 30KB.",
        .inputSchema =
            nlohmann::json {
                { "type", "object" },
                { "properties",
                  { { "command",
                      { { "type", "string" }, { "description", "The shell command to execute" } } },
                    { "timeout_ms",
                      { { "type", "integer" },
                        { "description",
                          "Maximum execution time in milliseconds (default: 120000, max: 600000)" } } } } },
                { "required", nlohmann::json::array({ "command" }) },
            },
    };
}

auto ShellExecuteTool::execute(nlohmann::json const& arguments) -> std::expected<ToolResult, ToolError>
{
    auto const command = arguments.value("command", std::string {});
    if (command.empty())
        return std::unexpected(ToolError { .message = "Missing required parameter: command" });

    auto timeoutMs = arguments.value("timeout_ms", DefaultTimeoutMs);
    timeoutMs = std::clamp(timeoutMs, 1, MaxTimeoutMs);

    auto const timeout = std::chrono::milliseconds(timeoutMs);

    if (!_executeCallback)
        return std::unexpected(ToolError { .message = "Shell execution callback not configured" });

    auto result = _executeCallback(command, timeout);

    auto output = std::move(result.output);

    // Truncate output if it exceeds the maximum size
    auto truncated = false;
    if (output.size() > MaxOutputSize)
    {
        auto const truncatedBytes = output.size() - MaxOutputSize;
        output.resize(MaxOutputSize);
        output += std::format("\n\n[truncated — {} bytes omitted]", truncatedBytes);
        truncated = true;
    }

    if (result.timedOut)
    {
        return ToolResult {
            .content = std::format("Command timed out after {}ms\n{}", timeoutMs, output),
            .isError = true,
        };
    }

    auto content = std::format("Exit code: {}\n{}", result.exitCode, output);

    return ToolResult {
        .content = std::move(content),
        .isError = result.exitCode != 0,
    };
}

} // namespace endo::agent
