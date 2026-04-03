// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace endo::test
{

/// Execution mode for a test file.
enum class TestMode : std::uint8_t
{
    Execute,    ///< Full pipeline: parse -> IR -> codegen -> execute
    IROnly,     ///< Parse and generate IR only, no execution
    ParseOnly,  ///< Parse only, no IR generation
    Structured, ///< Execute with pre-populated structured command state
};

/// Parsed metadata and source from an .endo test file.
struct TestFile
{
    std::filesystem::path path;              ///< Absolute path to the .endo file
    std::string relativePath;                ///< Path relative to test root (for display)
    std::string description;                 ///< Human-readable description (defaults to filename)
    std::vector<std::string> expectedOutput; ///< Expected output lines (joined with \n)
    int64_t expectedExitCode = 0;            ///< Expected exit code (default: 0)
    std::vector<std::string> expectedErrors; ///< Expected compilation error substrings
    TestMode mode = TestMode::Execute;       ///< Execution mode
    std::optional<std::string> skipReason;   ///< If set, test is skipped with this reason
    std::string source;                      ///< Source code (everything after metadata)
    std::vector<std::string> sessionPrompts; ///< For session tests: source split at separator
    bool isSessionTest = false;              ///< True if session-separator was specified
    std::vector<std::pair<std::string, std::string>> mockEnv;        ///< Mock environment variables
    std::vector<std::pair<std::string, std::string>> mockWhichPaths; ///< Mock which paths
    std::vector<std::pair<std::string, std::string>> expectedEnv;    ///< Expected env vars after execution
    bool expectNonEmptyOutput = false;    ///< Assert output is non-empty (no exact match)
    bool unusedValueDetection = false;    ///< Enable unused value detection during IR generation
    std::vector<std::string> modulePaths; ///< Additional module search paths

    /// Auxiliary source files embedded in test (for multi-file module tests).
    /// Each pair is (filename, content). Written to a temp dir at execution time.
    std::vector<std::pair<std::string, std::string>> auxiliaryFiles;

    /// External source files to load as session prompts before the test source.
    /// Paths are relative to the project source root (ENDO_SOURCE_DIR).
    std::vector<std::string> sourceFiles;
};

/// Parses an .endo test file into its metadata directives and source code.
class TestFileParser
{
  public:
    /// Parses the file at the given path.
    ///
    /// @param filePath     Absolute path to the .endo file
    /// @param relativePath Path relative to test root (for display and default description)
    /// @return Parsed test file, or std::nullopt on read failure
    [[nodiscard]] static std::optional<TestFile> parse(std::filesystem::path const& filePath,
                                                       std::string const& relativePath);
};

} // namespace endo::test
