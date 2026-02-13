// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace endo
{

/// @brief Abstract history interface for completion and recall.
///
/// Provides a testable abstraction over command history storage,
/// supporting both in-memory and persistent implementations.
class History
{
  public:
    virtual ~History() = default;

    /// @brief Adds an entry to the history.
    /// @param entry The command line to add.
    virtual void add(std::string entry) = 0;

    /// @brief Returns all history entries (oldest first).
    [[nodiscard]] virtual std::vector<std::string> const& entries() const = 0;

    /// @brief Returns the number of entries in history.
    [[nodiscard]] virtual size_t size() const = 0;

    /// @brief Returns the maximum number of entries to store.
    [[nodiscard]] virtual size_t maxSize() const = 0;

    /// @brief Clears all history entries.
    virtual void clear() = 0;

    /// @brief Reports the exit code of the last added command.
    ///
    /// This allows persistent history implementations to decide whether
    /// to save a command based on whether it succeeded. The default
    /// implementation is a no-op for in-memory history.
    /// @param exitCode The exit code of the last command (0 = success).
    virtual void markLastResult(int /*exitCode*/) {}

    /// @brief Searches for entries matching a prefix.
    /// @param prefix The prefix to search for.
    /// @param maxResults Maximum number of results to return.
    /// @return Matching entries ordered by recency (newest first).
    [[nodiscard]] virtual std::vector<std::string_view> search(std::string_view prefix,
                                                               size_t maxResults = 10) const = 0;

    /// @brief Result from fuzzy history search.
    struct FuzzySearchResult
    {
        std::string_view entry;        ///< The matched history entry.
        std::vector<size_t> positions; ///< Grapheme indices of matched characters.
        int score;                     ///< Match score (higher = better).
        bool isPrefixMatch;            ///< True if this is a prefix match (vs fuzzy).
    };

    /// @brief Searches for entries using both prefix and fuzzy matching.
    /// @param prefix The pattern to search for.
    /// @param maxResults Maximum number of results to return.
    /// @return Matching entries ordered by score (highest first), then recency.
    [[nodiscard]] virtual std::vector<FuzzySearchResult> searchFuzzy(std::string_view prefix,
                                                                     size_t maxResults = 10) const = 0;
};

/// @brief In-memory history implementation (current session only).
class InMemoryHistory: public History
{
  public:
    /// @brief Constructs an in-memory history with the given maximum size.
    /// @param maxSize Maximum number of entries to store (default: 1000).
    explicit InMemoryHistory(size_t maxSize = 1000);

    void add(std::string entry) override;
    [[nodiscard]] std::vector<std::string> const& entries() const override;
    [[nodiscard]] size_t size() const override;
    [[nodiscard]] size_t maxSize() const override;
    void clear() override;
    [[nodiscard]] std::vector<std::string_view> search(std::string_view prefix,
                                                       size_t maxResults = 10) const override;
    [[nodiscard]] std::vector<FuzzySearchResult> searchFuzzy(std::string_view prefix,
                                                             size_t maxResults = 10) const override;

  private:
    std::vector<std::string> _entries;
    size_t _maxSize;
};

} // namespace endo
