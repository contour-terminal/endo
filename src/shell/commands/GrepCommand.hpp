// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <regex>
#include <span>
#include <stop_token>
#include <string>
#include <vector>

#include <platform/FileSystem.hpp>

namespace endo::grep
{

/// Color output mode for grep results.
enum class ColorMode : uint8_t
{
    Never,  ///< Never colorize output
    Always, ///< Always colorize output
    Auto,   ///< Colorize when output is a terminal
};

/// Controls whether filenames are shown in output.
enum class FilenameMode : uint8_t
{
    Auto,   ///< Show when multiple files
    Always, ///< Always show (-H)
    Never,  ///< Never show (-h)
};

/// All parsed grep command-line options.
struct GrepOptions
{
    std::vector<std::string> patterns;     ///< Patterns to search for (-e or positional)
    std::vector<std::string> files;        ///< Files/directories to search
    bool fixedStrings = false;             ///< -F: treat patterns as fixed strings
    bool extendedRegex = true;             ///< -E: extended regex (default)
    bool ignoreCase = false;               ///< -i: case-insensitive matching
    bool wordRegexp = false;               ///< -w: match whole words only
    bool lineRegexp = false;               ///< -x: match whole lines only
    bool countOnly = false;                ///< -c: print count of matching lines
    bool filesWithMatches = false;         ///< -l: print only filenames with matches
    bool filesWithoutMatch = false;        ///< -L: print only filenames without matches
    bool lineNumbers = false;              ///< -n: prefix output with line numbers
    bool onlyMatching = false;             ///< -o: print only the matching parts
    bool invertMatch = false;              ///< -v: invert match (select non-matching lines)
    bool quiet = false;                    ///< -q/--quiet/--silent: suppress output
    bool suppressErrors = false;           ///< -s: suppress error messages
    bool recursive = false;                ///< -r/-R/--recursive: recurse into directories
    bool skipBinary = false;               ///< -I: skip binary files
    bool showHelp = false;                 ///< --help: show help text
    int afterContext = 0;                  ///< -A NUM: lines of trailing context
    int beforeContext = 0;                 ///< -B NUM: lines of leading context
    int bothContext = 0;                   ///< -C NUM: lines of context (before + after)
    int maxCount = 0;                      ///< -m NUM: stop after NUM matches per file (0 = unlimited)
    ColorMode colorMode = ColorMode::Auto; ///< --color=auto|always|never
    FilenameMode filenameMode = FilenameMode::Auto; ///< -H/-h filename display mode
    std::vector<std::string> includeGlobs;          ///< --include=GLOB patterns
    std::vector<std::string> excludeGlobs;          ///< --exclude=GLOB patterns
    std::vector<std::string> excludeDirs;           ///< --exclude-dir=DIR patterns

    /// @return Effective after-context lines (max of -A and -C).
    [[nodiscard]] int effectiveAfterContext() const { return std::max(afterContext, bothContext); }

    /// @return Effective before-context lines (max of -B and -C).
    [[nodiscard]] int effectiveBeforeContext() const { return std::max(beforeContext, bothContext); }

    /// @return Whether to show filenames given the file count.
    [[nodiscard]] bool showFilename(size_t fileCount) const
    {
        if (filenameMode == FilenameMode::Always)
            return true;
        if (filenameMode == FilenameMode::Never)
            return false;
        return fileCount > 1;
    }
};

/// Callback type for writing output.
using OutputWriter = std::function<void(std::string_view)>;

/// Callback type for writing error messages.
using ErrorWriter = std::function<void(std::string_view)>;

/// Parses grep command-line arguments into a GrepOptions struct.
/// @param args Command arguments (excluding "grep" itself).
/// @return Parsed options, or error message string.
[[nodiscard]] std::expected<GrepOptions, std::string> parseGrepArgs(std::span<std::string const> args);

/// Builds a compiled regex from the parsed grep options.
/// @param opts Parsed grep options containing patterns and flags.
/// @return Compiled regex, or error message string.
[[nodiscard]] std::expected<std::regex, std::string> buildRegex(GrepOptions const& opts);

/// Checks if a file appears to be binary by scanning for null bytes.
/// @param path Path to the file to check.
/// @return true if the file appears to be binary.
[[nodiscard]] bool isBinaryFile(std::filesystem::path const& path);

/// Collects the list of files to search based on options.
///
/// Recursion is driven through the injected FileSystem's lazy coroutine walk
/// (`walkDirectoryRecursive`), so a large tree is enumerated incrementally and
/// can be aborted: the walk polls for a pending Ctrl+C (drained from the OS, so
/// it works on Linux too) and for @p stopToken between entries, returning the
/// files gathered so far when either is observed.
///
/// @param fs FileSystem abstraction used to test paths and walk directories.
/// @param opts Parsed grep options with files and recursion settings.
/// @param errWriter Callback for error messages.
/// @param hasError Set to true if any error occurred.
/// @param stopToken Optional external cancellation; collection stops early when
///                  stop is requested (used by tests; default = no cancellation).
/// @return Vector of file paths to search (possibly partial if interrupted).
[[nodiscard]] std::vector<std::filesystem::path> collectFiles(platform::FileSystem const& fs,
                                                              GrepOptions const& opts,
                                                              ErrorWriter const& errWriter,
                                                              bool& hasError,
                                                              std::stop_token stopToken = {});

/// Searches lines for regex matches and writes formatted output.
/// @param lines The lines of text to search (without trailing newlines).
/// @param regex The compiled regex to match against.
/// @param opts The grep options controlling output format.
/// @param filename The filename for output prefixing (empty for stdin).
/// @param showFilename Whether to prefix output with filename.
/// @param useColor Whether to colorize the output.
/// @param writer Callback for writing output.
/// @param stopToken Optional external cancellation; searching stops early when a
///                  pending Ctrl+C or stop request is observed (default = none).
/// @return Number of matching lines found (possibly partial if interrupted).
[[nodiscard]] size_t searchLines(std::vector<std::string> const& lines,
                                 std::regex const& regex,
                                 GrepOptions const& opts,
                                 std::string_view filename,
                                 bool showFilename,
                                 bool useColor,
                                 OutputWriter const& writer,
                                 std::stop_token stopToken = {});

} // namespace endo::grep
