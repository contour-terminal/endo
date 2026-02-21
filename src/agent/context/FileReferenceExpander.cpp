// SPDX-License-Identifier: Apache-2.0
#include <charconv>
#include <format>
#include <fstream>
#include <sstream>
#include <string>

#include <agent/context/FileReferenceExpander.hpp>

namespace endo::agent
{

auto FileReferenceExpander::parse(std::string_view text) -> std::vector<FileReference>
{
    auto refs = std::vector<FileReference> {};
    auto pos = size_t { 0 };

    while (pos < text.size())
    {
        auto const atPos = text.find('@', pos);
        if (atPos == std::string_view::npos)
            break;

        // '@' must be at position 0 or preceded by whitespace
        if (atPos > 0 && !std::isspace(static_cast<unsigned char>(text[atPos - 1])))
        {
            pos = atPos + 1;
            continue;
        }

        // Extract token from '@' to next whitespace or end
        auto const afterAt = atPos + 1;
        if (afterAt >= text.size())
        {
            pos = afterAt;
            continue;
        }

        // Path must not start with whitespace
        if (std::isspace(static_cast<unsigned char>(text[afterAt])))
        {
            pos = afterAt;
            continue;
        }

        auto endPos = afterAt;
        while (endPos < text.size() && !std::isspace(static_cast<unsigned char>(text[endPos])))
            ++endPos;

        auto const token = text.substr(afterAt, endPos - afterAt);
        if (token.empty())
        {
            pos = endPos;
            continue;
        }

        auto ref = FileReference {};
        ref.originalText = std::string(text.substr(atPos, endPos - atPos));

        // Check for line range suffix: path:N or path:N-M
        auto pathStr = std::string {};
        if (auto const colonPos = token.rfind(':'); colonPos != std::string_view::npos && colonPos > 0)
        {
            auto const suffix = token.substr(colonPos + 1);
            auto startLine = int {};
            auto const [ptr, ec] = std::from_chars(suffix.data(), suffix.data() + suffix.size(), startLine);
            if (ec == std::errc {} && ptr != suffix.data())
            {
                // Successfully parsed at least the start line
                ref.startLine = startLine;
                pathStr = std::string(token.substr(0, colonPos));

                if (ptr < suffix.data() + suffix.size() && *ptr == '-')
                {
                    auto endLine = int {};
                    auto const [ptr2, ec2] = std::from_chars(ptr + 1, suffix.data() + suffix.size(), endLine);
                    if (ec2 == std::errc {} && ptr2 == suffix.data() + suffix.size())
                        ref.endLine = endLine;
                    else
                        ref.endLine = startLine; // @path:N with no valid end means single line
                }
                else if (ptr == suffix.data() + suffix.size())
                {
                    // Single line: @path:N
                    ref.endLine = startLine;
                }
                else
                {
                    // Colon suffix wasn't a valid line range, treat whole token as path
                    pathStr = std::string(token);
                    ref.startLine.reset();
                }
            }
            else
            {
                // Not a line number after colon, treat whole token as path
                pathStr = std::string(token);
            }
        }
        else
        {
            pathStr = std::string(token);
        }

        ref.resolvedPath = std::filesystem::path(pathStr);
        refs.push_back(std::move(ref));
        pos = endPos;
    }

    return refs;
}

auto FileReferenceExpander::readFile(FileReference const& ref, int const maxLines)
    -> std::expected<std::string, std::string>
{
    auto ec = std::error_code {};
    if (!std::filesystem::exists(ref.resolvedPath, ec))
        return std::unexpected("File not found");

    if (!std::filesystem::is_regular_file(ref.resolvedPath, ec))
        return std::unexpected("Not a regular file");

    auto file = std::ifstream(ref.resolvedPath);
    if (!file)
        return std::unexpected("Cannot open file");

    auto const startLine = ref.startLine.value_or(1);
    auto const endLine = ref.endLine.value_or(std::numeric_limits<int>::max());

    auto result = std::ostringstream {};
    auto line = std::string {};
    auto lineNum = 0;
    auto linesEmitted = 0;
    auto totalLines = 0;

    while (std::getline(file, line))
    {
        ++lineNum;
        if (lineNum < startLine)
            continue;
        if (lineNum > endLine)
            break;

        ++totalLines;
        if (linesEmitted < maxLines)
        {
            // Truncate individual lines at 2000 characters
            if (line.size() > 2000)
                line = line.substr(0, 2000);
            result << std::format("{:6}\t{}\n", lineNum, line);
            ++linesEmitted;
        }
    }

    // Count remaining lines if we hit the limit before endLine
    if (linesEmitted >= maxLines)
    {
        while (std::getline(file, line))
        {
            ++lineNum;
            if (lineNum > endLine)
                break;
            ++totalLines;
        }
    }

    auto content = result.str();
    auto const truncatedCount = totalLines - linesEmitted;
    if (truncatedCount > 0)
        content += std::format("[truncated — {} more lines omitted]\n", truncatedCount);

    return content;
}

auto FileReferenceExpander::stripExpansions(std::string_view text) -> std::string
{
    if (auto const pos = text.find("\n\n<file "); pos != std::string_view::npos)
    {
        // Trim trailing whitespace from the original portion
        auto end = pos;
        while (end > 0 && std::isspace(static_cast<unsigned char>(text[end - 1])))
            --end;
        return std::string(text.substr(0, end));
    }
    return std::string(text);
}

auto FileReferenceExpander::expand(std::string_view message, std::filesystem::path const& cwd)
    -> FileExpansionResult
{
    auto refs = parse(message);
    if (refs.empty())
        return { .expandedMessage = std::string(message), .fileCount = 0 };

    // Resolve relative paths against cwd
    for (auto& ref: refs)
    {
        if (ref.resolvedPath.is_relative())
            ref.resolvedPath = cwd / ref.resolvedPath;
    }

    auto result = std::ostringstream {};
    result << message;

    auto fileCount = size_t { 0 };
    for (auto const& ref: refs)
    {
        result << "\n\n";

        // Build relative path for display
        auto displayPath = std::filesystem::relative(ref.resolvedPath, cwd);
        if (displayPath.empty())
            displayPath = ref.resolvedPath;

        auto const content = readFile(ref);
        if (content.has_value())
        {
            result << "<file path=\"" << displayPath.string() << "\"";
            if (ref.startLine.has_value())
            {
                result << " lines=\"" << *ref.startLine;
                if (ref.endLine.has_value() && *ref.endLine != *ref.startLine)
                    result << "-" << *ref.endLine;
                result << "\"";
            }
            result << ">\n" << *content << "</file>";
            ++fileCount;
        }
        else
        {
            result << "<file path=\"" << displayPath.string() << "\" error=\"" << content.error() << "\"/>";
        }
    }

    return { .expandedMessage = result.str(), .fileCount = fileCount };
}

} // namespace endo::agent
