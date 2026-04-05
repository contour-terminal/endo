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

    /// Strips a single trailing newline from a string, if present.
    void stripTrailingNewline(std::string& s) noexcept
    {
        if (!s.empty() && s.back() == '\n')
            s.pop_back();
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

    // Aux-file accumulation state
    bool inAuxFile = false;
    std::string currentAuxFileName;
    std::ostringstream currentAuxContent;

    while (std::getline(file, line))
    {
        // Check for aux-file / main-file markers at any point (even past metadata)
        if (auto val = parseDirective(line, "aux-file"))
        {
            // Finalize previous aux file if any
            if (inAuxFile && !currentAuxFileName.empty())
            {
                auto content = currentAuxContent.str();
                stripTrailingNewline(content);
                result.auxiliaryFiles.emplace_back(std::move(currentAuxFileName), std::move(content));
                currentAuxContent.str("");
                currentAuxContent.clear();
            }
            currentAuxFileName = std::string(*val);
            inAuxFile = true;
            inMetadata = false; // aux-file ends the metadata section
            continue;
        }

        if (parseDirective(line, "main-file").has_value())
        {
            // Finalize current aux file
            if (inAuxFile && !currentAuxFileName.empty())
            {
                auto content = currentAuxContent.str();
                stripTrailingNewline(content);
                result.auxiliaryFiles.emplace_back(std::move(currentAuxFileName), std::move(content));
                currentAuxContent.str("");
                currentAuxContent.clear();
            }
            inAuxFile = false;
            // Rest of file is main source
            continue;
        }

        if (inAuxFile)
        {
            currentAuxContent << line << '\n';
            continue;
        }

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
            if (auto val = parseDirective(line, "expect-expr"))
            {
                result.expectExpr = std::string(*val);
                continue;
            }
            if (parseDirective(line, "unused-detection"))
            {
                result.unusedValueDetection = true;
                continue;
            }
            if (auto val = parseDirective(line, "module-path"))
            {
                result.modulePaths.emplace_back(*val);
                continue;
            }
            if (auto val = parseDirective(line, "source-file"))
            {
                result.sourceFiles.emplace_back(*val);
                continue;
            }

            // Unknown directive or plain comment — skip
            continue;
        }

        // Past metadata — accumulate source
        sourceStream << line << '\n';
    }

    // Finalize any trailing aux file (no main-file marker at end)
    if (inAuxFile && !currentAuxFileName.empty())
    {
        auto content = currentAuxContent.str();
        if (!content.empty() && content.back() == '\n')
            content.pop_back();
        result.auxiliaryFiles.emplace_back(std::move(currentAuxFileName), std::move(content));
    }

    result.source = sourceStream.str();

    // Remove trailing newline if present
    stripTrailingNewline(result.source);

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
                stripTrailingNewline(prompt);
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
        stripTrailingNewline(lastPrompt);
        if (!lastPrompt.empty())
            result.sessionPrompts.push_back(std::move(lastPrompt));
    }

    // Load external source files and inject as session prompts
    if (!result.sourceFiles.empty())
    {
        std::vector<std::string> sourceFileContents;
        for (auto const& sourcePath: result.sourceFiles)
        {
#ifdef ENDO_SOURCE_DIR
            auto fullPath = std::filesystem::path(ENDO_SOURCE_DIR) / sourcePath;
#else
            auto fullPath = std::filesystem::path(sourcePath);
#endif
            std::ifstream srcFile(fullPath);
            if (!srcFile.is_open())
                return std::nullopt; // Source file not found — treat as parse failure
            auto content = std::string(std::istreambuf_iterator<char>(srcFile), {});
            stripTrailingNewline(content);
            sourceFileContents.push_back(std::move(content));
        }

        if (!sourceFileContents.empty())
        {
            if (result.isSessionTest && !result.sessionPrompts.empty())
            {
                // Insert source files AFTER any manual prompts that precede the test call,
                // but BEFORE the last prompt (the test invocation).
                auto lastPrompt = std::move(result.sessionPrompts.back());
                result.sessionPrompts.pop_back();
                for (auto& content: sourceFileContents)
                    result.sessionPrompts.push_back(std::move(content));
                result.sessionPrompts.push_back(std::move(lastPrompt));
            }
            else
            {
                // No session separator — create implicit session: [sourceFiles..., testSource]
                result.isSessionTest = true;
                result.sessionPrompts.clear();
                for (auto& content: sourceFileContents)
                    result.sessionPrompts.push_back(std::move(content));
                result.sessionPrompts.push_back(result.source);
            }
        }
    }

    return result;
}

} // namespace endo::test
