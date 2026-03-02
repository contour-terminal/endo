// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <regex>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <agent/tools/GrepTool.hpp>

namespace endo::agent
{

namespace
{
    constexpr auto MaxMatches = size_t { 500 };
    constexpr auto BinaryCheckSize = size_t { 8192 };

    /// Checks if a file appears to be binary by scanning for null bytes.
    auto isBinaryFile(std::filesystem::path const& path) -> bool
    {
        auto file = std::ifstream(path, std::ios::binary);
        if (!file.is_open())
            return true;

        auto buffer = std::array<char, BinaryCheckSize> {};
        file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        auto const bytesRead = static_cast<size_t>(file.gcount());

        return std::any_of(buffer.begin(), buffer.begin() + bytesRead, [](char c) { return c == '\0'; });
    }

    /// Simple glob matching for file filtering (single level only, no **).
    auto matchesGlob(std::string_view filename, std::string_view glob) -> bool
    {
        if (glob.empty())
            return true;

        // Convert simple glob to regex (just * and ?)
        auto pattern = std::string { "^" };
        for (auto const ch: glob)
        {
            switch (ch)
            {
                case '*': pattern += ".*"; break;
                case '?': pattern += "."; break;
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
                    pattern += '\\';
                    pattern += ch;
                    break;
                default: pattern += ch; break;
            }
        }
        pattern += '$';

        try
        {
            return std::regex_match(std::string(filename), std::regex(pattern));
        }
        catch (std::regex_error const&)
        {
            return false;
        }
    }

    struct MatchResult
    {
        std::string filePath;
        int lineNumber;
        std::string lineContent;
    };

} // namespace

auto GrepTool::name() const noexcept -> std::string_view
{
    return "grep";
}

auto GrepTool::definition() const -> ToolDefinition
{
    return ToolDefinition {
        .name = "grep",
        .description = "Searches file contents using regular expressions. Returns matches in "
                       "path:line_number: content format. Skips binary files. Limited to 500 matches.",
        .inputSchema =
            nlohmann::json {
                { "type", "object" },
                { "properties",
                  { { "pattern",
                      { { "type", "string" },
                        { "description", "Regular expression pattern to search for" } } },
                    { "path",
                      { { "type", "string" },
                        { "description", "Directory to search in (default: current working directory)" } } },
                    { "glob",
                      { { "type", "string" },
                        { "description", R"(Glob pattern to filter files (e.g. "*.cpp", "*.{ts,tsx}"))" } } },
                    { "context",
                      { { "type", "integer" },
                        { "description",
                          "Number of context lines before and after each match (default: 0)" } } } } },
                { "required", nlohmann::json::array({ "pattern" }) },
            },
    };
}

auto GrepTool::execute(nlohmann::json const& arguments) -> std::expected<ToolResult, ToolError>
{
    auto const pattern = arguments.value("pattern", std::string {});
    if (pattern.empty())
        return std::unexpected(ToolError { .message = "Missing required parameter: pattern" });

    auto basePath = std::filesystem::path(arguments.value("path", std::string {}));
    if (basePath.empty())
        basePath = std::filesystem::current_path();

    auto const globFilter = arguments.value("glob", std::string {});
    auto const contextLines = std::max(0, arguments.value("context", 0));

    auto ec = std::error_code {};
    if (!std::filesystem::exists(basePath, ec))
        return std::unexpected(
            ToolError { .message = std::format("Directory not found: {}", basePath.string()) });

    auto regex = std::regex {};
    try
    {
        regex = std::regex(pattern, std::regex::ECMAScript);
    }
    catch (std::regex_error const& e)
    {
        return std::unexpected(ToolError { .message = std::format("Invalid regex pattern: {}", e.what()) });
    }

    auto totalMatches = size_t { 0 };
    auto output = std::string {};

    // If basePath is a file, search just that file
    auto const isFile = std::filesystem::is_regular_file(basePath, ec);
    auto filesToSearch = std::vector<std::filesystem::path> {};

    if (isFile)
    {
        filesToSearch.push_back(basePath);
    }
    else
    {
        for (auto const& entry: std::filesystem::recursive_directory_iterator(
                 basePath, std::filesystem::directory_options::skip_permission_denied, ec))
        {
            if (!entry.is_regular_file(ec))
                continue;

            if (!globFilter.empty() && !matchesGlob(entry.path().filename().string(), globFilter))
                continue;

            filesToSearch.push_back(entry.path());
        }
    }

    for (auto const& filePath: filesToSearch)
    {
        if (totalMatches >= MaxMatches)
            break;

        if (isBinaryFile(filePath))
            continue;

        auto file = std::ifstream(filePath);
        if (!file.is_open())
            continue;

        // Read all lines for context support
        auto lines = std::vector<std::string> {};
        auto line = std::string {};
        while (std::getline(file, line))
            lines.push_back(std::move(line));

        auto matchedLineNumbers = std::set<int> {};

        for (auto i = 0; std::cmp_less(i, lines.size()); ++i)
        {
            if (std::regex_search(lines[i], regex))
                matchedLineNumbers.insert(i);
        }

        if (matchedLineNumbers.empty())
            continue;

        // Build output with context lines
        auto printedLines = std::set<int> {};
        for (auto const matchLine: matchedLineNumbers)
        {
            if (totalMatches >= MaxMatches)
                break;

            auto const start = std::max(0, matchLine - contextLines);
            auto const end = std::min(static_cast<int>(lines.size()) - 1, matchLine + contextLines);

            for (auto i = start; i <= end; ++i)
            {
                if (printedLines.contains(i))
                    continue;

                printedLines.insert(i);

                auto const marker = (i == matchLine) ? ':' : '-';
                output += std::format("{}{}{}{}  {}\n", filePath.string(), marker, i + 1, marker, lines[i]);
            }

            ++totalMatches;
        }
    }

    if (output.empty())
        return ToolResult { .content = "No matches found.", .isError = false };

    if (totalMatches >= MaxMatches)
        output += std::format("\n(truncated at {} matches)\n", MaxMatches);

    return ToolResult {
        .content = std::move(output),
        .isError = false,
    };
}

} // namespace endo::agent
