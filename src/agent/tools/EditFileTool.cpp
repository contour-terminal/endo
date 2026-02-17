// SPDX-License-Identifier: Apache-2.0
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <string>

#include <agent/tools/EditFileTool.hpp>

namespace endo::agent
{

auto EditFileTool::name() const noexcept -> std::string_view
{
    return "edit_file";
}

auto EditFileTool::definition() const -> ToolDefinition
{
    return ToolDefinition {
        .name = "edit_file",
        .description = "Performs exact string replacements in a file. "
                       "Fails if the old_string is not found or is ambiguous (multiple matches) "
                       "unless replace_all is true.",
        .inputSchema =
            nlohmann::json {
                { "type", "object" },
                { "properties",
                  { { "path", { { "type", "string" }, { "description", "The file path to edit" } } },
                    { "old_string",
                      { { "type", "string" }, { "description", "The exact text to find and replace" } } },
                    { "new_string", { { "type", "string" }, { "description", "The replacement text" } } },
                    { "replace_all",
                      { { "type", "boolean" },
                        { "description", "Replace all occurrences (default: false)" },
                        { "default", false } } } } },
                { "required", nlohmann::json::array({ "path", "old_string", "new_string" }) },
            },
    };
}

auto EditFileTool::execute(nlohmann::json const& arguments) -> std::expected<ToolResult, ToolError>
{
    auto const path = arguments.value("path", std::string {});
    if (path.empty())
        return std::unexpected(ToolError { .message = "Missing required parameter: path" });

    auto const oldString = arguments.value("old_string", std::string {});
    if (oldString.empty())
        return std::unexpected(ToolError { .message = "old_string must not be empty" });

    auto const newString = arguments.value("new_string", std::string {});
    auto const replaceAll = arguments.value("replace_all", false);

    auto const fsPath = std::filesystem::path(path);
    auto ec = std::error_code {};

    if (!std::filesystem::exists(fsPath, ec))
        return std::unexpected(ToolError { .message = std::format("File not found: {}", path) });

    // Read the entire file
    auto file = std::ifstream(fsPath);
    if (!file.is_open())
        return std::unexpected(ToolError { .message = std::format("Permission denied: {}", path) });

    auto ss = std::ostringstream {};
    ss << file.rdbuf();
    auto content = ss.str();
    file.close();

    // Count occurrences
    auto count = size_t { 0 };
    auto pos = size_t { 0 };
    while ((pos = content.find(oldString, pos)) != std::string::npos)
    {
        ++count;
        pos += oldString.size();
    }

    if (count == 0)
        return std::unexpected(ToolError { .message = "old_string not found in file" });

    if (count > 1 && !replaceAll)
        return std::unexpected(
            ToolError { .message = std::format("old_string is ambiguous: found {} occurrences. "
                                               "Use replace_all=true or provide more context.",
                                               count) });

    // Perform replacement
    auto result = std::string {};
    pos = 0;
    auto replacements = size_t { 0 };

    while (true)
    {
        auto const found = content.find(oldString, pos);
        if (found == std::string::npos)
        {
            result += content.substr(pos);
            break;
        }

        result += content.substr(pos, found - pos);
        result += newString;
        pos = found + oldString.size();
        ++replacements;

        if (!replaceAll)
        {
            result += content.substr(pos);
            break;
        }
    }

    // Write the modified content back
    auto outFile = std::ofstream(fsPath, std::ios::binary);
    if (!outFile.is_open())
        return std::unexpected(ToolError { .message = std::format("Failed to write to file: {}", path) });

    outFile.write(result.data(), static_cast<std::streamsize>(result.size()));

    return ToolResult {
        .content =
            std::format("Replaced {} occurrence{} in {}", replacements, replacements != 1 ? "s" : "", path),
        .isError = false,
    };
}

} // namespace endo::agent
