// SPDX-License-Identifier: Apache-2.0

#include <shell/completion/CompletionCache.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string>

#include <platform/testing/InMemoryFileSystem.hpp>

using namespace std::string_literals;

using endo::CachedCompletions;
using endo::CollectedCompletion;
using endo::FileSystemCompletionCache;
using endo::InMemoryCompletionCache;
using endo::platform::testing::InMemoryFileSystem;

namespace
{
/// A fixed, non-zero wall-clock instant so serialized timestamps are deterministic.
std::chrono::system_clock::time_point fixedTime()
{
    return std::chrono::system_clock::time_point(std::chrono::milliseconds(1'700'000'000'123));
}

CollectedCompletion cc(std::string text, std::string desc = {}, std::string detail = {})
{
    return { .text = std::move(text), .description = std::move(desc), .detail = std::move(detail) };
}
} // namespace

TEST_CASE("InMemoryCompletionCache.round_trip")
{
    InMemoryCompletionCache cache;
    CHECK_FALSE(cache.load("missing").has_value());

    CachedCompletions entry { .results = { cc("bash"), cc("bat") }, .timestamp = fixedTime() };
    cache.store("dnf\0install"s, entry);

    auto const loaded = cache.load("dnf\0install"s);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->results.size() == 2);
    CHECK(loaded->results[0].text == "bash");
    CHECK(loaded->timestamp == fixedTime());
}

TEST_CASE("FileSystemCompletionCache.round_trip_through_filesystem")
{
    InMemoryFileSystem fs;
    FileSystemCompletionCache cache(fs, "/home/user/.cache/endo/completions");

    CHECK_FALSE(cache.load("dnf\0install"s).has_value()); // cold

    CachedCompletions entry {
        .results = { cc("bash", "GNU Bourne-Again SHell"), cc("bat", "cat clone") },
        .timestamp = fixedTime(),
    };
    cache.store("dnf\0install"s, entry);

    auto const loaded = cache.load("dnf\0install"s);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->results.size() == 2);
    CHECK(loaded->results[0].text == "bash");
    CHECK(loaded->results[0].description == "GNU Bourne-Again SHell");
    CHECK(loaded->results[1].text == "bat");
    CHECK(loaded->timestamp == fixedTime());
}

TEST_CASE("FileSystemCompletionCache.empty_result_set_round_trips")
{
    InMemoryFileSystem fs;
    FileSystemCompletionCache cache(fs, "/cache");

    CachedCompletions entry { .results = {}, .timestamp = fixedTime() };
    cache.store("cmd", entry);

    auto const loaded = cache.load("cmd");
    REQUIRE(loaded.has_value());
    CHECK(loaded->results.empty());
    CHECK(loaded->timestamp == fixedTime());
}

TEST_CASE("FileSystemCompletionCache.preserves_special_characters")
{
    InMemoryFileSystem fs;
    FileSystemCompletionCache cache(fs, "/cache");

    // Text/description containing the field separator, backslash, and newline must
    // survive the escaped on-disk format.
    CachedCompletions entry {
        .results = { cc("a\tb", "line1\nline2", "back\\slash") },
        .timestamp = fixedTime(),
    };
    cache.store("k", entry);

    auto const loaded = cache.load("k");
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->results.size() == 1);
    CHECK(loaded->results[0].text == "a\tb");
    CHECK(loaded->results[0].description == "line1\nline2");
    CHECK(loaded->results[0].detail == "back\\slash");
}

TEST_CASE("FileSystemCompletionCache.hash_collision_verified_by_stored_key")
{
    // Two different keys that (by construction of the test) land in the same file
    // would collide, but deserialize() rejects a mismatched stored key. We simulate
    // a collision by writing key A's content into key B's file path.
    InMemoryFileSystem fs;
    FileSystemCompletionCache cache(fs, "/cache");

    CachedCompletions entry { .results = { cc("only-for-A") }, .timestamp = fixedTime() };
    auto const serializedA = FileSystemCompletionCache::serialize("keyA", entry);

    // Place A's serialized content at B's filename.
    auto const fileB = std::filesystem::path("/cache") / FileSystemCompletionCache::fileNameForKey("keyB");
    REQUIRE(fs.createDirectories("/cache").has_value());
    REQUIRE(fs.writeFile(fileB, serializedA).has_value());

    // Loading keyB must miss: the file's stored key ("keyA") does not match.
    CHECK_FALSE(cache.load("keyB").has_value());
}

TEST_CASE("FileSystemCompletionCache.corrupt_file_is_a_miss")
{
    InMemoryFileSystem fs;
    FileSystemCompletionCache cache(fs, "/cache");

    auto const file = std::filesystem::path("/cache") / FileSystemCompletionCache::fileNameForKey("cmd");
    REQUIRE(fs.createDirectories("/cache").has_value());
    REQUIRE(fs.writeFile(file, "garbage not our format").has_value());

    CHECK_FALSE(cache.load("cmd").has_value());
}

TEST_CASE("FileSystemCompletionCache.deserialize_rejects_bad_magic_and_timestamp")
{
    // Wrong magic.
    CHECK_FALSE(FileSystemCompletionCache::deserialize("wrong-magic\n123\ncmd\n", "cmd").has_value());
    // Non-numeric timestamp.
    CHECK_FALSE(
        FileSystemCompletionCache::deserialize("endo-completion-cache/1\nxyz\ncmd\n", "cmd").has_value());
    // Too few header lines.
    CHECK_FALSE(FileSystemCompletionCache::deserialize("endo-completion-cache/1\n", "cmd").has_value());
}

TEST_CASE("FileSystemCompletionCache.fileNameForKey_is_stable_and_distinct")
{
    auto const a1 = FileSystemCompletionCache::fileNameForKey("dnf\0install"s);
    auto const a2 = FileSystemCompletionCache::fileNameForKey("dnf\0install"s);
    auto const b = FileSystemCompletionCache::fileNameForKey("rpm\0-q"s);
    CHECK(a1 == a2);        // deterministic
    CHECK(a1 != b);         // different keys → different files (overwhelmingly likely)
    CHECK(a1.size() == 16); // 64-bit FNV-1a as hex
}

TEST_CASE("FileSystemCompletionCache.store_overwrites_prior_entry")
{
    InMemoryFileSystem fs;
    FileSystemCompletionCache cache(fs, "/cache");

    cache.store("k", CachedCompletions { .results = { cc("old") }, .timestamp = fixedTime() });
    cache.store("k", CachedCompletions { .results = { cc("new1"), cc("new2") }, .timestamp = fixedTime() });

    auto const loaded = cache.load("k");
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->results.size() == 2);
    CHECK(loaded->results[0].text == "new1");
}
