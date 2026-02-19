// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <array>
#include <filesystem>
#include <format>
#include <fstream>
#include <regex>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <agent/tools/SearchTool.hpp>

namespace endo::agent
{

namespace
{
    constexpr auto DefaultContentLimit = size_t { 500 };
    constexpr auto DefaultFilesLimit = size_t { 1000 };
    constexpr auto BinaryCheckSize = size_t { 8192 };

    /// File type mapping entry: type name to glob pattern.
    struct FileTypeEntry
    {
        std::string_view typeName;
        std::string_view glob;
    };

    // clang-format off
    /// Lookup table for file type shorthand to glob patterns.
    constexpr auto FileTypeGlobs = std::array<FileTypeEntry, 43> { {
        { .typeName = "cpp", .glob = "*.cpp" },     { .typeName = "cpp", .glob = "*.cc" },
        { .typeName = "cpp", .glob = "*.cxx" },     { .typeName = "cpp", .glob = "*.hpp" },
        { .typeName = "cpp", .glob = "*.hh" },      { .typeName = "cpp", .glob = "*.hxx" },
        { .typeName = "cpp", .glob = "*.h" },       { .typeName = "cpp", .glob = "*.ipp" },
        { .typeName = "c", .glob = "*.c" },         { .typeName = "c", .glob = "*.h" },
        { .typeName = "rust", .glob = "*.rs" },     { .typeName = "go", .glob = "*.go" },
        { .typeName = "java", .glob = "*.java" },   { .typeName = "py", .glob = "*.py" },
        { .typeName = "py", .glob = "*.pyi" },      { .typeName = "js", .glob = "*.js" },
        { .typeName = "js", .glob = "*.mjs" },      { .typeName = "js", .glob = "*.cjs" },
        { .typeName = "ts", .glob = "*.ts" },       { .typeName = "ts", .glob = "*.tsx" },
        { .typeName = "ts", .glob = "*.mts" },      { .typeName = "ts", .glob = "*.cts" },
        { .typeName = "ruby", .glob = "*.rb" },     { .typeName = "swift", .glob = "*.swift" },
        { .typeName = "csharp", .glob = "*.cs" },   { .typeName = "shell", .glob = "*.sh" },
        { .typeName = "shell", .glob = "*.bash" },  { .typeName = "shell", .glob = "*.zsh" },
        { .typeName = "cmake", .glob = "CMakeLists.txt" }, { .typeName = "cmake", .glob = "*.cmake" },
        { .typeName = "yaml", .glob = "*.yml" },    { .typeName = "yaml", .glob = "*.yaml" },
        { .typeName = "json", .glob = "*.json" },   { .typeName = "toml", .glob = "*.toml" },
        { .typeName = "html", .glob = "*.html" },   { .typeName = "html", .glob = "*.htm" },
        { .typeName = "css", .glob = "*.css" },     { .typeName = "css", .glob = "*.scss" },
        { .typeName = "css", .glob = "*.sass" },    { .typeName = "css", .glob = "*.less" },
        { .typeName = "md", .glob = "*.md" },       { .typeName = "md", .glob = "*.markdown" },
        { .typeName = "zig", .glob = "*.zig" },
    } };
    // clang-format on

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

    /// Simple glob matching for a single filename component (no path separators).
    auto matchesGlob(std::string_view filename, std::string_view glob) -> bool
    {
        if (glob.empty())
            return true;

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

    /// Returns the glob patterns associated with a file type shorthand.
    auto fileTypeGlobs(std::string_view typeName) -> std::vector<std::string_view>
    {
        auto result = std::vector<std::string_view> {};
        for (auto const& [name, glob]: FileTypeGlobs)
        {
            if (name == typeName)
                result.push_back(glob);
        }
        return result;
    }

    /// Checks if a filename matches any of the given glob patterns.
    auto matchesAnyGlob(std::string_view filename, std::vector<std::string_view> const& globs) -> bool
    {
        return std::ranges::any_of(globs, [&](auto g) { return matchesGlob(filename, g); });
    }

    /// Determines whether a file should be included based on glob and type filters.
    auto shouldIncludeFile(std::filesystem::path const& filePath,
                           std::string_view globFilter,
                           std::vector<std::string_view> const& typeGlobs) -> bool
    {
        auto const filename = filePath.filename().string();

        if (!globFilter.empty() && !matchesGlob(filename, globFilter))
            return false;

        if (!typeGlobs.empty() && !matchesAnyGlob(filename, typeGlobs))
            return false;

        return true;
    }

    /// File entry with path and modification time for sorting.
    struct FileEntry
    {
        std::string path;
        std::filesystem::file_time_type modTime;
    };

    /// Collects files from a directory, applying glob/type filters.
    auto collectFiles(std::filesystem::path const& basePath,
                      std::string_view globFilter,
                      std::vector<std::string_view> const& typeGlobs) -> std::vector<std::filesystem::path>
    {
        auto result = std::vector<std::filesystem::path> {};
        auto ec = std::error_code {};

        for (auto const& entry: std::filesystem::recursive_directory_iterator(
                 basePath, std::filesystem::directory_options::skip_permission_denied, ec))
        {
            if (!entry.is_regular_file(ec))
                continue;

            if (!shouldIncludeFile(entry.path(), globFilter, typeGlobs))
                continue;

            result.push_back(entry.path());
        }

        return result;
    }

    /// Executes file-finding mode: walk directory, match relative paths against glob regex.
    auto executeFilesMode(std::filesystem::path const& basePath,
                          std::string_view pattern,
                          std::vector<std::string_view> const& typeGlobs,
                          size_t limit,
                          size_t offset) -> std::expected<ToolResult, ToolError>
    {
        auto const regexPattern = "^" + globToRegex(pattern) + "$";
        auto regex = std::regex {};
        try
        {
            regex = std::regex(regexPattern, std::regex::ECMAScript);
        }
        catch (std::regex_error const& e)
        {
            return std::unexpected(
                ToolError { .message = std::format("Invalid glob pattern: {}", e.what()) });
        }

        auto matches = std::vector<FileEntry> {};
        auto ec = std::error_code {};

        for (auto const& entry: std::filesystem::recursive_directory_iterator(
                 basePath, std::filesystem::directory_options::skip_permission_denied, ec))
        {
            if (!entry.is_regular_file(ec))
                continue;

            auto const relativePath = std::filesystem::relative(entry.path(), basePath, ec);
            if (ec)
                continue;

            auto const relativeStr = relativePath.generic_string();
            if (!std::regex_match(relativeStr, regex))
                continue;

            if (!typeGlobs.empty())
            {
                auto const filename = entry.path().filename().string();
                if (!matchesAnyGlob(filename, typeGlobs))
                    continue;
            }

            auto modTime = entry.last_write_time(ec);
            if (ec)
                modTime = std::filesystem::file_time_type {};

            matches.push_back(FileEntry { .path = entry.path().string(), .modTime = modTime });
        }

        // Sort by modification time (newest first)
        std::ranges::sort(matches, [](auto const& a, auto const& b) { return a.modTime > b.modTime; });

        if (matches.empty())
            return ToolResult { .content = "No files matched the pattern.", .isError = false };

        // Apply offset and limit
        auto const total = matches.size();
        auto const startIdx = std::min(offset, total);
        auto const endIdx = std::min(startIdx + limit, total);

        auto output = std::string {};
        for (auto i = startIdx; i < endIdx; ++i)
        {
            output += matches[i].path;
            output += '\n';
        }

        if (endIdx < total)
            output += std::format("\n(showing {}-{} of {} matches)\n", startIdx + 1, endIdx, total);

        return ToolResult { .content = std::move(output), .isError = false };
    }

    /// Executes content search mode: regex search within file contents.
    auto executeContentMode(std::filesystem::path const& basePath,
                            std::string_view pattern,
                            std::string_view globFilter,
                            std::vector<std::string_view> const& typeGlobs,
                            bool caseInsensitive,
                            int contextBefore,
                            int contextAfter,
                            bool multiline,
                            size_t limit,
                            size_t offset) -> std::expected<ToolResult, ToolError>
    {
        auto regexFlags = std::regex::ECMAScript;
        if (caseInsensitive)
            regexFlags |= std::regex::icase;

        auto regex = std::regex {};
        try
        {
            regex = std::regex(std::string(pattern), regexFlags);
        }
        catch (std::regex_error const& e)
        {
            return std::unexpected(
                ToolError { .message = std::format("Invalid regex pattern: {}", e.what()) });
        }

        auto const isFile = std::filesystem::is_regular_file(basePath);
        auto filesToSearch = std::vector<std::filesystem::path> {};

        if (isFile)
        {
            filesToSearch.push_back(basePath);
        }
        else
        {
            filesToSearch = collectFiles(basePath, globFilter, typeGlobs);
        }

        auto totalMatches = size_t { 0 };
        auto emittedMatches = size_t { 0 };
        auto output = std::string {};

        for (auto const& filePath: filesToSearch)
        {
            if (emittedMatches >= limit)
                break;

            if (isBinaryFile(filePath))
                continue;

            auto file = std::ifstream(filePath);
            if (!file.is_open())
                continue;

            if (multiline)
            {
                // Read entire file content for multiline matching
                auto content = std::string(std::istreambuf_iterator<char>(file), {});
                auto searchStart = content.cbegin();
                auto match = std::smatch {};

                while (std::regex_search(searchStart, content.cend(), match, regex))
                {
                    ++totalMatches;
                    if (totalMatches > offset && emittedMatches < limit)
                    {
                        // Compute line number of match start
                        auto const matchPos =
                            match.position(0) + std::distance(content.cbegin(), searchStart);
                        auto const lineNum =
                            1 + std::count(content.begin(), content.begin() + matchPos, '\n');
                        output += std::format("{}:{}:  {}\n", filePath.string(), lineNum, match.str());
                        ++emittedMatches;
                    }
                    searchStart = match.suffix().first;
                }
            }
            else
            {
                // Line-by-line matching
                auto lines = std::vector<std::string> {};
                auto line = std::string {};
                while (std::getline(file, line))
                    lines.push_back(std::move(line));

                auto matchedLineNumbers = std::vector<int> {};
                for (auto i = 0; i < static_cast<int>(lines.size()); ++i)
                {
                    if (std::regex_search(lines[i], regex))
                        matchedLineNumbers.push_back(i);
                }

                if (matchedLineNumbers.empty())
                    continue;

                auto printedLines = std::set<int> {};
                for (auto const matchLine: matchedLineNumbers)
                {
                    ++totalMatches;
                    if (totalMatches <= offset)
                        continue;
                    if (emittedMatches >= limit)
                        break;

                    auto const start = std::max(0, matchLine - contextBefore);
                    auto const end = std::min(static_cast<int>(lines.size()) - 1, matchLine + contextAfter);

                    for (auto i = start; i <= end; ++i)
                    {
                        if (printedLines.contains(i))
                            continue;
                        printedLines.insert(i);

                        auto const marker = (i == matchLine) ? ':' : '-';
                        output +=
                            std::format("{}{}{}{}  {}\n", filePath.string(), marker, i + 1, marker, lines[i]);
                    }

                    ++emittedMatches;
                }
            }
        }

        if (output.empty())
            return ToolResult { .content = "No matches found.", .isError = false };

        if (emittedMatches >= limit)
            output += std::format("\n(truncated at {} matches)\n", limit);

        return ToolResult { .content = std::move(output), .isError = false };
    }

    /// Executes files-with-matches mode: list files containing at least one match.
    auto executeFilesWithMatchesMode(std::filesystem::path const& basePath,
                                     std::string_view pattern,
                                     std::string_view globFilter,
                                     std::vector<std::string_view> const& typeGlobs,
                                     bool caseInsensitive,
                                     bool multiline,
                                     size_t limit,
                                     size_t offset) -> std::expected<ToolResult, ToolError>
    {
        auto regexFlags = std::regex::ECMAScript;
        if (caseInsensitive)
            regexFlags |= std::regex::icase;

        auto regex = std::regex {};
        try
        {
            regex = std::regex(std::string(pattern), regexFlags);
        }
        catch (std::regex_error const& e)
        {
            return std::unexpected(
                ToolError { .message = std::format("Invalid regex pattern: {}", e.what()) });
        }

        auto const files = collectFiles(basePath, globFilter, typeGlobs);
        auto matchingFiles = std::vector<FileEntry> {};

        for (auto const& filePath: files)
        {
            if (isBinaryFile(filePath))
                continue;

            auto file = std::ifstream(filePath);
            if (!file.is_open())
                continue;

            auto found = false;
            if (multiline)
            {
                auto content = std::string(std::istreambuf_iterator<char>(file), {});
                found = std::regex_search(content, regex);
            }
            else
            {
                auto line = std::string {};
                while (std::getline(file, line))
                {
                    if (std::regex_search(line, regex))
                    {
                        found = true;
                        break;
                    }
                }
            }

            if (found)
            {
                auto ec = std::error_code {};
                auto modTime = std::filesystem::last_write_time(filePath, ec);
                if (ec)
                    modTime = std::filesystem::file_time_type {};
                matchingFiles.push_back(FileEntry { .path = filePath.string(), .modTime = modTime });
            }
        }

        // Sort by modification time (newest first)
        std::ranges::sort(matchingFiles, [](auto const& a, auto const& b) { return a.modTime > b.modTime; });

        if (matchingFiles.empty())
            return ToolResult { .content = "No matches found.", .isError = false };

        auto const total = matchingFiles.size();
        auto const startIdx = std::min(offset, total);
        auto const endIdx = std::min(startIdx + limit, total);

        auto output = std::string {};
        for (auto i = startIdx; i < endIdx; ++i)
        {
            output += matchingFiles[i].path;
            output += '\n';
        }

        if (endIdx < total)
            output += std::format("\n(showing {}-{} of {} files)\n", startIdx + 1, endIdx, total);

        return ToolResult { .content = std::move(output), .isError = false };
    }

    /// Executes count mode: count matches per file, sorted by count descending.
    auto executeCountMode(std::filesystem::path const& basePath,
                          std::string_view pattern,
                          std::string_view globFilter,
                          std::vector<std::string_view> const& typeGlobs,
                          bool caseInsensitive,
                          bool multiline,
                          size_t limit,
                          size_t offset) -> std::expected<ToolResult, ToolError>
    {
        auto regexFlags = std::regex::ECMAScript;
        if (caseInsensitive)
            regexFlags |= std::regex::icase;

        auto regex = std::regex {};
        try
        {
            regex = std::regex(std::string(pattern), regexFlags);
        }
        catch (std::regex_error const& e)
        {
            return std::unexpected(
                ToolError { .message = std::format("Invalid regex pattern: {}", e.what()) });
        }

        auto const files = collectFiles(basePath, globFilter, typeGlobs);

        struct CountEntry
        {
            std::string path;
            size_t count;
        };

        auto counts = std::vector<CountEntry> {};

        for (auto const& filePath: files)
        {
            if (isBinaryFile(filePath))
                continue;

            auto file = std::ifstream(filePath);
            if (!file.is_open())
                continue;

            auto matchCount = size_t { 0 };
            if (multiline)
            {
                auto content = std::string(std::istreambuf_iterator<char>(file), {});
                auto searchStart = content.cbegin();
                auto match = std::smatch {};
                while (std::regex_search(searchStart, content.cend(), match, regex))
                {
                    ++matchCount;
                    searchStart = match.suffix().first;
                }
            }
            else
            {
                auto line = std::string {};
                while (std::getline(file, line))
                {
                    if (std::regex_search(line, regex))
                        ++matchCount;
                }
            }

            if (matchCount > 0)
                counts.push_back(CountEntry { .path = filePath.string(), .count = matchCount });
        }

        // Sort by count descending
        std::ranges::sort(counts, [](auto const& a, auto const& b) { return a.count > b.count; });

        if (counts.empty())
            return ToolResult { .content = "No matches found.", .isError = false };

        auto const total = counts.size();
        auto const startIdx = std::min(offset, total);
        auto const endIdx = std::min(startIdx + limit, total);

        auto output = std::string {};
        for (auto i = startIdx; i < endIdx; ++i)
            output += std::format("{}:{}\n", counts[i].path, counts[i].count);

        if (endIdx < total)
            output += std::format("\n(showing {}-{} of {} files)\n", startIdx + 1, endIdx, total);

        return ToolResult { .content = std::move(output), .isError = false };
    }

} // namespace

auto SearchTool::name() const noexcept -> std::string_view
{
    return "search";
}

auto SearchTool::definition() const -> ToolDefinition
{
    return ToolDefinition {
        .name = "search",
        .description =
            "Unified search tool for finding files by glob pattern and searching file contents by regex. "
            "Modes: \"files\" (glob match on paths), \"content\" (regex on contents with context lines), "
            "\"files_with_matches\" (list files containing matches), \"count\" (match counts per file). "
            "Supports file type filtering, case-insensitive search, asymmetric context, multiline "
            "matching, and pagination via offset/limit.",
        .inputSchema =
            nlohmann::json {
                { "type", "object" },
                { "properties",
                  nlohmann::json {
                      { "pattern",
                        nlohmann::json {
                            { "type", "string" },
                            { "description",
                              "Glob pattern (files mode) or regex (content/files_with_matches/count modes)" },
                        } },
                      { "mode",
                        nlohmann::json {
                            { "type", "string" },
                            { "enum",
                              nlohmann::json::array({ "files", "content", "files_with_matches", "count" }) },
                            { "description", "Output mode (default: \"content\")" },
                        } },
                      { "path",
                        nlohmann::json {
                            { "type", "string" },
                            { "description",
                              "Base directory or file to search in (default: current working directory)" },
                        } },
                      { "glob",
                        nlohmann::json {
                            { "type", "string" },
                            { "description", "Filename filter glob for content modes (e.g. \"*.cpp\")" },
                        } },
                      { "type",
                        nlohmann::json {
                            { "type", "string" },
                            { "description",
                              "File type shorthand (e.g. \"cpp\", \"py\", \"js\", \"rust\", \"ts\")" },
                        } },
                      { "case_insensitive",
                        nlohmann::json {
                            { "type", "boolean" },
                            { "description", "Case-insensitive regex matching (default: false)" },
                        } },
                      { "context_before",
                        nlohmann::json {
                            { "type", "integer" },
                            { "description", "Lines before each match in content mode (default: 0)" },
                        } },
                      { "context_after",
                        nlohmann::json {
                            { "type", "integer" },
                            { "description", "Lines after each match in content mode (default: 0)" },
                        } },
                      { "context",
                        nlohmann::json {
                            { "type", "integer" },
                            { "description",
                              "Symmetric context lines; overridden by context_before/context_after" },
                        } },
                      { "multiline",
                        nlohmann::json {
                            { "type", "boolean" },
                            { "description", "Cross-line regex matching (default: false)" },
                        } },
                      { "limit",
                        nlohmann::json {
                            { "type", "integer" },
                            { "description",
                              "Max result entries (default: 500 for content, 1000 for files modes)" },
                        } },
                      { "offset",
                        nlohmann::json {
                            { "type", "integer" },
                            { "description", "Skip first N entries for pagination (default: 0)" },
                        } },
                  } },
                { "required", nlohmann::json::array({ "pattern" }) },
            },
    };
}

auto SearchTool::execute(nlohmann::json const& arguments) -> std::expected<ToolResult, ToolError>
{
    auto const pattern = arguments.value("pattern", std::string {});
    if (pattern.empty())
        return std::unexpected(ToolError { .message = "Missing required parameter: pattern" });

    auto const mode = arguments.value("mode", std::string { "content" });

    auto basePath = std::filesystem::path(arguments.value("path", std::string {}));
    if (basePath.empty())
        basePath = std::filesystem::current_path();

    auto ec = std::error_code {};
    if (!std::filesystem::exists(basePath, ec))
        return std::unexpected(ToolError { .message = std::format("Path not found: {}", basePath.string()) });

    auto const globFilter = arguments.value("glob", std::string {});
    auto const typeFilter = arguments.value("type", std::string {});
    auto const caseInsensitive = arguments.value("case_insensitive", false);
    auto const multiline = arguments.value("multiline", false);
    auto const offset = static_cast<size_t>(std::max(0, arguments.value("offset", 0)));

    // Resolve context lines
    auto const contextSym = std::max(0, arguments.value("context", 0));
    auto const contextBefore =
        arguments.contains("context_before") ? std::max(0, arguments.value("context_before", 0)) : contextSym;
    auto const contextAfter =
        arguments.contains("context_after") ? std::max(0, arguments.value("context_after", 0)) : contextSym;

    // Validate type filter
    auto const typeGlobs = typeFilter.empty() ? std::vector<std::string_view> {} : fileTypeGlobs(typeFilter);
    if (!typeFilter.empty() && typeGlobs.empty())
        return std::unexpected(ToolError { .message = std::format("Unknown file type: {}", typeFilter) });

    if (mode == "files")
    {
        auto const limit = arguments.contains("limit")
                               ? static_cast<size_t>(std::max(1, arguments.value("limit", 0)))
                               : DefaultFilesLimit;
        return executeFilesMode(basePath, pattern, typeGlobs, limit, offset);
    }
    else if (mode == "content")
    {
        auto const limit = arguments.contains("limit")
                               ? static_cast<size_t>(std::max(1, arguments.value("limit", 0)))
                               : DefaultContentLimit;
        return executeContentMode(basePath,
                                  pattern,
                                  globFilter,
                                  typeGlobs,
                                  caseInsensitive,
                                  contextBefore,
                                  contextAfter,
                                  multiline,
                                  limit,
                                  offset);
    }
    else if (mode == "files_with_matches")
    {
        auto const limit = arguments.contains("limit")
                               ? static_cast<size_t>(std::max(1, arguments.value("limit", 0)))
                               : DefaultFilesLimit;
        return executeFilesWithMatchesMode(
            basePath, pattern, globFilter, typeGlobs, caseInsensitive, multiline, limit, offset);
    }
    else if (mode == "count")
    {
        auto const limit = arguments.contains("limit")
                               ? static_cast<size_t>(std::max(1, arguments.value("limit", 0)))
                               : DefaultFilesLimit;
        return executeCountMode(
            basePath, pattern, globFilter, typeGlobs, caseInsensitive, multiline, limit, offset);
    }
    else
    {
        return std::unexpected(ToolError {
            .message = std::format(
                "Invalid mode: \"{}\". Must be one of: files, content, files_with_matches, count", mode) });
    }
}

} // namespace endo::agent
