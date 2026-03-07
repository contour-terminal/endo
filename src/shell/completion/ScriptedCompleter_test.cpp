// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

#include "CompleterFunctionRegistry.hpp"
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

/// @brief Mock execution callback returning predetermined completions.
auto createMockCallback() -> endo::CompleterExecutionCallback
{
    return [](std::string_view funcName,
              std::vector<std::string> const& args,
              std::string_view /*prefix*/) -> endo::CompleterExecutionResult {
        if (funcName == "flatpak_complete")
        {
            if (args.empty())
                return { .completions = { "run", "install", "uninstall", "update", "list", "info", "search" },
                         .errors = {} };
            if (args.size() == 1 && args[0] == "run")
                return { .completions = { "com.visualstudio.code",
                                          "org.mozilla.firefox",
                                          "org.gnome.Calculator",
                                          "io.github.sxyazi.yazi" },
                         .errors = {} };
            if (args.size() == 1 && args[0] == "--")
                return { .completions = { "--user", "--system", "--verbose", "-v" }, .errors = {} };
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
        return { .completions = { "run", "install", "update" }, .errors = {} };
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
        return { .completions = { "result" }, .errors = {} };
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
