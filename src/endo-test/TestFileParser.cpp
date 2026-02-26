// SPDX-License-Identifier: Apache-2.0
#include "TestFileParser.hpp"

#include <fstream>
#include <sstream>
#include <string_view>

namespace endo::test
{

namespace
{

    /// Trims leading and trailing whitespace from a string_view.
    [[nodiscard]] constexpr std::string_view trim(std::string_view sv) noexcept
    {
        while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t'))
            sv.remove_prefix(1);
        while (!sv.empty() && (sv.back() == ' ' || sv.back() == '\t'))
            sv.remove_suffix(1);
        return sv;
    }

    /// Attempts to parse a directive value from a comment line.
    /// Returns the value if the line matches "# key: value", std::nullopt otherwise.
    [[nodiscard]] std::optional<std::string_view> parseDirective(std::string_view line,
                                                                 std::string_view key) noexcept
    {
        // Line must start with '#'
        auto trimmedLine = trim(line);
        if (!trimmedLine.starts_with('#'))
            return std::nullopt;

        auto afterHash = trim(trimmedLine.substr(1));
        if (!afterHash.starts_with(key))
            return std::nullopt;

        auto afterKey = afterHash.substr(key.size());
        if (afterKey.empty() || afterKey.front() != ':')
            return std::nullopt;

        return trim(afterKey.substr(1));
    }

} // namespace

std::optional<TestFile> TestFileParser::parse(std::filesystem::path const& filePath,
                                              std::string const& relativePath)
{
    std::ifstream file(filePath);
    if (!file.is_open())
        return std::nullopt;

    TestFile result;
    result.path = filePath;
    result.relativePath = relativePath;
    result.description = relativePath; // Default description

    std::string sessionSeparator;
    bool inMetadata = true;
    std::ostringstream sourceStream;
    std::string line;

    while (std::getline(file, line))
    {
        if (inMetadata)
        {
            auto trimmedLine = trim(line);

            // Empty lines in metadata section are allowed (skipped)
            if (trimmedLine.empty())
                continue;

            // Non-comment line ends metadata section
            if (!trimmedLine.starts_with('#'))
            {
                inMetadata = false;
                sourceStream << line << '\n';
                continue;
            }

            // Try to parse known directives
            if (auto val = parseDirective(line, "description"))
            {
                result.description = std::string(*val);
                continue;
            }
            if (auto val = parseDirective(line, "expect-exit"))
            {
                result.expectedExitCode = std::stoll(std::string(*val));
                continue;
            }
            if (auto val = parseDirective(line, "expect-error"))
            {
                result.expectedErrors.emplace_back(*val);
                continue;
            }
            if (auto val = parseDirective(line, "expect"))
            {
                result.expectedOutput.emplace_back(*val);
                continue;
            }
            if (auto val = parseDirective(line, "mode"))
            {
                auto modeStr = *val;
                if (modeStr == "ir-only")
                    result.mode = TestMode::IROnly;
                else if (modeStr == "parse-only")
                    result.mode = TestMode::ParseOnly;
                else if (modeStr == "structured")
                    result.mode = TestMode::Structured;
                else
                    result.mode = TestMode::Execute;
                continue;
            }
            if (auto val = parseDirective(line, "skip"))
            {
                result.skipReason = std::string(*val);
                continue;
            }
            if (auto val = parseDirective(line, "session-separator"))
            {
                sessionSeparator = std::string(*val);
                result.isSessionTest = true;
                continue;
            }
            if (auto val = parseDirective(line, "mock-env"))
            {
                auto sv = *val;
                if (auto eqPos = sv.find('='); eqPos != std::string_view::npos)
                    result.mockEnv.emplace_back(sv.substr(0, eqPos), sv.substr(eqPos + 1));
                continue;
            }
            if (auto val = parseDirective(line, "mock-which"))
            {
                auto sv = *val;
                if (auto eqPos = sv.find('='); eqPos != std::string_view::npos)
                    result.mockWhichPaths.emplace_back(sv.substr(0, eqPos), sv.substr(eqPos + 1));
                continue;
            }
            if (auto val = parseDirective(line, "expect-env"))
            {
                auto sv = *val;
                if (auto eqPos = sv.find('='); eqPos != std::string_view::npos)
                    result.expectedEnv.emplace_back(sv.substr(0, eqPos), sv.substr(eqPos + 1));
                continue;
            }
            if (parseDirective(line, "expect-nonempty"))
            {
                result.expectNonEmptyOutput = true;
                continue;
            }
            if (parseDirective(line, "unused-detection"))
            {
                result.unusedValueDetection = true;
                continue;
            }

            // Unknown directive or plain comment — skip
            continue;
        }

        // Past metadata — accumulate source
        sourceStream << line << '\n';
    }

    result.source = sourceStream.str();

    // Remove trailing newline if present
    if (!result.source.empty() && result.source.back() == '\n')
        result.source.pop_back();

    // Split source into session prompts if session-separator was specified
    if (result.isSessionTest && !sessionSeparator.empty())
    {
        auto const separatorLine = "# " + sessionSeparator;
        std::istringstream sourceInput(result.source);
        std::ostringstream currentPrompt;
        std::string sourceLine;

        while (std::getline(sourceInput, sourceLine))
        {
            if (trim(sourceLine) == separatorLine)
            {
                auto prompt = currentPrompt.str();
                if (!prompt.empty() && prompt.back() == '\n')
                    prompt.pop_back();
                if (!prompt.empty())
                    result.sessionPrompts.push_back(std::move(prompt));
                currentPrompt.str("");
                currentPrompt.clear();
            }
            else
            {
                currentPrompt << sourceLine << '\n';
            }
        }

        // Add the last prompt
        auto lastPrompt = currentPrompt.str();
        if (!lastPrompt.empty() && lastPrompt.back() == '\n')
            lastPrompt.pop_back();
        if (!lastPrompt.empty())
            result.sessionPrompts.push_back(std::move(lastPrompt));
    }

    return result;
}

} // namespace endo::test
