// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_test_macros.hpp>

#include "Completer.hpp"
#include <platform/testing/TestEnvironmentProvider.hpp>

using namespace std::string_literals;

// =============================================================================
// Ghost text (suggest) tests
// =============================================================================

TEST_CASE("Completer.suggest.empty_input_returns_nullopt")
{
    endo::InMemoryHistory history;
    endo::TestEnvironment env;
    endo::FSharpPersistentState fsharpState;
    endo::Completer completer(env, history, fsharpState);

    auto result = completer.suggest("", 0);
    CHECK_FALSE(result.has_value());
}

TEST_CASE("Completer.suggest.command_with_history_match")
{
    endo::InMemoryHistory history;
    history.add("git push origin main");
    endo::TestEnvironment env;
    endo::FSharpPersistentState fsharpState;
    endo::Completer completer(env, history, fsharpState);

    auto result = completer.suggest("git p", 5);
    REQUIRE(result.has_value());
    CHECK(*result == "ush origin main");
}

TEST_CASE("Completer.suggest.variable_context_word_level")
{
    endo::InMemoryHistory history;
    endo::TestEnvironment env;
    env.set("PATH", "/usr/bin");
    env.set("PAGER", "less");
    endo::FSharpPersistentState fsharpState;
    endo::Completer completer(env, history, fsharpState);

    // $PA should suggest TH (completing PATH) or GER — we get whichever sorts first
    auto result = completer.suggest("$PA", 3);
    REQUIRE(result.has_value());
    // Should complete to either PAGER or PATH — both start with PA
    CHECK((*result == "GER" || *result == "TH"));
}

TEST_CASE("Completer.suggest.argument_context_history_match")
{
    endo::InMemoryHistory history;
    history.add("echo hello world");
    endo::TestEnvironment env;
    endo::FSharpPersistentState fsharpState;
    endo::Completer completer(env, history, fsharpState);

    // Typing "echo he" in argument position — Phase 1 should match full line from history
    auto result = completer.suggest("echo he", 7);
    REQUIRE(result.has_value());
    CHECK(*result == "llo world");
}

TEST_CASE("Completer.suggest.argument_context_no_match_returns_nullopt")
{
    endo::InMemoryHistory history;
    endo::TestEnvironment env;
    endo::FSharpPersistentState fsharpState;
    endo::Completer completer(env, history, fsharpState);

    auto result = completer.suggest("echo xyznonexistent", 19);
    CHECK_FALSE(result.has_value());
}

TEST_CASE("Completer.suggest.let_binding_word_level_fallback")
{
    endo::InMemoryHistory history;
    endo::TestEnvironment env;
    endo::FSharpPersistentState fsharpState;
    fsharpState.functions["myFunction"] = endo::FSharpPersistentState::PersistedFunction {
        .parameters = { "x" },
        .parameterTypes = { std::nullopt },
    };
    endo::Completer completer(env, history, fsharpState);

    // In command position, "myFun" should match the let binding via Phase 2
    auto result = completer.suggest("myFun", 5);
    REQUIRE(result.has_value());
    CHECK(*result == "ction");
}

TEST_CASE("Completer.suggest.history_preferred_over_word_level")
{
    endo::InMemoryHistory history;
    history.add("git commit -m \"fix bug\"");
    endo::TestEnvironment env;
    endo::FSharpPersistentState fsharpState;
    endo::Completer completer(env, history, fsharpState);

    // Phase 1 should match the full history line "git commit -m ..." before Phase 2
    auto result = completer.suggest("git co", 6);
    REQUIRE(result.has_value());
    CHECK(*result == "mmit -m \"fix bug\"");
}

TEST_CASE("Completer.suggest.history_full_line_over_variable_word")
{
    endo::InMemoryHistory history;
    history.add("$PATH/bin/something");
    endo::TestEnvironment env;
    env.set("PATH", "/usr/bin");
    endo::FSharpPersistentState fsharpState;
    endo::Completer completer(env, history, fsharpState);

    // History has a full line starting with $PATH — Phase 1 should find it
    auto result = completer.suggest("$PATH", 5);
    REQUIRE(result.has_value());
    CHECK(*result == "/bin/something");
}
