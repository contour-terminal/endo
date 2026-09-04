// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <shell/completion/CommandQueryProvider.hpp>

#include <chrono>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace endo
{

/// @brief Generic TTL cache for external command query results.
///
/// Wraps a CommandQueryProvider and caches results per queryTag with
/// configurable TTL. Stale entries are lazily refreshed on next access.
class QueryCache
{
  public:
    /// @brief Constructs a cache around a query provider.
    /// @param provider The underlying provider to cache results from.
    /// @param ttl Time-to-live for cached entries (default: 2 seconds).
    explicit QueryCache(std::unique_ptr<CommandQueryProvider> provider,
                        std::chrono::milliseconds ttl = std::chrono::milliseconds { 2000 });

    // Move-only (because of unique_ptr member)
    QueryCache(QueryCache&&) noexcept = default;
    QueryCache& operator=(QueryCache&&) noexcept = default;

    /// @brief Queries with caching. Returns cached data if still valid.
    ///
    /// Returned by reference: a result set can hold thousands of entries (every executable
    /// on $PATH), and copying it on every call — cache hits included — dominated the cost
    /// of the lookup it was meant to avoid.
    ///
    /// @param queryTag The query tag to resolve.
    /// @return Cached or freshly queried results, owned by this cache. Valid until the next
    ///         query() or invalidateAll() call.
    [[nodiscard]] std::vector<QueryResult> const& query(std::string_view queryTag);

    /// @brief Invalidates all cached entries.
    void invalidateAll();

  private:
    std::unique_ptr<CommandQueryProvider> _provider;
    std::chrono::milliseconds _ttl;

    struct CacheEntry
    {
        std::vector<QueryResult> data;
        std::chrono::steady_clock::time_point timestamp;
    };

    std::unordered_map<std::string, CacheEntry> _entries;
};

} // namespace endo
