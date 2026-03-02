// SPDX-License-Identifier: Apache-2.0
#include <filesystem>
#include <format>
#include <fstream>
#include <string>

#include <agent/tools/ReadFileTool.hpp>

namespace endo::agent
{

namespace
{
    constexpr auto MaxLineLength = size_t { 2000 };
    constexpr auto DefaultLimit = 2000;
} // namespace

auto ReadFileTool::name() const noexcept -> std::string_view
{
    return "read_file";
}

auto ReadFileTool::definition() const -> ToolDefinition
{
    return ToolDefinition {
        .name = "read_file",
        .description = "Reads a file from the filesystem. Returns file contents with line numbers. "
                       "Lines longer than 2000 characters are truncated.",
        .inputSchema =
            nlohmann::json {
                { "type", "object" },
                { "properties",
                  { { "path", { { "type", "string" }, { "description", "The file path to read" } } },
                    { "offset",
                      { { "type", "integer" },
                        { "description", "1-based line number to start reading from (default: 1)" } } },
                    { "limit",
                      { { "type", "integer" },
                        { "description", "Maximum number of lines to read (default: 2000)" } } } } },
                { "required", nlohmann::json::array({ "path" }) },
            },
    };
}

auto ReadFileTool::execute(nlohmann::json const& arguments) -> std::expected<ToolResult, ToolError>
{
    auto const path = arguments.value("path", std::string {});
    if (path.empty())
        return std::unexpected(ToolError { .message = "Missing required parameter: path" });

    auto const offset = std::max(1, arguments.value("offset", 1));
    auto const limit = std::max(1, arguments.value("limit", DefaultLimit));

    auto const fsPath = std::filesystem::path(path);
    auto ec = std::error_code {};

    if (!std::filesystem::exists(fsPath, ec))
        return std::unexpected(ToolError { .message = std::format("File not found: {}", path) });

    if (std::filesystem::is_directory(fsPath, ec))
        return std::unexpected(ToolError { .message = std::format("Is a directory: {}", path) });

    auto file = std::ifstream(fsPath);
    if (!file.is_open())
        return std::unexpected(ToolError { .message = std::format("Permission denied: {}", path) });

    auto output = std::string {};
    auto line = std::string {};
    auto lineNumber = 0;
    auto linesRead = 0;

    while (std::getline(file, line))
    {
        ++lineNumber;

        if (lineNumber < offset)
            continue;

        if (linesRead >= limit)
            break;

        // Truncate long lines
        if (line.size() > MaxLineLength)
            line = line.substr(0, MaxLineLength) + "...";

        output += std::format("{:>6}\t{}\n", lineNumber, line);
        ++linesRead;
    }

    if (output.empty())
        output = "(empty file)\n";

    return ToolResult {
        .content = std::move(output),
        .isError = false,
    };
}

} // namespace endo::agent
