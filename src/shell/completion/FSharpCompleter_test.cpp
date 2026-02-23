// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_test_macros.hpp>

#include "FSharpCompleter.hpp"

using namespace std::string_literals;

namespace
{

/// @brief Helper to create a CompletionContext with a given prefix at command position.
endo::CompletionContext makeContext(std::string prefix)
{
    return endo::CompletionContext {
        .type = endo::CompletionContextType::Command,
        .prefix = std::move(prefix),
        .prefixStart = 0,
        .cursorPosition = 0,
    };
}

/// @brief Helper to collect completion text values from results.
std::vector<std::string> completionTexts(std::vector<tui::CompletionItem> const& items)
{
    std::vector<std::string> texts;
    texts.reserve(items.size());
    for (auto const& item: items)
        texts.push_back(item.text);
    return texts;
}

/// @brief Helper to check if a specific text is in the completions.
bool hasCompletion(std::vector<tui::CompletionItem> const& items, std::string const& text)
{
    for (auto const& item: items)
        if (item.text == text)
            return true;
    return false;
}

} // namespace

// ============================================================================
// Option module completions
// ============================================================================

TEST_CASE("FSharpCompleter.Option.all_methods")
{
    endo::FSharpPersistentState state;
    endo::FSharpCompleter completer(state);

    auto results = completer.complete(makeContext("Option."));
    CHECK(results.size() == 3);
    CHECK(hasCompletion(results, "Option.map"));
    CHECK(hasCompletion(results, "Option.bind"));
    CHECK(hasCompletion(results, "Option.defaultValue"));
}

TEST_CASE("FSharpCompleter.Option.prefix_m")
{
    endo::FSharpPersistentState state;
    endo::FSharpCompleter completer(state);

    auto results = completer.complete(makeContext("Option.m"));
    CHECK(results.size() == 1);
    CHECK(results[0].text == "Option.map");
}

TEST_CASE("FSharpCompleter.Option.prefix_de")
{
    endo::FSharpPersistentState state;
    endo::FSharpCompleter completer(state);

    auto results = completer.complete(makeContext("Option.de"));
    CHECK(results.size() == 1);
    CHECK(results[0].text == "Option.defaultValue");
}

TEST_CASE("FSharpCompleter.Option.prefix_b")
{
    endo::FSharpPersistentState state;
    endo::FSharpCompleter completer(state);

    auto results = completer.complete(makeContext("Option.b"));
    CHECK(results.size() == 1);
    CHECK(results[0].text == "Option.bind");
}

TEST_CASE("FSharpCompleter.Option.no_match")
{
    endo::FSharpPersistentState state;
    endo::FSharpCompleter completer(state);

    auto results = completer.complete(makeContext("Option.x"));
    CHECK(results.empty());
}

TEST_CASE("FSharpCompleter.Option.description")
{
    endo::FSharpPersistentState state;
    endo::FSharpCompleter completer(state);

    auto results = completer.complete(makeContext("Option.m"));
    REQUIRE(results.size() == 1);
    CHECK(results[0].description == "Option.map f opt -> option");
}

// ============================================================================
// Underscore field completions
// ============================================================================

TEST_CASE("FSharpCompleter.underscore.all_fields")
{
    endo::FSharpPersistentState state;
    state.recordTypeFields["ProcessInfo"] = {
        { "pid", "int" },   { "ppid", "int" },  { "user", "str" },
        { "cpu", "float" }, { "mem", "float" }, { "command", "str" },
    };
    endo::FSharpCompleter completer(state);

    auto results = completer.complete(makeContext("_."));
    CHECK(results.size() == 6);
    CHECK(hasCompletion(results, "_.pid"));
    CHECK(hasCompletion(results, "_.ppid"));
    CHECK(hasCompletion(results, "_.user"));
    CHECK(hasCompletion(results, "_.cpu"));
    CHECK(hasCompletion(results, "_.mem"));
    CHECK(hasCompletion(results, "_.command"));
}

TEST_CASE("FSharpCompleter.underscore.prefix_pp")
{
    endo::FSharpPersistentState state;
    state.recordTypeFields["ProcessInfo"] = {
        { "pid", "int" },   { "ppid", "int" },  { "user", "str" },
        { "cpu", "float" }, { "mem", "float" }, { "command", "str" },
    };
    endo::FSharpCompleter completer(state);

    auto results = completer.complete(makeContext("_.pp"));
    CHECK(results.size() == 1);
    CHECK(hasCompletion(results, "_.ppid"));
}

TEST_CASE("FSharpCompleter.underscore.prefix_us")
{
    endo::FSharpPersistentState state;
    state.recordTypeFields["ProcessInfo"] = {
        { "pid", "int" },   { "ppid", "int" },  { "user", "str" },
        { "cpu", "float" }, { "mem", "float" }, { "command", "str" },
    };
    endo::FSharpCompleter completer(state);

    auto results = completer.complete(makeContext("_.us"));
    CHECK(results.size() == 1);
    CHECK(results[0].text == "_.user");
}

TEST_CASE("FSharpCompleter.underscore.no_match")
{
    endo::FSharpPersistentState state;
    state.recordTypeFields["ProcessInfo"] = {
        { "pid", "int" },   { "ppid", "int" },  { "user", "str" },
        { "cpu", "float" }, { "mem", "float" }, { "command", "str" },
    };
    endo::FSharpCompleter completer(state);

    auto results = completer.complete(makeContext("_.z"));
    CHECK(results.empty());
}

TEST_CASE("FSharpCompleter.underscore.multiple_types")
{
    endo::FSharpPersistentState state;
    state.recordTypeFields["ProcessInfo"] = {
        { "pid", "int" },   { "ppid", "int" },  { "user", "str" },
        { "cpu", "float" }, { "mem", "float" }, { "command", "str" },
    };
    state.recordTypeFields["Person"] = { { "name", "str" }, { "age", "int" } };
    endo::FSharpCompleter completer(state);

    auto results = completer.complete(makeContext("_."));
    CHECK(results.size() == 8);
    CHECK(hasCompletion(results, "_.name"));
    CHECK(hasCompletion(results, "_.age"));
}

TEST_CASE("FSharpCompleter.underscore.multiple_types_filter")
{
    endo::FSharpPersistentState state;
    state.recordTypeFields["ProcessInfo"] = {
        { "pid", "int" },   { "ppid", "int" },  { "user", "str" },
        { "cpu", "float" }, { "mem", "float" }, { "command", "str" },
    };
    state.recordTypeFields["Person"] = { { "name", "str" }, { "age", "int" } };
    endo::FSharpCompleter completer(state);

    auto results = completer.complete(makeContext("_.n"));
    CHECK(results.size() == 1);
    CHECK(results[0].text == "_.name");
}

TEST_CASE("FSharpCompleter.underscore.description_shows_type")
{
    endo::FSharpPersistentState state;
    state.recordTypeFields["ProcessInfo"] = { { "pid", "int" } };
    endo::FSharpCompleter completer(state);

    auto results = completer.complete(makeContext("_.p"));
    REQUIRE(results.size() == 1);
    CHECK(results[0].description == "field: int");
}

// ============================================================================
// Value method/field completions (identifier.xxx)
// ============================================================================

TEST_CASE("FSharpCompleter.value.offers_methods_and_fields")
{
    endo::FSharpPersistentState state;
    state.recordTypeFields["ProcessInfo"] = {
        { "pid", "int" },   { "ppid", "int" },  { "user", "str" },
        { "cpu", "float" }, { "mem", "float" }, { "command", "str" },
    };
    endo::FSharpCompleter completer(state);

    auto results = completer.complete(makeContext("myVar."));
    // 3 Option methods + 6 ProcessInfo fields = 9
    CHECK(results.size() == 9);
    CHECK(hasCompletion(results, "myVar.map"));
    CHECK(hasCompletion(results, "myVar.bind"));
    CHECK(hasCompletion(results, "myVar.defaultValue"));
    CHECK(hasCompletion(results, "myVar.pid"));
    CHECK(hasCompletion(results, "myVar.command"));
}

TEST_CASE("FSharpCompleter.value.filter_m")
{
    endo::FSharpPersistentState state;
    state.recordTypeFields["ProcessInfo"] = {
        { "pid", "int" },   { "ppid", "int" },  { "user", "str" },
        { "cpu", "float" }, { "mem", "float" }, { "command", "str" },
    };
    endo::FSharpCompleter completer(state);

    auto results = completer.complete(makeContext("myVar.m"));
    CHECK(results.size() == 2);
    CHECK(hasCompletion(results, "myVar.map"));
    CHECK(hasCompletion(results, "myVar.mem"));
}

TEST_CASE("FSharpCompleter.value.filter_bi")
{
    endo::FSharpPersistentState state;
    state.recordTypeFields["ProcessInfo"] = {
        { "pid", "int" },   { "ppid", "int" },  { "user", "str" },
        { "cpu", "float" }, { "mem", "float" }, { "command", "str" },
    };
    endo::FSharpCompleter completer(state);

    auto results = completer.complete(makeContext("myVar.bi"));
    CHECK(results.size() == 1);
    CHECK(results[0].text == "myVar.bind");
}

// ============================================================================
// No dot — returns empty (leave to other providers)
// ============================================================================

TEST_CASE("FSharpCompleter.no_dot.bare_option")
{
    endo::FSharpPersistentState state;
    endo::FSharpCompleter completer(state);

    auto results = completer.complete(makeContext("Option"));
    CHECK(results.empty());
}

TEST_CASE("FSharpCompleter.no_dot.bare_map")
{
    endo::FSharpPersistentState state;
    endo::FSharpCompleter completer(state);

    auto results = completer.complete(makeContext("map"));
    CHECK(results.empty());
}

TEST_CASE("FSharpCompleter.no_dot.bare_underscore")
{
    endo::FSharpPersistentState state;
    endo::FSharpCompleter completer(state);

    auto results = completer.complete(makeContext("_"));
    CHECK(results.empty());
}

// ============================================================================
// Edge cases
// ============================================================================

TEST_CASE("FSharpCompleter.edge.empty_prefix")
{
    endo::FSharpPersistentState state;
    endo::FSharpCompleter completer(state);

    auto results = completer.complete(makeContext(""));
    CHECK(results.empty());
}

TEST_CASE("FSharpCompleter.edge.just_dot")
{
    endo::FSharpPersistentState state;
    endo::FSharpCompleter completer(state);

    // "." has dot at position 0, objectPart is empty → returns empty
    auto results = completer.complete(makeContext("."));
    CHECK(results.empty());
}

TEST_CASE("FSharpCompleter.edge.nested_dot")
{
    endo::FSharpPersistentState state;
    state.recordTypeFields["ProcessInfo"] = { { "cpu", "float" } };
    endo::FSharpCompleter completer(state);

    // "a.b.c" should split on last dot: objectPart="a.b", memberPrefix="c"
    auto results = completer.complete(makeContext("a.b.c"));
    CHECK(hasCompletion(results, "a.b.cpu"));
}

// ============================================================================
// DateTime.now. completions (module function return type resolution)
// ============================================================================

TEST_CASE("FSharpCompleter.DateTime_now.returns_only_DateTime_fields")
{
    endo::FSharpPersistentState state;
    state.recordTypeFields["DateTime"] = {
        { "year", "int" },   { "month", "int" },  { "day", "int" },   { "hour", "int" },
        { "minute", "int" }, { "second", "int" }, { "epoch", "int" },
    };
    state.recordTypeFields["ProcessInfo"] = {
        { "pid", "int" }, { "cpu", "float" }, { "command", "str" },
    };
    state.recordTypeFields["GitCommit"] = {
        { "author", "str" }, { "email", "str" }, { "date", "str" },
    };
    endo::FSharpCompleter completer(state);

    auto results = completer.complete(makeContext("DateTime.now."));
    CHECK(results.size() == 7);
    CHECK(hasCompletion(results, "DateTime.now.year"));
    CHECK(hasCompletion(results, "DateTime.now.month"));
    CHECK(hasCompletion(results, "DateTime.now.day"));
    CHECK(hasCompletion(results, "DateTime.now.hour"));
    CHECK(hasCompletion(results, "DateTime.now.minute"));
    CHECK(hasCompletion(results, "DateTime.now.second"));
    CHECK(hasCompletion(results, "DateTime.now.epoch"));
    // Must NOT contain fields from other types
    CHECK_FALSE(hasCompletion(results, "DateTime.now.pid"));
    CHECK_FALSE(hasCompletion(results, "DateTime.now.author"));
    CHECK_FALSE(hasCompletion(results, "DateTime.now.cpu"));
}

TEST_CASE("FSharpCompleter.DateTime_now.filter_by_prefix")
{
    endo::FSharpPersistentState state;
    state.recordTypeFields["DateTime"] = {
        { "year", "int" },   { "month", "int" },  { "day", "int" },   { "hour", "int" },
        { "minute", "int" }, { "second", "int" }, { "epoch", "int" },
    };
    state.recordTypeFields["ProcessInfo"] = { { "mem", "float" } };
    endo::FSharpCompleter completer(state);

    auto results = completer.complete(makeContext("DateTime.now.m"));
    REQUIRE(results.size() == 2);
    CHECK(hasCompletion(results, "DateTime.now.month"));
    CHECK(hasCompletion(results, "DateTime.now.minute"));
}

TEST_CASE("FSharpCompleter.DateTime_fromEpoch.returns_only_DateTime_fields")
{
    endo::FSharpPersistentState state;
    state.recordTypeFields["DateTime"] = {
        { "year", "int" }, { "month", "int" }, { "day", "int" },
    };
    state.recordTypeFields["ProcessInfo"] = { { "pid", "int" } };
    endo::FSharpCompleter completer(state);

    auto results = completer.complete(makeContext("DateTime.fromEpoch."));
    CHECK(results.size() == 3);
    CHECK(hasCompletion(results, "DateTime.fromEpoch.year"));
    CHECK_FALSE(hasCompletion(results, "DateTime.fromEpoch.pid"));
}

TEST_CASE("FSharpCompleter.canHandle")
{
    endo::FSharpPersistentState state;
    endo::FSharpCompleter completer(state);

    CHECK(completer.canHandle(endo::CompletionContextType::Command));
    CHECK(completer.canHandle(endo::CompletionContextType::Argument));
    CHECK_FALSE(completer.canHandle(endo::CompletionContextType::FilePath));
    CHECK_FALSE(completer.canHandle(endo::CompletionContextType::Variable));
    CHECK_FALSE(completer.canHandle(endo::CompletionContextType::Option));
}

TEST_CASE("FSharpCompleter.priority")
{
    endo::FSharpPersistentState state;
    endo::FSharpCompleter completer(state);

    CHECK(completer.priority() == 95);
}
