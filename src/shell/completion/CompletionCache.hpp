// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <shell/completion/ScriptedCompleter.hpp>

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace endo::platform
{
class FileSystem;
}

namespace endo
{

/// @brief A cached completion result together with when it was produced.
///
/// The timestamp is wall-clock (@c std::chrono::system_clock) rather than
/// @c steady_clock because the on-disk cache must survive process restarts, and
/// a monotonic clock resets every run. Freshness (fresh / stale-while-revalidate /
/// expired) is decided by the consumer (@c ScriptedCompleter) comparing this
/// timestamp against an injected wall-clock now — the cache itself stores and
/// returns entries verbatim and applies no TTL of its own.
struct CachedCompletions
{
    std::vector<CollectedCompletion> results;        ///< The cached completion candidates.
    std::chrono::system_clock::time_point timestamp; ///< When the results were produced.
};

/// @brief Persistent store for scripted-completer results, keyed by an opaque string.
///
/// A dependency-injection seam so the caching policy is swappable: the production
/// implementation (@c FileSystemCompletionCache) persists to the user's XDG cache
/// directory; tests inject an in-memory fake. The interface is deliberately a dumb
/// key/value store — TTL and stale-while-revalidate policy live in the consumer.
///
/// The key is the same prefix-excluded key @c ScriptedCompleter already builds
/// (@c makeCacheKey over funcName + args + optionPrefix), so a warm entry serves
/// every typed prefix without re-fetching.
class CompletionCache
{
  public:
    virtual ~CompletionCache() = default;

    /// @brief Loads the cached entry for @p key.
    /// @param key Opaque cache key.
    /// @return The cached entry, or std::nullopt on a miss (absent / unreadable / corrupt).
    [[nodiscard]] virtual std::optional<CachedCompletions> load(std::string_view key) const = 0;

    /// @brief Stores @p entry under @p key, replacing any prior entry.
    /// @param key Opaque cache key.
    /// @param entry The entry to persist. Best-effort: storage failures are swallowed.
    virtual void store(std::string_view key, CachedCompletions const& entry) const = 0;
};

/// @brief In-memory @c CompletionCache. Used by tests and as a non-persistent fallback.
class InMemoryCompletionCache final: public CompletionCache
{
  public:
    [[nodiscard]] std::optional<CachedCompletions> load(std::string_view key) const override;
    void store(std::string_view key, CachedCompletions const& entry) const override;

  private:
    mutable std::unordered_map<std::string, CachedCompletions> _entries;
};

/// @brief @c CompletionCache that persists one file per key under a cache directory.
///
/// Each entry is written to `<cacheDir>/<hash(key)>` where the filename is a stable
/// hash of the key (keys contain NUL-delimited args and arbitrary command names, so
/// they are not path-safe). The raw key is stored inside the file and verified on
/// load, so a hash collision is a miss rather than a wrong hit. Writes are atomic
/// (temp file + rename) so a concurrent reader never observes a half-written entry
/// and a crashed writer never corrupts a sibling key.
class FileSystemCompletionCache final: public CompletionCache
{
  public:
    /// @brief Constructs a filesystem-backed cache.
    /// @param fs Filesystem abstraction for all I/O (never touches the OS directly).
    /// @param cacheDir Directory that holds the per-key entry files. Created lazily on first store.
    FileSystemCompletionCache(platform::FileSystem const& fs, std::filesystem::path cacheDir);

    [[nodiscard]] std::optional<CachedCompletions> load(std::string_view key) const override;
    void store(std::string_view key, CachedCompletions const& entry) const override;

    /// @brief Serializes an entry to the on-disk text format. Exposed for testing.
    [[nodiscard]] static std::string serialize(std::string_view key, CachedCompletions const& entry);

    /// @brief Parses the on-disk text format, verifying it belongs to @p expectedKey.
    /// @return The parsed entry, or std::nullopt if malformed or the stored key differs.
    [[nodiscard]] static std::optional<CachedCompletions> deserialize(std::string_view content,
                                                                      std::string_view expectedKey);

    /// @brief Maps a cache key to its per-key filename (a stable hex hash).
    [[nodiscard]] static std::string fileNameForKey(std::string_view key);

  private:
    platform::FileSystem const& _fs;
    std::filesystem::path _cacheDir;
};

} // namespace endo
