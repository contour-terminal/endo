// SPDX-License-Identifier: Apache-2.0
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>

#include <agent/tools/SaveMemoryTool.hpp>

namespace endo::agent
{

SaveMemoryTool::SaveMemoryTool(MemorySavedCallback onSaved): _onSaved(std::move(onSaved))
{
}

auto SaveMemoryTool::name() const noexcept -> std::string_view
{
    return "save_memory";
}

auto SaveMemoryTool::definition() const -> ToolDefinition
{
    return ToolDefinition {
        .name = "save_memory",
        .description = "Saves a memory file to persistent agent memory (~/.config/endo/agent-memory/). "
                       "Memory files are loaded into the system prompt on every agent session, allowing "
                       "you to remember information across sessions. Use this to store learned project "
                       "patterns, user preferences, and key insights. The filename should not include "
                       "the .md extension.",
        .inputSchema =
            nlohmann::json {
                { "type", "object" },
                { "properties",
                  { { "filename",
                      { { "type", "string" },
                        { "description",
                          "The memory file name without extension (e.g. 'project-notes', "
                          "'preferences')" } } },
                    { "content",
                      { { "type", "string" }, { "description", "The markdown content to save" } } } } },
                { "required", nlohmann::json::array({ "filename", "content" }) },
            },
    };
}

auto SaveMemoryTool::memoryDirectory() -> std::filesystem::path
{
    auto const* home = std::getenv("HOME");
    if (!home)
        return {};
    return std::filesystem::path(home) / ".config" / "endo" / "agent-memory";
}

auto SaveMemoryTool::execute(nlohmann::json const& arguments) -> std::expected<ToolResult, ToolError>
{
    auto const filename = arguments.value("filename", std::string {});
    if (filename.empty())
        return std::unexpected(ToolError { .message = "Missing required parameter: filename" });

    if (!arguments.contains("content"))
        return std::unexpected(ToolError { .message = "Missing required parameter: content" });

    // Reject filenames with path separators to prevent directory traversal.
    if (filename.find('/') != std::string::npos || filename.find('\\') != std::string::npos)
        return std::unexpected(ToolError { .message = "Filename must not contain path separators" });

    auto const& content = arguments["content"].get_ref<std::string const&>();
    auto const memoryDir = memoryDirectory();
    if (memoryDir.empty())
        return std::unexpected(ToolError { .message = "Cannot determine home directory" });

    // Create memory directory if it does not exist.
    auto ec = std::error_code {};
    std::filesystem::create_directories(memoryDir, ec);
    if (ec)
        return std::unexpected(
            ToolError { .message = std::format("Failed to create memory directory: {}", ec.message()) });

    auto const filePath = memoryDir / std::format("{}.md", filename);
    auto file = std::ofstream(filePath, std::ios::binary);
    if (!file.is_open())
        return std::unexpected(
            ToolError { .message = std::format("Failed to open file for writing: {}", filePath.string()) });

    file.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (!file.good())
        return std::unexpected(
            ToolError { .message = std::format("Failed to write to file: {}", filePath.string()) });

    // Notify caller (e.g. to invalidate cached project context).
    if (_onSaved)
        _onSaved();

    return ToolResult {
        .content = std::format("Saved {} bytes to {}", content.size(), filePath.string()),
        .isError = false,
    };
}

} // namespace endo::agent
