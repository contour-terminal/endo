// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>

#include "BuiltinSpecs.hpp"
#include "CommandSpecCompleter.hpp"
#include "PathCommandIndex.hpp"
#include "PathCommandQueryProvider.hpp"
#include <platform/testing/InMemoryFileSystem.hpp>
#include <platform/testing/TestEnvironmentProvider.hpp>

namespace
{

/// @brief Builds a filesystem with two $PATH directories worth of executables.
///
/// /usr/bin holds git, git-lfs and a non-executable README; /usr/local/bin shadows git
/// (so $PATH precedence can be observed) and adds gio.
endo::InMemoryFileSystem makePathFilesystem()
{
    return endo::InMemoryFileSystem {
        { .path = "/usr/bin", .isDirectory = true },
        { .path = "/usr/bin/git", .isExecutable = true },
        { .path = "/usr/bin/git-lfs", .isExecutable = true },
        { .path = "/usr/bin/README", .content = "not a program" },
        { .path = "/usr/local/bin", .isDirectory = true },
        { .path = "/usr/local/bin/git", .isExecutable = true },
        { .path = "/usr/local/bin/gio", .isExecutable = true },
    };
}

/// @brief Creates an Argument-type context for a `which` command line.
endo::CompletionContext makeWhichContext(std::string fullInput, std::string const& prefix = "")
{
    auto const cursor = fullInput.size();
    return endo::CompletionContext {
        .type = endo::CompletionContextType::Argument,
        .prefix = prefix,
        .prefixStart = cursor - prefix.size(),
        .cursorPosition = cursor,
        .command = "which",
        .fullInput = std::move(fullInput),
    };
}

/// @brief Creates an Option-type context for a `which` command line.
endo::CompletionContext makeWhichOptionContext(std::string fullInput, std::string const& prefix)
{
    auto const cursor = fullInput.size();
    return endo::CompletionContext {
        .type = endo::CompletionContextType::Option,
        .prefix = prefix,
        .prefixStart = cursor - prefix.size(),
        .cursorPosition = cursor,
        .command = "which",
        .fullInput = std::move(fullInput),
    };
}

bool hasCompletion(std::vector<tui::CompletionItem> const& items, std::string const& text)
{
    return std::ranges::any_of(items, [&](auto const& item) { return item.text == text; });
}

/// @brief Filesystem, environment and index with a shared lifetime.
///
/// PathCommandIndex holds references to the other two, so they cannot be separate locals
/// without repeating the ordering constraint in every test.
struct PathFixture
{
    endo::InMemoryFileSystem fs = makePathFilesystem();
    endo::TestEnvironment env;
    endo::PathCommandIndex index { env, fs };

    /// @param path Value for $PATH; the index reads it lazily, on the first entries() call.
    explicit PathFixture(std::string_view path) { env.set("PATH", path); }

    /// @brief Returns the indexed command names, in order.
    [[nodiscard]] std::vector<std::string> names() const
    {
        auto result = std::vector<std::string> {};
        for (auto const& [name, resolvedPath]: index.entries())
            result.push_back(name);
        return result;
    }

    /// @brief Returns the resolved path for @p name, or an empty string if absent.
    [[nodiscard]] std::string pathOf(std::string_view name) const
    {
        auto const& entries = index.entries();
        auto const it = std::ranges::find_if(entries, [&](auto const& e) { return e.first == name; });
        return it == entries.end() ? std::string {} : it->second;
    }

    /// @brief Builds the `which` completer wired to this fixture's index.
    [[nodiscard]] endo::CommandSpecCompleter whichCompleter() const
    {
        auto completer = endo::CommandSpecCompleter {};
        completer.registerCommand(endo::createWhichSpec(),
                                  std::make_unique<endo::PathCommandQueryProvider>(index, env));
        return completer;
    }
};

} // namespace

// =============================================================================
// PathCommandIndex
// =============================================================================

TEST_CASE("PathCommandIndex.enumerates_only_executables")
{
    PathFixture const fx { "/usr/bin:/usr/local/bin" };

    // README has no execute bit, so it must not appear; git is deduplicated across the two
    // directories. Sorted by name: "gio" < "git" < "git-lfs".
    CHECK(fx.names() == std::vector<std::string> { "gio", "git", "git-lfs" });
}

TEST_CASE("PathCommandIndex.earlier_path_entry_wins")
{
    // The reported path must be the one that would actually run.
    CHECK(PathFixture { "/usr/bin:/usr/local/bin" }.pathOf("git") == "/usr/bin/git");
    CHECK(PathFixture { "/usr/local/bin:/usr/bin" }.pathOf("git") == "/usr/local/bin/git");
}

TEST_CASE("PathCommandIndex.rescans_when_path_changes")
{
    PathFixture fx { "/usr/bin" };
    CHECK(fx.names() == std::vector<std::string> { "git", "git-lfs" });

    fx.env.set("PATH", "/usr/local/bin");
    CHECK(fx.names() == std::vector<std::string> { "gio", "git" });
}

TEST_CASE("PathCommandIndex.tolerates_empty_and_missing_path_entries")
{
    SECTION("unset PATH")
    {
        PathFixture fx { "" };
        fx.env.unset("PATH");
        CHECK(fx.names().empty());
    }

    SECTION("empty PATH")
    {
        CHECK(PathFixture { "" }.names().empty());
    }

    SECTION("nonexistent and empty segments are skipped")
    {
        CHECK(PathFixture { "::/nope:/usr/bin:" }.names() == std::vector<std::string> { "git", "git-lfs" });
    }
}

TEST_CASE("PathCommandIndex.invalidateCache_forces_rescan")
{
    PathFixture fx { "/usr/bin" };
    CHECK(fx.names().size() == 2);

    fx.index.invalidateCache();
    CHECK(fx.names() == std::vector<std::string> { "git", "git-lfs" });
}

// =============================================================================
// PathCommandQueryProvider
// =============================================================================

TEST_CASE("PathCommandQueryProvider.unknown_tag_returns_empty")
{
    PathFixture const fx { "/usr/bin" };
    endo::PathCommandQueryProvider provider(fx.index, fx.env);

    CHECK(provider.query("process-names").empty());
    CHECK(provider.query("branches").empty());
}

TEST_CASE("PathCommandQueryProvider.reports_names_with_resolved_paths")
{
    PathFixture const fx { "/usr/bin" };
    endo::PathCommandQueryProvider provider(fx.index, fx.env);

    auto const results = provider.query("path-commands");

    REQUIRE(results.size() == 2);
    CHECK(results[0].text == "git");
    CHECK(results[0].description == "/usr/bin/git");
    CHECK(results[1].text == "git-lfs");
}

TEST_CASE("PathCommandQueryProvider.collapses_home_prefix_in_description")
{
    auto const fs = endo::InMemoryFileSystem {
        { .path = "/home/testuser/bin", .isDirectory = true },
        { .path = "/home/testuser/bin/mytool", .isExecutable = true },
    };
    endo::TestEnvironment env;
    env.set("HOME", "/home/testuser");
    env.set("PATH", "/home/testuser/bin");
    endo::PathCommandIndex index(env, fs);

    endo::PathCommandQueryProvider provider(index, env);
    auto const results = provider.query("path-commands");

    REQUIRE(results.size() == 1);
    CHECK(results[0].text == "mytool");
    CHECK(results[0].description == "~/bin/mytool");
}

// =============================================================================
// `which` argument completion
// =============================================================================

TEST_CASE("WhichCompletion.completes_program_names_not_files")
{
    PathFixture const fx { "/usr/bin:/usr/local/bin" };
    auto completer = fx.whichCompleter();

    auto const results = completer.complete(makeWhichContext("which gi", "gi"));

    CHECK(hasCompletion(results, "git"));
    CHECK(hasCompletion(results, "git-lfs"));
    CHECK(hasCompletion(results, "gio"));
}

TEST_CASE("WhichCompletion.positional_is_repeatable")
{
    PathFixture const fx { "/usr/bin:/usr/local/bin" };
    auto completer = fx.whichCompleter();

    // A second program name must still complete against $PATH, not fall through to files.
    auto const results = completer.complete(makeWhichContext("which git gi", "gi"));

    CHECK(hasCompletion(results, "git-lfs"));
    CHECK(hasCompletion(results, "gio"));
}

TEST_CASE("WhichCompletion.completes_flags")
{
    PathFixture const fx { "/usr/bin" };
    auto completer = fx.whichCompleter();

    auto const results = completer.complete(makeWhichOptionContext("which -", "-"));

    CHECK(hasCompletion(results, "--all"));
    CHECK(hasCompletion(results, "--read-alias"));
    CHECK(hasCompletion(results, "--help"));
}

TEST_CASE("WhichCompletion.exclusive_for_bare_prefix_but_not_for_paths")
{
    PathFixture const fx { "/usr/bin" };
    auto completer = fx.whichCompleter();

    // A bare name is a program lookup: suppress FileCompleter so no local filenames leak in.
    CHECK(completer.isExclusiveFor(makeWhichContext("which gi", "gi")));

    // `which` also accepts a path argument, so path-shaped prefixes must keep file completion.
    CHECK_FALSE(completer.isExclusiveFor(makeWhichContext("which ./gi", "./gi")));
    CHECK_FALSE(completer.isExclusiveFor(makeWhichContext("which /usr/b", "/usr/b")));
    CHECK_FALSE(completer.isExclusiveFor(makeWhichContext("which ~/b", "~/b")));
}
