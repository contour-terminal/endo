// SPDX-License-Identifier: Apache-2.0
#include <shell/completion/CompletionCache.hpp>

#include <crispy/FNV.h>
#include <crispy/escape.h>

#include <charconv>
#include <cstdint>
#include <format>
#include <string>
#include <utility>

#include <platform/FileSystem.hpp>

namespace endo
{

namespace
{
    /// On-disk format version. Bump when the serialized layout changes so stale
    /// entries from an older endo are treated as a miss rather than mis-parsed.
    constexpr std::string_view FormatMagic = "endo-completion-cache/1";

    /// Field separator within a completion record. Completion text, descriptions, and
    /// details are @c crispy::escape'd, so any raw tab/newline is escaped and never
    /// collides with the record (`\n`) or field (`\t`) separators.
    constexpr char FieldSep = '\t';

    /// FNV-1a 64-bit hash rendered as 16 lowercase hex digits. Stable across runs and
    /// platforms (no seeding), so a key always maps to the same filename. Uses the
    /// shared @c crispy::fnv with explicit 64-bit basis/prime (its default ctor is
    /// 32-bit).
    std::string fnv1aHex(std::string_view s)
    {
        constexpr auto Hasher =
            crispy::fnv<char, std::uint64_t> { 0x00000100000001b3ULL, 0xcbf29ce484222325ULL };
        return std::format("{:016x}", Hasher(Hasher.basis(), s));
    }

    /// Splits @p text into its lines (dropping the terminators). A trailing newline
    /// does not yield a spurious empty final line.
    std::vector<std::string_view> splitLines(std::string_view text)
    {
        std::vector<std::string_view> lines;
        std::size_t start = 0;
        while (start <= text.size())
        {
            auto const nl = text.find('\n', start);
            if (nl == std::string_view::npos)
            {
                if (start < text.size())
                    lines.push_back(text.substr(start));
                break;
            }
            lines.push_back(text.substr(start, nl - start));
            start = nl + 1;
        }
        return lines;
    }
} // namespace

// --- InMemoryCompletionCache -------------------------------------------------

std::optional<CachedCompletions> InMemoryCompletionCache::load(std::string_view key) const
{
    if (auto const it = _entries.find(std::string(key)); it != _entries.end())
        return it->second;
    return std::nullopt;
}

void InMemoryCompletionCache::store(std::string_view key, CachedCompletions const& entry) const
{
    _entries[std::string(key)] = entry;
}

// --- FileSystemCompletionCache ----------------------------------------------

FileSystemCompletionCache::FileSystemCompletionCache(platform::FileSystem const& fs,
                                                     std::filesystem::path cacheDir):
    _fs(fs), _cacheDir(std::move(cacheDir))
{
}

std::string FileSystemCompletionCache::fileNameForKey(std::string_view key)
{
    return fnv1aHex(key);
}

std::string FileSystemCompletionCache::serialize(std::string_view key, CachedCompletions const& entry)
{
    auto const epochMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(entry.timestamp.time_since_epoch()).count();

    std::string out;
    out += FormatMagic;
    out += '\n';
    out += std::to_string(epochMs);
    out += '\n';
    out += crispy::escape(key);
    out += '\n';
    for (auto const& c: entry.results)
    {
        out += crispy::escape(c.text);
        out += FieldSep;
        out += crispy::escape(c.description);
        out += FieldSep;
        out += crispy::escape(c.detail);
        out += '\n';
    }
    return out;
}

std::optional<CachedCompletions> FileSystemCompletionCache::deserialize(std::string_view content,
                                                                        std::string_view expectedKey)
{
    auto const lines = splitLines(content);
    // Header: magic, timestamp, key. Records may be zero (an empty completion set).
    if (lines.size() < 3)
        return std::nullopt;
    if (lines[0] != FormatMagic)
        return std::nullopt;

    std::int64_t epochMs = 0;
    auto const& tsLine = lines[1];
    auto const parsed = std::from_chars(tsLine.data(), tsLine.data() + tsLine.size(), epochMs);
    if (parsed.ec != std::errc {} || parsed.ptr != tsLine.data() + tsLine.size())
        return std::nullopt;

    if (crispy::unescape(lines[2]) != expectedKey)
        return std::nullopt; // hash collision or unrelated file: treat as a miss

    CachedCompletions entry;
    entry.timestamp = std::chrono::system_clock::time_point(std::chrono::milliseconds(epochMs));
    entry.results.reserve(lines.size() - 3); // record count is known: one line per completion
    for (auto i = std::size_t { 3 }; i < lines.size(); ++i)
    {
        auto const line = lines[i];
        auto const sep1 = line.find(FieldSep);
        if (sep1 == std::string_view::npos)
            return std::nullopt;
        auto const sep2 = line.find(FieldSep, sep1 + 1);
        if (sep2 == std::string_view::npos)
            return std::nullopt;
        entry.results.push_back(
            CollectedCompletion { .text = crispy::unescape(line.substr(0, sep1)),
                                  .description = crispy::unescape(line.substr(sep1 + 1, sep2 - sep1 - 1)),
                                  .detail = crispy::unescape(line.substr(sep2 + 1)) });
    }
    return entry;
}

std::optional<CachedCompletions> FileSystemCompletionCache::load(std::string_view key) const
{
    auto const path = _cacheDir / fileNameForKey(key);
    if (!_fs.exists(path))
        return std::nullopt;
    auto const content = _fs.readFile(path);
    if (!content)
        return std::nullopt;
    return deserialize(*content, key);
}

void FileSystemCompletionCache::store(std::string_view key, CachedCompletions const& entry) const
{
    // Best-effort: any failure leaves the cache without this entry (a future miss),
    // never propagating an error into the completion path.
    if (auto const created = _fs.createDirectories(_cacheDir); !created)
        return;

    auto const serialized = serialize(key, entry);
    auto const target = _cacheDir / fileNameForKey(key);

    // Atomic publish: write to a temp file then rename over the target, so a reader
    // never sees a half-written entry and a concurrent writer is last-writer-wins.
    // The temp file must live in _cacheDir (not the system temp dir): rename is only
    // atomic — and only succeeds without a copy — within one filesystem, and /tmp is
    // commonly a separate mount (tmpfs) from ~/.cache, which would fail with EXDEV and
    // silently leave the persistent cache empty.
    auto const tmp = _fs.createTempFile("endo-completion-cache", _cacheDir);
    if (!tmp)
        return;
    if (auto const written = _fs.writeFile(*tmp, serialized); !written)
    {
        [[maybe_unused]] auto const removed = _fs.remove(*tmp);
        return;
    }
    if (auto const renamed = _fs.rename(*tmp, target); !renamed)
    {
        [[maybe_unused]] auto const removed = _fs.remove(*tmp);
    }
}

} // namespace endo
