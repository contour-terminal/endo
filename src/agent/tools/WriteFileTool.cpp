// SPDX-License-Identifier: Apache-2.0
#include <filesystem>
#include <format>
#include <fstream>

#include <agent/tools/WriteFileTool.hpp>

namespace endo::agent
{

auto WriteFileTool::name() const noexcept -> std::string_view
{
    return "write_file";
}

auto WriteFileTool::definition() const -> ToolDefinition
{
    return ToolDefinition {
        .name = "write_file",
        .description = "Writes content to a file. Creates parent directories as needed. "
                       "Overwrites the file if it already exists.",
        .inputSchema =
            nlohmann::json {
                { "type", "object" },
                { "properties",
                  { { "path", { { "type", "string" }, { "description", "The file path to write to" } } },
                    { "content", { { "type", "string" }, { "description", "The content to write" } } } } },
                { "required", nlohmann::json::array({ "path", "content" }) },
            },
    };
}

auto WriteFileTool::execute(nlohmann::json const& arguments) -> std::expected<ToolResult, ToolError>
{
    auto const path = arguments.value("path", std::string {});
    if (path.empty())
        return std::unexpected(ToolError { .message = "Missing required parameter: path" });

    if (!arguments.contains("content"))
        return std::unexpected(ToolError { .message = "Missing required parameter: content" });

    auto const& content = arguments["content"].get_ref<std::string const&>();
    auto const fsPath = std::filesystem::path(path);

    // Create parent directories if they don't exist
    if (fsPath.has_parent_path())
    {
        auto ec = std::error_code {};
        std::filesystem::create_directories(fsPath.parent_path(), ec);
        if (ec)
            return std::unexpected(
                ToolError { .message = std::format("Failed to create directories: {}", ec.message()) });
    }

    auto file = std::ofstream(fsPath, std::ios::binary);
    if (!file.is_open())
        return std::unexpected(
            ToolError { .message = std::format("Failed to open file for writing: {}", path) });

    file.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (!file.good())
        return std::unexpected(ToolError { .message = std::format("Failed to write to file: {}", path) });

    return ToolResult {
        .content = std::format("Successfully wrote {} bytes to {}", content.size(), path),
        .isError = false,
    };
}

} // namespace endo::agent
