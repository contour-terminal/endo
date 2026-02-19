// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tui/completer/CompletionProvider.hpp>

#include <cstddef>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace endo::agent
{

/// @brief Completion provider for @-mention file path references in agent mode.
///
/// When the user types `@` followed by a query, this provider matches project file paths
/// using smart-case prefix matching (priority) and fuzzy matching (fallback). The `@` must
/// appear at the start of input or after whitespace, and no whitespace may appear between
/// the `@` and the cursor.
///
/// File paths are populated asynchronously after project context loading completes.
/// Thread-safe: `setFilePaths()` may be called from a background thread.
class FilePathCompleter final: public tui::CompletionProvider
{
  public:
    /// @brief Updates the set of project file paths available for completion.
    /// @param paths Flat list of relative file paths (e.g., from `git ls-files`).
    void setFilePaths(std::vector<std::string> paths);

    /// @brief Generates completions for @-mention file path input.
    /// @param input The full input text.
    /// @param cursorPosition The cursor byte offset in the input.
    /// @return Completion items for matching file paths, or empty if not in @-context.
    [[nodiscard]] std::vector<tui::CompletionItem> complete(std::string_view input,
                                                            size_t cursorPosition) override;

    /// @brief Returns priority between slash commands (100) and history (50).
    [[nodiscard]] int priority() const override { return 75; }

  private:
    std::vector<std::string> _filePaths;
    mutable std::mutex _mutex; ///< Guards _filePaths for thread-safe updates.

    /// @brief Finds the position of the triggering `@` character, if any.
    ///
    /// The `@` must be at position 0 or preceded by whitespace. No whitespace
    /// may appear between the `@` and the cursor position.
    ///
    /// @param input The full input text.
    /// @param cursorPosition The cursor byte offset.
    /// @return The byte offset of `@`, or std::string_view::npos if not in @-context.
    [[nodiscard]] static size_t findAtPosition(std::string_view input, size_t cursorPosition);
};

} // namespace endo::agent
