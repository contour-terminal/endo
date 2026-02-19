// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <string>
#include <system_error>
#include <vector>

#include <agent/tools/ListDirectoryTool.hpp>

namespace endo::agent
{

namespace
{
    constexpr size_t MaxEntries = 1000;

    /// @brief Formats a byte size into a human-readable string (e.g., "1.5 KB").
    [[nodiscard]] auto formatSize(uintmax_t bytes) -> std::string
    {
        if (bytes < 1024)
            return std::format("{} B", bytes);
        if (bytes < 1024 * 1024)
            return std::format("{:.1f} KB", static_cast<double>(bytes) / 1024.0);
        if (bytes < 1024 * 1024 * 1024)
            return std::format("{:.1f} MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
        return std::format("{:.1f} GB", static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
    }

    /// @brief Returns a single-character type indicator for a directory entry.
    [[nodiscard]] auto typeIndicator(std::filesystem::file_status status) -> std::string_view
    {
        if (std::filesystem::is_directory(status))
            return "d";
        if (std::filesystem::is_symlink(status))
            return "l";
        if (std::filesystem::is_regular_file(status))
            return "-";
        return "?";
    }
} // namespace

auto ListDirectoryTool::name() const noexcept -> std::string_view
{
    return "list_directory";
}

auto ListDirectoryTool::definition() const -> ToolDefinition
{
    return ToolDefinition {
        .name = "list_directory",
        .description = "Lists the contents of a directory. Returns file and directory names, "
                       "sorted alphabetically with directories first (suffixed with '/'). "
                       "Use long_format for sizes, dates, and type indicators.",
        .inputSchema =
            nlohmann::json {
                { "type", "object" },
                { "properties",
                  nlohmann::json {
                      { "path",
                        nlohmann::json {
                            { "type", "string" },
                            { "description", "Directory path to list." },
                        } },
                      { "show_hidden",
                        nlohmann::json {
                            { "type", "boolean" },
                            { "description", "Include dotfiles (default: false)." },
                        } },
                      { "long_format",
                        nlohmann::json {
                            { "type", "boolean" },
                            { "description", "Show sizes, dates, and types (default: false)." },
                        } },
                  } },
                { "required", nlohmann::json::array({ "path" }) },
            },
    };
}

auto ListDirectoryTool::execute(nlohmann::json const& arguments) -> std::expected<ToolResult, ToolError>
{
    auto const path = arguments.value("path", std::string {});
    if (path.empty())
        return std::unexpected(ToolError { .message = "Missing required parameter: path" });

    auto const showHidden = arguments.value("show_hidden", false);
    auto const longFormat = arguments.value("long_format", false);

    auto ec = std::error_code {};
    auto const dirPath = std::filesystem::path(path);

    if (!std::filesystem::exists(dirPath, ec))
        return std::unexpected(ToolError { .message = std::format("Path does not exist: {}", path) });

    if (!std::filesystem::is_directory(dirPath, ec))
        return std::unexpected(ToolError { .message = std::format("Path is not a directory: {}", path) });

    // Collect entries
    struct Entry
    {
        std::string name;
        bool isDir = false;
        bool isSymlink = false;
        std::filesystem::file_status status;
        uintmax_t size = 0;
        std::filesystem::file_time_type modTime;
        std::string symlinkTarget;
    };

    auto entries = std::vector<Entry> {};
    for (auto const& dirEntry: std::filesystem::directory_iterator(dirPath, ec))
    {
        auto const filename = dirEntry.path().filename().string();

        // Filter hidden files
        if (!showHidden && !filename.empty() && filename[0] == '.')
            continue;

        auto entry = Entry {};
        entry.name = filename;
        entry.status = dirEntry.symlink_status(ec);
        entry.isSymlink = std::filesystem::is_symlink(entry.status);
        entry.isDir = dirEntry.is_directory(ec);

        if (longFormat)
        {
            if (dirEntry.is_regular_file(ec))
                entry.size = dirEntry.file_size(ec);
            entry.modTime = dirEntry.last_write_time(ec);

            if (entry.isSymlink)
            {
                auto target = std::filesystem::read_symlink(dirEntry.path(), ec);
                if (!ec)
                    entry.symlinkTarget = target.string();
            }
        }

        entries.push_back(std::move(entry));
    }

    // Sort: directories first, then alphabetically by name
    std::ranges::sort(entries, [](auto const& a, auto const& b) {
        if (a.isDir != b.isDir)
            return a.isDir > b.isDir; // directories first
        return a.name < b.name;
    });

    // Format output
    auto const truncated = entries.size() > MaxEntries;
    if (truncated)
        entries.resize(MaxEntries);

    auto output = std::string {};
    for (auto const& entry: entries)
    {
        if (longFormat)
        {
            auto const type = entry.isSymlink ? "l" : typeIndicator(entry.status);
            auto const sizeStr = entry.isDir ? std::string { "-" } : formatSize(entry.size);

            // Convert file_time_type to system_clock for formatting
            auto const sctp = std::chrono::clock_cast<std::chrono::system_clock>(entry.modTime);
            auto const date = std::format("{:%Y-%m-%d %H:%M}", sctp);

            auto displayName = entry.name;
            if (entry.isDir)
                displayName += '/';
            if (entry.isSymlink && !entry.symlinkTarget.empty())
                displayName += std::format(" -> {}", entry.symlinkTarget);

            output += std::format("{} {:>8}  {}  {}\n", type, sizeStr, date, displayName);
        }
        else
        {
            output += entry.name;
            if (entry.isDir)
                output += '/';
            output += '\n';
        }
    }

    if (truncated)
        output += std::format("\n[truncated — showing {} of {} entries]\n",
                              MaxEntries,
                              MaxEntries); // We don't know total; just note truncation

    return ToolResult { .content = std::move(output), .isError = false };
}

} // namespace endo::agent
