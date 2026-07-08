// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>

#include "CompleterFunctionRegistry.hpp"
#include "CompletionCache.hpp"
#include "ScriptedCompleter.hpp"

using namespace std::string_literals;

namespace
{

/// @brief Helper to create an Argument-type CompletionContext.
endo::CompletionContext makeContext(std::string fullInput,
                                    std::string prefix = "",
                                    std::string command = "flatpak")
{
    auto const cursor = fullInput.size();
    auto const prefixStart = cursor - prefix.size();
    return endo::CompletionContext {
        .type = endo::CompletionContextType::Argument,
        .prefix = std::move(prefix),
        .prefixStart = prefixStart,
        .cursorPosition = cursor,
        .command = std::move(command),
        .fullInput = std::move(fullInput),
    };
}

/// @brief Helper to check if a specific text is in the completions.
bool hasCompletion(std::vector<tui::CompletionItem> const& items, std::string_view text)
{
    return std::ranges::any_of(items, [text](auto const& item) { return item.text == text; });
}

/// @brief Helper to create a CollectedCompletion from a plain string.
endo::CollectedCompletion cc(std::string text)
{
    return { .text = std::move(text) };
}

/// @brief Mock execution callback returning predetermined completions.
/// Mimics real .endo scripts that use prefix to distinguish options vs subcommands.
auto createMockCallback() -> endo::CompleterExecutionCallback
{
    return [](std::string_view funcName,
              std::vector<std::string> const& args,
              std::string_view prefix) -> endo::CompleterExecutionResult {
        if (funcName == "flatpak_complete")
        {
            if (args.empty() && !prefix.empty() && prefix[0] == '-')
                return { .completions = { cc("--user"), cc("--system"), cc("--verbose"), cc("-v") },
                         .errors = {} };
            if (args.empty())
                return { .completions = { cc("run"),
                                          cc("install"),
                                          cc("uninstall"),
                                          cc("update"),
                                          cc("list"),
                                          cc("info"),
                                          cc("search") },
                         .errors = {} };
            if (args.size() == 1 && args[0] == "run")
                return { .completions = { cc("com.visualstudio.code"),
                                          cc("org.mozilla.firefox"),
                                          cc("org.gnome.Calculator"),
                                          cc("io.github.sxyazi.yazi") },
                         .errors = {} };
        }
        return {};
    };
}

} // namespace

TEST_CASE("ScriptedCompleter.subcommand_completion")
{
    endo::CompleterFunctionRegistry registry;
    registry.registerFunction("flatpak", "flatpak_complete");
    endo::ScriptedCompleter completer(registry, createMockCallback());

    auto ctx = makeContext("flatpak ");
    auto results = completer.complete(ctx);

    CHECK_FALSE(results.empty());
    CHECK(hasCompletion(results, "run"));
    CHECK(hasCompletion(results, "install"));
    CHECK(hasCompletion(results, "update"));
}

TEST_CASE("ScriptedCompleter.argument_completion")
{
    endo::CompleterFunctionRegistry registry;
    registry.registerFunction("flatpak", "flatpak_complete");
    endo::ScriptedCompleter completer(registry, createMockCallback());

    auto ctx = makeContext("flatpak run ");
    auto results = completer.complete(ctx);

    CHECK_FALSE(results.empty());
    CHECK(hasCompletion(results, "com.visualstudio.code"));
    CHECK(hasCompletion(results, "org.mozilla.firefox"));
    CHECK(hasCompletion(results, "org.gnome.Calculator"));
}

TEST_CASE("ScriptedCompleter.fuzzy_filtering")
{
    endo::CompleterFunctionRegistry registry;
    registry.registerFunction("flatpak", "flatpak_complete");
    endo::ScriptedCompleter completer(registry, createMockCallback());

    auto ctx = makeContext("flatpak run com.vis", "com.vis");
    auto results = completer.complete(ctx);

    CHECK_FALSE(results.empty());
    CHECK(hasCompletion(results, "com.visualstudio.code"));
    // Non-matching entries should be filtered out or scored very low
}

TEST_CASE("ScriptedCompleter.substring_match_in_long_app_id")
{
    endo::CompleterFunctionRegistry registry;
    registry.registerFunction("flatpak", "flatpak_complete");
    endo::ScriptedCompleter completer(registry, createMockCallback());

    // "yazi" is a contiguous substring of "io.github.sxyazi.yazi" but quality (4/21)
    // is below the 0.2 threshold. The substring bypass should still accept this match.
    auto ctx = makeContext("flatpak run yazi", "yazi");
    auto results = completer.complete(ctx);

    CHECK_FALSE(results.empty());
    CHECK(hasCompletion(results, "io.github.sxyazi.yazi"));
}

TEST_CASE("ScriptedCompleter.unknown_command")
{
    endo::CompleterFunctionRegistry registry;
    registry.registerFunction("flatpak", "flatpak_complete");
    endo::ScriptedCompleter completer(registry, createMockCallback());

    auto ctx = makeContext("unknown_cmd ", "", "unknown_cmd");
    auto results = completer.complete(ctx);

    CHECK(results.empty());
}

TEST_CASE("ScriptedCompleter.empty_prefix")
{
    endo::CompleterFunctionRegistry registry;
    registry.registerFunction("flatpak", "flatpak_complete");
    endo::ScriptedCompleter completer(registry, createMockCallback());

    auto ctx = makeContext("flatpak ", "");
    auto results = completer.complete(ctx);

    CHECK_FALSE(results.empty());
    // All subcommands should be present
    CHECK(results.size() == 7);
}

TEST_CASE("ScriptedCompleter.isExclusiveFor")
{
    endo::CompleterFunctionRegistry registry;
    registry.registerFunction("flatpak", "flatpak_complete");
    endo::ScriptedCompleter completer(registry, createMockCallback());

    auto ctx1 = makeContext("flatpak ");
    CHECK(completer.isExclusiveFor(ctx1));

    auto ctx2 = makeContext("unknown ", "", "unknown");
    CHECK_FALSE(completer.isExclusiveFor(ctx2));
}

TEST_CASE("ScriptedCompleter.canHandle")
{
    endo::CompleterFunctionRegistry registry;
    registry.registerFunction("flatpak", "flatpak_complete");
    endo::ScriptedCompleter completer(registry, createMockCallback());

    // Should handle Argument and Option context types
    CHECK(completer.canHandle(endo::CompletionContextType::Argument));
    CHECK(completer.canHandle(endo::CompletionContextType::Option));

    // Should not handle Command context type
    CHECK_FALSE(completer.canHandle(endo::CompletionContextType::Command));
}

TEST_CASE("ScriptedCompleter.cache_reuse")
{
    int callCount = 0;
    auto countingCallback = [&callCount](std::string_view /*funcName*/,
                                         std::vector<std::string> const& /*args*/,
                                         std::string_view /*prefix*/) -> endo::CompleterExecutionResult {
        ++callCount;
        return { .completions = { cc("run"), cc("install"), cc("update") }, .errors = {} };
    };

    endo::CompleterFunctionRegistry registry;
    registry.registerFunction("flatpak", "flatpak_complete");
    endo::ScriptedCompleter completer(registry, countingCallback);

    // First call
    auto ctx1 = makeContext("flatpak ", "");
    auto r1 = completer.complete(ctx1);
    CHECK(callCount == 1);

    // Second call with same args but different prefix — should reuse cache
    auto ctx2 = makeContext("flatpak ru", "ru");
    auto r2 = completer.complete(ctx2);
    CHECK(callCount == 1); // Cache hit: same funcName + args

    // Call with different args — should invoke callback again
    auto ctx3 = makeContext("flatpak run ");
    auto r3 = completer.complete(ctx3);
    CHECK(callCount == 2); // Cache miss: different args
}

TEST_CASE("ScriptedCompleter.option_context_completion")
{
    endo::CompleterFunctionRegistry registry;
    registry.registerFunction("flatpak", "flatpak_complete");
    endo::ScriptedCompleter completer(registry, createMockCallback());

    // Option context: "flatpak --" with prefix "--"
    auto ctx = endo::CompletionContext {
        .type = endo::CompletionContextType::Option,
        .prefix = "--",
        .prefixStart = 8,
        .cursorPosition = 10,
        .command = "flatpak",
        .fullInput = "flatpak --",
    };
    auto results = completer.complete(ctx);

    CHECK_FALSE(results.empty());
    CHECK(hasCompletion(results, "--user"));
    CHECK(hasCompletion(results, "--system"));
    CHECK(hasCompletion(results, "--verbose"));
}

TEST_CASE("ScriptedCompleter.cache_invalidation_on_option_prefix_change")
{
    int callCount = 0;
    auto callback = [&callCount](std::string_view /*funcName*/,
                                 std::vector<std::string> const& args,
                                 std::string_view prefix) -> endo::CompleterExecutionResult {
        ++callCount;
        if (!prefix.empty() && prefix[0] == '-')
            return { .completions = { cc("--user"), cc("--system") }, .errors = {} };
        return { .completions = { cc("run"), cc("install") }, .errors = {} };
    };

    endo::CompleterFunctionRegistry registry;
    registry.registerFunction("flatpak", "flatpak_complete");
    endo::ScriptedCompleter completer(registry, callback);

    // First call: argument completion (no option prefix)
    auto ctx1 = makeContext("flatpak ", "");
    auto r1 = completer.complete(ctx1);
    CHECK(callCount == 1);
    CHECK(hasCompletion(r1, "run"));

    // Second call: option completion (option prefix) — must NOT reuse cache
    auto ctx2 = endo::CompletionContext {
        .type = endo::CompletionContextType::Option,
        .prefix = "--",
        .prefixStart = 8,
        .cursorPosition = 10,
        .command = "flatpak",
        .fullInput = "flatpak --",
    };
    auto r2 = completer.complete(ctx2);
    CHECK(callCount == 2); // Cache miss: different prefix category
    CHECK(hasCompletion(r2, "--user"));

    // Third call: same option prefix, more typed — should reuse cache
    auto ctx3 = endo::CompletionContext {
        .type = endo::CompletionContextType::Option,
        .prefix = "--us",
        .prefixStart = 8,
        .cursorPosition = 12,
        .command = "flatpak",
        .fullInput = "flatpak --us",
    };
    auto r3 = completer.complete(ctx3);
    CHECK(callCount == 2); // Cache hit: same prefix category
    CHECK(hasCompletion(r3, "--user"));
}

TEST_CASE("ScriptedCompleter.args_extraction")
{
    // Verify args are correctly extracted from fullInput
    int callCount = 0;
    std::vector<std::string> capturedArgs;
    auto captureCallback = [&](std::string_view /*funcName*/,
                               std::vector<std::string> const& args,
                               std::string_view /*prefix*/) -> endo::CompleterExecutionResult {
        ++callCount;
        capturedArgs = args;
        return { .completions = { cc("result") }, .errors = {} };
    };

    endo::CompleterFunctionRegistry registry;
    registry.registerFunction("flatpak", "flatpak_complete");
    endo::ScriptedCompleter completer(registry, captureCallback);

    // "flatpak run --user " → args should be ["run", "--user"]
    auto ctx = makeContext("flatpak run --user ");
    auto results = completer.complete(ctx);
    REQUIRE(capturedArgs.size() == 2);
    CHECK(capturedArgs[0] == "run");
    CHECK(capturedArgs[1] == "--user");
}

TEST_CASE("ScriptedCompleter.autosuggest_never_invokes_callback_on_cold_cache")
{
    int callCount = 0;
    auto callback = [&callCount](std::string_view /*funcName*/,
                                 std::vector<std::string> const& /*args*/,
                                 std::string_view /*prefix*/) -> endo::CompleterExecutionResult {
        ++callCount;
        return { .completions = { cc("firefox"), cc("firewalld") }, .errors = {} };
    };

    endo::CompleterFunctionRegistry registry;
    registry.registerFunction("dnf", "dnf_complete");
    endo::ScriptedCompleter completer(registry, callback);

    // Ghost text on a cold cache must NOT shell out — this is the fix for the freeze.
    auto ctx = makeContext("dnf install fire", "fire", "dnf");
    ctx.intent = endo::CompletionIntent::Autosuggest;
    auto results = completer.complete(ctx);

    CHECK(callCount == 0);
    CHECK(results.empty());
}

TEST_CASE("ScriptedCompleter.autosuggest_serves_from_warm_cache_without_fetching")
{
    int callCount = 0;
    auto callback = [&callCount](std::string_view /*funcName*/,
                                 std::vector<std::string> const& /*args*/,
                                 std::string_view /*prefix*/) -> endo::CompleterExecutionResult {
        ++callCount;
        return { .completions = { cc("firefox"), cc("firewalld") }, .errors = {} };
    };

    endo::CompleterFunctionRegistry registry;
    registry.registerFunction("dnf", "dnf_complete");
    endo::ScriptedCompleter completer(registry, callback);

    // Explicit Tab warms the cache (one fetch).
    auto tab = makeContext("dnf install ", "", "dnf");
    (void) completer.complete(tab);
    CHECK(callCount == 1);

    // A subsequent ghost-text request serves from cache without another fetch.
    auto ghost = makeContext("dnf install fire", "fire", "dnf");
    ghost.intent = endo::CompletionIntent::Autosuggest;
    auto results = completer.complete(ghost);
    CHECK(callCount == 1); // no additional fetch
    CHECK(hasCompletion(results, "firefox"));
}

TEST_CASE("ScriptedCompleter.aborted_result_is_not_cached")
{
    int callCount = 0;
    auto callback = [&callCount](std::string_view /*funcName*/,
                                 std::vector<std::string> const& /*args*/,
                                 std::string_view /*prefix*/) -> endo::CompleterExecutionResult {
        ++callCount;
        // Simulate an aborted `$(dnf repoquery)`: empty completions, Aborted status.
        return { .completions = {}, .errors = {}, .status = endo::CompleterExecutionStatus::Aborted };
    };

    endo::CompleterFunctionRegistry registry;
    registry.registerFunction("dnf", "dnf_complete");
    endo::ScriptedCompleter completer(registry, callback);

    auto ctx = makeContext("dnf install ", "", "dnf");
    (void) completer.complete(ctx);
    CHECK(callCount == 1);

    // Because the run was aborted, its empty result must not be cached — a second
    // Tab retries instead of serving a poisoned empty entry.
    (void) completer.complete(ctx);
    CHECK(callCount == 2);
}

TEST_CASE("ScriptedCompleter.aborted_fetch_falls_back_to_stale_cache")
{
    int callCount = 0;
    bool abortNext = false;
    auto callback = [&](std::string_view /*funcName*/,
                        std::vector<std::string> const& /*args*/,
                        std::string_view /*prefix*/) -> endo::CompleterExecutionResult {
        ++callCount;
        if (abortNext)
            return { .completions = {}, .errors = {}, .status = endo::CompleterExecutionStatus::Aborted };
        return { .completions = { cc("firefox") }, .errors = {} };
    };

    // A clock we can advance to force staleness.
    auto fakeNow = std::chrono::system_clock::time_point(std::chrono::seconds(1'000'000));
    endo::ScriptedCompleterConfig config; // freshTtl 250s, hardTtl 6h
    endo::CompleterFunctionRegistry registry;
    registry.registerFunction("dnf", "dnf_complete");
    endo::ScriptedCompleter completer(registry, callback, nullptr, config, [&fakeNow] { return fakeNow; });

    auto ctx = makeContext("dnf install ", "", "dnf");
    auto r1 = completer.complete(ctx);
    CHECK(callCount == 1);
    CHECK(hasCompletion(r1, "firefox"));

    // Advance past freshTtl so the next Tab refetches, but make that fetch abort.
    fakeNow += std::chrono::seconds(300);
    abortNext = true;
    auto r2 = completer.complete(ctx);
    CHECK(callCount == 2);               // it did attempt a refresh
    CHECK(hasCompletion(r2, "firefox")); // and fell back to the stale entry, not empty
}

TEST_CASE("ScriptedCompleter.persistent_cache_promotes_L2_hit_without_fetch")
{
    int callCount = 0;
    auto callback = [&callCount](std::string_view /*funcName*/,
                                 std::vector<std::string> const& /*args*/,
                                 std::string_view /*prefix*/) -> endo::CompleterExecutionResult {
        ++callCount;
        return { .completions = { cc("vim") }, .errors = {} };
    };

    endo::InMemoryCompletionCache l2;
    endo::CompleterFunctionRegistry registry;
    registry.registerFunction("dnf", "dnf_complete");

    auto fakeNow = std::chrono::system_clock::time_point(std::chrono::seconds(1'000'000));
    auto const clock = [&fakeNow] {
        return fakeNow;
    };

    // First completer instance fetches once and populates L2.
    {
        endo::ScriptedCompleter completer(registry, callback, &l2, {}, clock);
        auto ctx = makeContext("dnf install ", "", "dnf");
        (void) completer.complete(ctx);
        CHECK(callCount == 1);
    }

    // A fresh instance (new session, empty L1) served from L2 must not fetch again.
    {
        endo::ScriptedCompleter completer(registry, callback, &l2, {}, clock);
        auto ctx = makeContext("dnf install ", "", "dnf");
        auto results = completer.complete(ctx);
        CHECK(callCount == 1); // L2 hit, no fetch
        CHECK(hasCompletion(results, "vim"));
    }
}
