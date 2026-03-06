// SPDX-License-Identifier: Apache-2.0
#include <format>

#include <agent/tools/EndoExecuteTool.hpp>

namespace endo::agent
{

namespace
{
    constexpr auto DefaultTimeoutMs = 120'000;
    constexpr auto MaxTimeoutMs = 600'000;
    constexpr auto MaxOutputSize = size_t { 30'000 };
} // namespace

EndoExecuteTool::EndoExecuteTool(EndoExecuteCallback executeCallback):
    _executeCallback(std::move(executeCallback))
{
}

auto EndoExecuteTool::name() const noexcept -> std::string_view
{
    return "endo_execute";
}

auto EndoExecuteTool::definition() const -> ToolDefinition
{
    return ToolDefinition {
        .name = "endo_execute",
        .description = "Evaluates endo source code directly and returns the captured output. "
                       "Use this to run F#-style endo expressions, test language features, "
                       "or perform computations using the endo language. "
                       "Output is truncated at 30KB.",
        .inputSchema =
            nlohmann::json {
                { "type", "object" },
                { "properties",
                  { { "source",
                      { { "type", "string" }, { "description", "The endo source code to evaluate" } } },
                    { "timeout_ms",
                      { { "type", "integer" },
                        { "description",
                          "Maximum execution time in milliseconds (default: 120000, max: 600000)" } } } } },
                { "required", nlohmann::json::array({ "source" }) },
            },
    };
}

auto EndoExecuteTool::execute(nlohmann::json const& arguments) -> std::expected<ToolResult, ToolError>
{
    auto const source = arguments.value("source", std::string {});
    if (source.empty())
        return std::unexpected(ToolError { .message = "Missing required parameter: source" });

    auto timeoutMs = arguments.value("timeout_ms", DefaultTimeoutMs);
    timeoutMs = std::clamp(timeoutMs, 1, MaxTimeoutMs);

    auto const timeout = std::chrono::milliseconds(timeoutMs);

    if (!_executeCallback)
        return std::unexpected(ToolError { .message = "Endo execution callback not configured" });

    auto result = _executeCallback(source, timeout);

    auto output = std::move(result.output);

    // Truncate output if it exceeds the maximum size
    if (output.size() > MaxOutputSize)
    {
        auto const truncatedBytes = output.size() - MaxOutputSize;
        output.resize(MaxOutputSize);
        output += std::format("\n\n[truncated — {} bytes omitted]", truncatedBytes);
    }

    if (result.timedOut)
    {
        return ToolResult {
            .content = std::format("Evaluation timed out after {}ms\n{}", timeoutMs, output),
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
