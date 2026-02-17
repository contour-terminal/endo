// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <filesystem>
#include <format>
#include <regex>
#include <string>
#include <vector>

#include <agent/tools/GlobTool.hpp>

namespace endo::agent
{

namespace
{
    constexpr auto MaxResults = size_t { 1000 };

    /// Converts a glob pattern to a regex pattern.
    /// Supports *, **, and ? wildcards.
    auto globToRegex(std::string_view pattern) -> std::string
    {
        auto result = std::string {};
        auto i = size_t { 0 };

        while (i < pattern.size())
        {
            auto const ch = pattern[i];
            switch (ch)
            {
                case '*':
                    if (i + 1 < pattern.size() && pattern[i + 1] == '*')
                    {
                        // ** matches any path component(s)
                        if (i + 2 < pattern.size() && pattern[i + 2] == '/')
                        {
                            result += "(.*/)?";
                            i += 3;
                        }
                        else
                        {
                            result += ".*";
                            i += 2;
                        }
                    }
                    else
                    {
                        // * matches anything except /
                        result += "[^/]*";
                        ++i;
                    }
                    break;
                case '?':
                    result += "[^/]";
                    ++i;
                    break;
                case '.':
                case '(':
                case ')':
                case '+':
                case '|':
                case '^':
                case '$':
                case '{':
                case '}':
                case '[':
                case ']':
                case '\\':
                    result += '\\';
                    result += ch;
                    ++i;
                    break;
                default:
                    result += ch;
                    ++i;
                    break;
            }
        }

        return result;
    }

    struct FileEntry
    {
        std::string path;
        std::filesystem::file_time_type modTime;
    };

} // namespace

auto GlobTool::name() const noexcept -> std::string_view
{
    return "glob";
}

auto GlobTool::definition() const -> ToolDefinition
{
    return ToolDefinition {
        .name = "glob",
        .description =
            "Finds files matching a glob pattern. Supports *, **, and ? wildcards. "
            "Returns file paths sorted by modification time (newest first), limited to 1000 results.",
        .inputSchema =
            nlohmann::json {
                { "type", "object" },
                { "properties",
                  { { "pattern",
                      { { "type", "string" },
                        { "description", "Glob pattern to match files against (e.g. \"**/*.cpp\")" } } },
                    { "path",
                      { { "type", "string" },
                        { "description",
                          "Base directory to search in (default: current working directory)" } } } } },
                { "required", nlohmann::json::array({ "pattern" }) },
            },
    };
}

auto GlobTool::execute(nlohmann::json const& arguments) -> std::expected<ToolResult, ToolError>
{
    auto const pattern = arguments.value("pattern", std::string {});
    if (pattern.empty())
        return std::unexpected(ToolError { .message = "Missing required parameter: pattern" });

    auto basePath = std::filesystem::path(arguments.value("path", std::string {}));
    if (basePath.empty())
        basePath = std::filesystem::current_path();

    auto ec = std::error_code {};
    if (!std::filesystem::exists(basePath, ec))
        return std::unexpected(
            ToolError { .message = std::format("Directory not found: {}", basePath.string()) });

    // Build regex from glob pattern
    auto const regexPattern = "^" + globToRegex(pattern) + "$";
    auto regex = std::regex {};
    try
    {
        regex = std::regex(regexPattern, std::regex::ECMAScript);
    }
    catch (std::regex_error const& e)
    {
        return std::unexpected(ToolError { .message = std::format("Invalid glob pattern: {}", e.what()) });
    }

    auto matches = std::vector<FileEntry> {};

    for (auto const& entry: std::filesystem::recursive_directory_iterator(
             basePath, std::filesystem::directory_options::skip_permission_denied, ec))
    {
        if (!entry.is_regular_file(ec))
            continue;

        // Get relative path for matching
        auto const relativePath = std::filesystem::relative(entry.path(), basePath, ec);
        if (ec)
            continue;

        auto const relativeStr = relativePath.generic_string();
        if (std::regex_match(relativeStr, regex))
        {
            auto modTime = entry.last_write_time(ec);
            if (ec)
                modTime = std::filesystem::file_time_type {};

            matches.push_back(FileEntry {
                .path = entry.path().string(),
                .modTime = modTime,
            });

            if (matches.size() >= MaxResults)
                break;
        }
    }

    // Sort by modification time (newest first)
    std::ranges::sort(matches, [](auto const& a, auto const& b) { return a.modTime > b.modTime; });

    if (matches.empty())
        return ToolResult { .content = "No files matched the pattern.", .isError = false };

    auto output = std::string {};
    for (auto const& entry: matches)
    {
        output += entry.path;
        output += '\n';
    }

    return ToolResult {
        .content = std::move(output),
        .isError = false,
    };
}

} // namespace endo::agent
