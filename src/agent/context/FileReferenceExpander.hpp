// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace endo::agent
{

/// A single parsed @-file reference from a user message.
struct FileReference
{
    std::string originalText;           ///< e.g. "@src/foo.cpp:10-50"
    std::filesystem::path resolvedPath; ///< Absolute path after resolution
    std::optional<int> startLine;       ///< Optional 1-based start line
    std::optional<int> endLine;         ///< Optional 1-based end line
};

/// Result of expanding @-file references in a user message.
struct FileExpansionResult
{
    std::string expandedMessage; ///< Original text + appended file contents
    size_t fileCount = 0;        ///< Number of files successfully injected
};

/// Stateless utility that parses @-file references from user messages,
/// reads the referenced files, and produces an expanded message with
/// file contents appended as XML-style context blocks.
///
/// Supports:
/// - `@path/to/file` — full file
/// - `@path/to/file:N` — single line
/// - `@path/to/file:N-M` — line range
class FileReferenceExpander
{
  public:
    /// Maximum number of lines to read per file.
    static constexpr auto maxLinesPerFile = 2000;

    /// Parse all @-file references from user input.
    ///
    /// The `@` must be at position 0 or preceded by whitespace.
    /// The path extends to the next whitespace or end of string.
    ///
    /// @param text The raw user message text.
    /// @return Parsed references with unresolved (relative) paths.
    [[nodiscard]] static auto parse(std::string_view text) -> std::vector<FileReference>;

    /// Read a file's contents, respecting optional line range and limits.
    ///
    /// Returns formatted content with line numbers (cat -n style).
    ///
    /// @param ref The file reference to read.
    /// @param maxLines Maximum lines to include (default: 2000).
    /// @return File contents on success, error message on failure.
    [[nodiscard]] static auto readFile(FileReference const& ref, int maxLines = maxLinesPerFile)
        -> std::expected<std::string, std::string>;

    /// Expand all @-file references: parse, read files, append contents.
    ///
    /// The original message text is preserved unchanged; file contents
    /// are appended after a blank line using XML-style `<file>` tags.
    ///
    /// @param message The raw user message.
    /// @param cwd Working directory for resolving relative paths.
    /// @return The expanded message and count of files injected.
    [[nodiscard]] static auto expand(std::string_view message, std::filesystem::path const& cwd)
        -> FileExpansionResult;
};

} // namespace endo::agent
