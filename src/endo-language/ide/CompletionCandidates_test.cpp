// SPDX-License-Identifier: Apache-2.0
#include <endo-language/ide/CompletionCandidates.hpp>
#include <endo-language/ide/TypeRegistryCompletionAdapter.hpp>

#include <CoreVM/types/TypeRegistry.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <unordered_map>

using namespace endo;

namespace
{

/// @brief Helper to check if a specific text is in the candidates.
bool hasCandidate(std::vector<CompletionCandidate> const& items, std::string const& text)
{
    return std::ranges::any_of(items, [&](auto const& c) { return c.text == text; });
}

/// @brief Helper to find a candidate by text.
CompletionCandidate const* findCandidate(std::vector<CompletionCandidate> const& items,
                                         std::string const& text)
{
    auto it = std::ranges::find_if(items, [&](auto const& c) { return c.text == text; });
    return it != items.end() ? &*it : nullptr;
}

/// @brief Returns the builtin module functions map for tests.
ModuleFunctionMap const& testModuleFunctions()
{
    static auto const map = [] {
        CoreVM::TypeRegistry registry;
        return builtinModuleFunctions(registry);
    }();
    return map;
}

} // namespace

// =============================================================================
// keywordCandidates tests
// =============================================================================

TEST_CASE("CompletionCandidates.keywordCandidates.returns_expected_keywords", "[completion]")
{
    auto keywords = keywordCandidates();
    CHECK(!keywords.empty());
    CHECK(hasCandidate(keywords, "let"));
    CHECK(hasCandidate(keywords, "match"));
    CHECK(hasCandidate(keywords, "fun"));
    CHECK(hasCandidate(keywords, "if"));
    CHECK(hasCandidate(keywords, "then"));
    CHECK(hasCandidate(keywords, "else"));
    CHECK(hasCandidate(keywords, "true"));
    CHECK(hasCandidate(keywords, "false"));
    CHECK(hasCandidate(keywords, "try"));
    CHECK(hasCandidate(keywords, "finally"));
}

TEST_CASE("CompletionCandidates.keywordCandidates.kind_is_keyword", "[completion]")
{
    auto keywords = keywordCandidates();
    for (auto const& kw: keywords)
        CHECK(kw.kind == CompletionKind::Keyword);
}

// =============================================================================
// builtinCandidates tests
// =============================================================================

TEST_CASE("CompletionCandidates.builtinCandidates.returns_expected_builtins", "[completion]")
{
    auto builtins = builtinCandidates();
    CHECK(!builtins.empty());
    CHECK(hasCandidate(builtins, "cd"));
    CHECK(hasCandidate(builtins, "exit"));
    CHECK(hasCandidate(builtins, "print"));
    CHECK(hasCandidate(builtins, "println"));
    CHECK(hasCandidate(builtins, "echo"));
    CHECK(hasCandidate(builtins, "shell_prompt_preset"));
    CHECK(hasCandidate(builtins, "shell_prompt_indicator"));
    CHECK(hasCandidate(builtins, "shell_prompt_layout"));
    CHECK(hasCandidate(builtins, "shell_prompt_separator"));
    CHECK(hasCandidate(builtins, "shell_prompt_transient"));
    CHECK(hasCandidate(builtins, "shell_prompt_duration_threshold"));
    CHECK(hasCandidate(builtins, "shell_is_interactive"));
}

TEST_CASE("CompletionCandidates.builtinCandidates.kind_is_builtin_or_property", "[completion]")
{
    auto builtins = builtinCandidates();
    for (auto const& b: builtins)
        CHECK((b.kind == CompletionKind::Builtin || b.kind == CompletionKind::Property));
}

// =============================================================================
// constructorCandidates tests
// =============================================================================

TEST_CASE("CompletionCandidates.constructorCandidates.returns_some_none_ok_error", "[completion]")
{
    auto ctors = constructorCandidates();
    REQUIRE(ctors.size() == 4);
    CHECK(hasCandidate(ctors, "Some"));
    CHECK(hasCandidate(ctors, "None"));
    CHECK(hasCandidate(ctors, "Ok"));
    CHECK(hasCandidate(ctors, "Error"));
}

TEST_CASE("CompletionCandidates.constructorCandidates.kind_is_constructor", "[completion]")
{
    auto ctors = constructorCandidates();
    for (auto const& c: ctors)
        CHECK(c.kind == CompletionKind::Constructor);
}

// =============================================================================
// dotAccessCandidates tests
// =============================================================================

TEST_CASE("CompletionCandidates.dotAccess.Option_returns_methods", "[completion]")
{
    std::unordered_map<std::string, std::vector<RecordFieldInfo>> fields;
    auto candidates = dotAccessCandidates("Option", "", fields, {}, {}, testModuleFunctions());
    CHECK(candidates.size() == 3);
    CHECK(hasCandidate(candidates, "Option.map"));
    CHECK(hasCandidate(candidates, "Option.bind"));
    CHECK(hasCandidate(candidates, "Option.defaultValue"));
}

TEST_CASE("CompletionCandidates.dotAccess.Option_filter_m", "[completion]")
{
    std::unordered_map<std::string, std::vector<RecordFieldInfo>> fields;
    auto candidates = dotAccessCandidates("Option", "m", fields, {}, {}, testModuleFunctions());
    REQUIRE(candidates.size() == 1);
    CHECK(candidates[0].text == "Option.map");
}

TEST_CASE("CompletionCandidates.dotAccess.underscore_returns_all_fields", "[completion]")
{
    std::unordered_map<std::string, std::vector<RecordFieldInfo>> fields;
    fields["ProcessInfo"] = { { .name = "pid", .typeName = "int" },
                              { .name = "user", .typeName = "str" },
                              { .name = "cpu", .typeName = "float" } };
    auto candidates = dotAccessCandidates("_", "", fields);
    CHECK(candidates.size() == 3);
    CHECK(hasCandidate(candidates, "_.pid"));
    CHECK(hasCandidate(candidates, "_.user"));
    CHECK(hasCandidate(candidates, "_.cpu"));
}

TEST_CASE("CompletionCandidates.dotAccess.underscore_filter", "[completion]")
{
    std::unordered_map<std::string, std::vector<RecordFieldInfo>> fields;
    fields["ProcessInfo"] = { { .name = "pid", .typeName = "int" },
                              { .name = "user", .typeName = "str" },
                              { .name = "cpu", .typeName = "float" } };
    auto candidates = dotAccessCandidates("_", "p", fields);
    REQUIRE(candidates.size() == 1);
    CHECK(candidates[0].text == "_.pid");
}

TEST_CASE("CompletionCandidates.dotAccess.generic_var_returns_methods_and_fields", "[completion]")
{
    std::unordered_map<std::string, std::vector<RecordFieldInfo>> fields;
    fields["ProcessInfo"] = { { .name = "pid", .typeName = "int" } };
    auto candidates = dotAccessCandidates("myVar", "", fields, {}, {}, testModuleFunctions());
    // 3 Option methods + 1 field = 4
    CHECK(candidates.size() == 4);
    CHECK(hasCandidate(candidates, "myVar.map"));
    CHECK(hasCandidate(candidates, "myVar.bind"));
    CHECK(hasCandidate(candidates, "myVar.defaultValue"));
    CHECK(hasCandidate(candidates, "myVar.pid"));
}

TEST_CASE("CompletionCandidates.dotAccess.no_duplicates_across_types", "[completion]")
{
    std::unordered_map<std::string, std::vector<RecordFieldInfo>> fields;
    fields["TypeA"] = { { .name = "name", .typeName = "str" }, { .name = "age", .typeName = "int" } };
    fields["TypeB"] = { { .name = "name", .typeName = "str" }, { .name = "score", .typeName = "int" } };
    auto candidates = dotAccessCandidates("_", "", fields);
    // "name" should only appear once
    auto nameCount = std::ranges::count_if(candidates, [](auto const& c) { return c.text == "_.name"; });
    CHECK(nameCount == 1);
}

TEST_CASE("CompletionCandidates.dotAccess.typed_fields_show_type", "[completion]")
{
    std::unordered_map<std::string, std::vector<RecordFieldInfo>> fields;
    fields["Person"] = { { .name = "name", .typeName = "str" }, { .name = "age", .typeName = "int" } };
    auto candidates = dotAccessCandidates("_", "", fields);
    auto const* nameCand = findCandidate(candidates, "_.name");
    REQUIRE(nameCand != nullptr);
    CHECK(nameCand->description.find("str") != std::string::npos);
    auto const* ageCand = findCandidate(candidates, "_.age");
    REQUIRE(ageCand != nullptr);
    CHECK(ageCand->description.find("int") != std::string::npos);
}

TEST_CASE("CompletionCandidates.dotAccess.variable_with_known_type", "[completion]")
{
    std::unordered_map<std::string, std::vector<RecordFieldInfo>> fields;
    fields["Person"] = { { .name = "name", .typeName = "str" }, { .name = "age", .typeName = "int" } };
    fields["ProcessInfo"] = { { .name = "pid", .typeName = "int" }, { .name = "cpu", .typeName = "float" } };
    std::unordered_map<std::string, std::string> variableTypes;
    variableTypes["alice"] = "Person";
    auto candidates = dotAccessCandidates("alice", "", fields, variableTypes);
    // Only Person fields, not ProcessInfo
    CHECK(candidates.size() == 2);
    CHECK(hasCandidate(candidates, "alice.name"));
    CHECK(hasCandidate(candidates, "alice.age"));
    CHECK(!hasCandidate(candidates, "alice.pid"));
}

TEST_CASE("CompletionCandidates.dotAccess.variable_with_known_type_no_option_methods", "[completion]")
{
    std::unordered_map<std::string, std::vector<RecordFieldInfo>> fields;
    fields["Person"] = { { .name = "name", .typeName = "str" } };
    std::unordered_map<std::string, std::string> variableTypes;
    variableTypes["alice"] = "Person";
    auto candidates = dotAccessCandidates("alice", "", fields, variableTypes);
    // Known-type variable should NOT get Option methods
    CHECK(!hasCandidate(candidates, "alice.map"));
    CHECK(!hasCandidate(candidates, "alice.bind"));
    CHECK(!hasCandidate(candidates, "alice.defaultValue"));
}

TEST_CASE("CompletionCandidates.dotAccess.variable_unknown_type_fallback", "[completion]")
{
    std::unordered_map<std::string, std::vector<RecordFieldInfo>> fields;
    fields["Person"] = { { .name = "name", .typeName = "str" } };
    std::unordered_map<std::string, std::string> variableTypes;
    // "bob" is NOT in variableTypes -> fallback to generic behavior
    auto candidates = dotAccessCandidates("bob", "", fields, variableTypes, {}, testModuleFunctions());
    // Should get Option methods + all fields
    CHECK(hasCandidate(candidates, "bob.map"));
    CHECK(hasCandidate(candidates, "bob.name"));
}

TEST_CASE("CompletionCandidates.dotAccess.typed_variable_TimeSpan", "[completion]")
{
    std::unordered_map<std::string, std::vector<RecordFieldInfo>> fields;
    fields["TimeSpan"] = { { .name = "milliseconds", .typeName = "int" } };
    fields["DateTime"] = { { .name = "year", .typeName = "int" },
                           { .name = "month", .typeName = "int" },
                           { .name = "epoch", .typeName = "int" } };
    std::unordered_map<std::string, std::string> variableTypes;
    variableTypes["x"] = "TimeSpan";
    auto candidates = dotAccessCandidates("x", "", fields, variableTypes, {}, testModuleFunctions());
    REQUIRE(candidates.size() == 1);
    CHECK(hasCandidate(candidates, "x.milliseconds"));
    CHECK(!hasCandidate(candidates, "x.year"));
    CHECK(!hasCandidate(candidates, "x.map"));
}

// =============================================================================
// Compound type literal dot-access tests (Size, TimeSpan)
// =============================================================================

TEST_CASE("CompletionCandidates.dotAccess.size_literal_float_shows_only_size_fields", "[completion]")
{
    std::unordered_map<std::string, std::vector<RecordFieldInfo>> fields;
    fields["Size"] = { { .name = "bytes", .typeName = "int" } };
    fields["ProcessInfo"] = { { .name = "pid", .typeName = "int" },
                              { .name = "cpu", .typeName = "float" },
                              { .name = "command", .typeName = "string" } };
    auto candidates = dotAccessCandidates("15.5MB", "", fields, {}, {}, testModuleFunctions());
    REQUIRE(candidates.size() == 1);
    CHECK(hasCandidate(candidates, "15.5MB.bytes"));
    CHECK(!hasCandidate(candidates, "15.5MB.pid"));
    CHECK(!hasCandidate(candidates, "15.5MB.map"));
}

TEST_CASE("CompletionCandidates.dotAccess.size_literal_int_shows_only_size_fields", "[completion]")
{
    std::unordered_map<std::string, std::vector<RecordFieldInfo>> fields;
    fields["Size"] = { { .name = "bytes", .typeName = "int" } };
    fields["ProcessInfo"] = { { .name = "pid", .typeName = "int" } };
    auto candidates = dotAccessCandidates("100KB", "", fields, {}, {}, testModuleFunctions());
    REQUIRE(candidates.size() == 1);
    CHECK(hasCandidate(candidates, "100KB.bytes"));
}

TEST_CASE("CompletionCandidates.dotAccess.size_literal_filter_by_prefix", "[completion]")
{
    std::unordered_map<std::string, std::vector<RecordFieldInfo>> fields;
    fields["Size"] = { { .name = "bytes", .typeName = "int" } };
    auto candidates = dotAccessCandidates("15.5MB", "b", fields);
    REQUIRE(candidates.size() == 1);
    CHECK(candidates[0].text == "15.5MB.bytes");
}

TEST_CASE("CompletionCandidates.dotAccess.size_literal_all_suffixes", "[completion]")
{
    std::unordered_map<std::string, std::vector<RecordFieldInfo>> fields;
    fields["Size"] = { { .name = "bytes", .typeName = "int" } };
    for (auto const* const suffix: { "B", "KB", "MB", "GB", "TB" })
    {
        auto literal = std::string("100") + suffix;
        auto candidates = dotAccessCandidates(literal, "", fields);
        REQUIRE(candidates.size() == 1);
        CHECK(hasCandidate(candidates, literal + ".bytes"));
    }
}

TEST_CASE("CompletionCandidates.dotAccess.timespan_literal_shows_only_timespan_fields", "[completion]")
{
    std::unordered_map<std::string, std::vector<RecordFieldInfo>> fields;
    fields["TimeSpan"] = { { .name = "milliseconds", .typeName = "int" } };
    fields["ProcessInfo"] = { { .name = "pid", .typeName = "int" } };
    auto candidates = dotAccessCandidates("500ms", "", fields, {}, {}, testModuleFunctions());
    REQUIRE(candidates.size() == 1);
    CHECK(hasCandidate(candidates, "500ms.milliseconds"));
    CHECK(!hasCandidate(candidates, "500ms.pid"));
}

TEST_CASE("CompletionCandidates.dotAccess.timespan_literal_float_seconds", "[completion]")
{
    std::unordered_map<std::string, std::vector<RecordFieldInfo>> fields;
    fields["TimeSpan"] = { { .name = "milliseconds", .typeName = "int" } };
    auto candidates = dotAccessCandidates("3.5s", "", fields);
    REQUIRE(candidates.size() == 1);
    CHECK(hasCandidate(candidates, "3.5s.milliseconds"));
}

TEST_CASE("CompletionCandidates.dotAccess.timespan_literal_all_suffixes", "[completion]")
{
    std::unordered_map<std::string, std::vector<RecordFieldInfo>> fields;
    fields["TimeSpan"] = { { .name = "milliseconds", .typeName = "int" } };
    for (auto const* const suffix: { "ms", "s", "min", "h" })
    {
        auto literal = std::string("100") + suffix;
        auto candidates = dotAccessCandidates(literal, "", fields);
        REQUIRE(candidates.size() == 1);
        CHECK(hasCandidate(candidates, literal + ".milliseconds"));
    }
}

TEST_CASE("CompletionCandidates.dotAccess.stdlib_function_not_qualifiable", "[completion]")
{
    std::unordered_map<std::string, std::vector<RecordFieldInfo>> fields;
    fields["ProcessInfo"] = { { .name = "pid", .typeName = "int" } };
    // Stdlib function names like ps, head, trim, rand should produce no dot-access candidates
    for (auto const& name: { "ps", "head", "trim", "rand" })
    {
        auto candidates = dotAccessCandidates(name, "", fields, {}, {}, testModuleFunctions());
        CHECK(candidates.empty());
    }
}

TEST_CASE("CompletionCandidates.dotAccess.underscore_typed_fields", "[completion]")
{
    std::unordered_map<std::string, std::vector<RecordFieldInfo>> fields;
    fields["Person"] = { { .name = "name", .typeName = "str" }, { .name = "age", .typeName = "int" } };
    auto candidates = dotAccessCandidates("_", "", fields);
    // Both fields should appear with type info
    CHECK(candidates.size() == 2);
    auto const* nameCand = findCandidate(candidates, "_.name");
    REQUIRE(nameCand != nullptr);
    CHECK(nameCand->description == "field: str");
    auto const* ageCand = findCandidate(candidates, "_.age");
    REQUIRE(ageCand != nullptr);
    CHECK(ageCand->description == "field: int");
}

TEST_CASE("CompletionCandidates.dotAccess.DateTime_returns_methods", "[completion]")
{
    std::unordered_map<std::string, std::vector<RecordFieldInfo>> fields;
    auto candidates = dotAccessCandidates("DateTime", "", fields, {}, {}, testModuleFunctions());
    CHECK(candidates.size() == 2);
    CHECK(hasCandidate(candidates, "DateTime.now"));
    CHECK(hasCandidate(candidates, "DateTime.fromEpoch"));
}

TEST_CASE("CompletionCandidates.dotAccess.DateTime_filter_f", "[completion]")
{
    std::unordered_map<std::string, std::vector<RecordFieldInfo>> fields;
    auto candidates = dotAccessCandidates("DateTime", "f", fields, {}, {}, testModuleFunctions());
    REQUIRE(candidates.size() == 1);
    CHECK(candidates[0].text == "DateTime.fromEpoch");
}

TEST_CASE("CompletionCandidates.dotAccess.DateTime_now_returns_DateTime_fields", "[completion]")
{
    std::unordered_map<std::string, std::vector<RecordFieldInfo>> fields;
    fields["DateTime"] = { { .name = "year", .typeName = "int" },   { .name = "month", .typeName = "int" },
                           { .name = "day", .typeName = "int" },    { .name = "hour", .typeName = "int" },
                           { .name = "minute", .typeName = "int" }, { .name = "second", .typeName = "int" },
                           { .name = "epoch", .typeName = "int" } };
    fields["ProcessInfo"] = { { .name = "pid", .typeName = "int" },
                              { .name = "cpu", .typeName = "float" },
                              { .name = "command", .typeName = "string" } };
    auto candidates = dotAccessCandidates("DateTime.now", "", fields);
    CHECK(candidates.size() == 7);
    CHECK(hasCandidate(candidates, "DateTime.now.year"));
    CHECK(hasCandidate(candidates, "DateTime.now.month"));
    CHECK(hasCandidate(candidates, "DateTime.now.day"));
    CHECK(hasCandidate(candidates, "DateTime.now.hour"));
    CHECK(hasCandidate(candidates, "DateTime.now.minute"));
    CHECK(hasCandidate(candidates, "DateTime.now.second"));
    CHECK(hasCandidate(candidates, "DateTime.now.epoch"));
}

TEST_CASE("CompletionCandidates.dotAccess.DateTime_now_filter_by_prefix", "[completion]")
{
    std::unordered_map<std::string, std::vector<RecordFieldInfo>> fields;
    fields["DateTime"] = { { .name = "year", .typeName = "int" },   { .name = "month", .typeName = "int" },
                           { .name = "day", .typeName = "int" },    { .name = "hour", .typeName = "int" },
                           { .name = "minute", .typeName = "int" }, { .name = "second", .typeName = "int" },
                           { .name = "epoch", .typeName = "int" } };
    fields["ProcessInfo"] = { { .name = "pid", .typeName = "int" },
                              { .name = "cpu", .typeName = "float" },
                              { .name = "command", .typeName = "string" } };
    auto candidates = dotAccessCandidates("DateTime.now", "m", fields);
    REQUIRE(candidates.size() == 2);
    CHECK(hasCandidate(candidates, "DateTime.now.month"));
    CHECK(hasCandidate(candidates, "DateTime.now.minute"));
}

TEST_CASE("CompletionCandidates.dotAccess.nested_dot_resolves_through_types", "[completion]")
{
    std::unordered_map<std::string, std::vector<RecordFieldInfo>> fields;
    fields["FileInfo"] = { { .name = "name", .typeName = "str" },
                           { .name = "size", .typeName = "Size" },
                           { .name = "mtime", .typeName = "DateTime" } };
    fields["Size"] = { { .name = "bytes", .typeName = "int" } };
    fields["DateTime"] = { { .name = "year", .typeName = "int" },   { .name = "month", .typeName = "int" },
                           { .name = "day", .typeName = "int" },    { .name = "hour", .typeName = "int" },
                           { .name = "minute", .typeName = "int" }, { .name = "second", .typeName = "int" },
                           { .name = "epoch", .typeName = "int" } };
    std::unordered_map<std::string, std::string> variableTypes;
    variableTypes["f"] = "FileInfo";
    auto candidates = dotAccessCandidates("f.mtime", "", fields, variableTypes);
    CHECK(candidates.size() == 7);
    CHECK(hasCandidate(candidates, "f.mtime.year"));
    CHECK(hasCandidate(candidates, "f.mtime.month"));
    CHECK(hasCandidate(candidates, "f.mtime.day"));
    CHECK(hasCandidate(candidates, "f.mtime.hour"));
    CHECK(hasCandidate(candidates, "f.mtime.minute"));
    CHECK(hasCandidate(candidates, "f.mtime.second"));
    CHECK(hasCandidate(candidates, "f.mtime.epoch"));
}

TEST_CASE("CompletionCandidates.dotAccess.nested_dot_FileInfo_size_bytes", "[completion]")
{
    std::unordered_map<std::string, std::vector<RecordFieldInfo>> fields;
    fields["FileInfo"] = {
        { .name = "name", .typeName = "str" },   { .name = "size", .typeName = "Size" },
        { .name = "mode", .typeName = "int" },   { .name = "mtime", .typeName = "DateTime" },
        { .name = "isDir", .typeName = "bool" },
    };
    fields["Size"] = { { .name = "bytes", .typeName = "int" } };
    fields["DateTime"] = { { .name = "year", .typeName = "int" },   { .name = "month", .typeName = "int" },
                           { .name = "day", .typeName = "int" },    { .name = "hour", .typeName = "int" },
                           { .name = "minute", .typeName = "int" }, { .name = "second", .typeName = "int" },
                           { .name = "epoch", .typeName = "int" } };
    std::unordered_map<std::string, std::string> variableTypes;
    variableTypes["f"] = "FileInfo";
    auto candidates = dotAccessCandidates("f.size", "", fields, variableTypes);
    REQUIRE(candidates.size() == 1);
    CHECK(hasCandidate(candidates, "f.size.bytes"));
}

TEST_CASE("CompletionCandidates.dotAccess.nested_dot_filter_by_prefix", "[completion]")
{
    std::unordered_map<std::string, std::vector<RecordFieldInfo>> fields;
    fields["FileInfo"] = { { .name = "name", .typeName = "str" },
                           { .name = "mtime", .typeName = "DateTime" } };
    fields["DateTime"] = { { .name = "year", .typeName = "int" },   { .name = "month", .typeName = "int" },
                           { .name = "day", .typeName = "int" },    { .name = "hour", .typeName = "int" },
                           { .name = "minute", .typeName = "int" }, { .name = "second", .typeName = "int" },
                           { .name = "epoch", .typeName = "int" } };
    std::unordered_map<std::string, std::string> variableTypes;
    variableTypes["f"] = "FileInfo";
    auto candidates = dotAccessCandidates("f.mtime", "y", fields, variableTypes);
    REQUIRE(candidates.size() == 1);
    CHECK(candidates[0].text == "f.mtime.year");
}

// =============================================================================
// symbolCandidates tests
// =============================================================================

TEST_CASE("CompletionCandidates.symbolCandidates.formats_function", "[completion]")
{
    std::vector<SymbolDefinitionInfo> symbols = { {
        .name = "add",
        .isFunction = true,
        .parameterNames = { "x", "y" },
        .parameterTypes = { std::nullopt, std::nullopt },
    } };
    auto candidates = symbolCandidates(symbols);
    REQUIRE(candidates.size() == 1);
    CHECK(candidates[0].text == "add");
    CHECK(candidates[0].kind == CompletionKind::Function);
    CHECK(candidates[0].description.find("add(x, y)") != std::string::npos);
}

TEST_CASE("CompletionCandidates.symbolCandidates.formats_typed_function", "[completion]")
{
    std::vector<SymbolDefinitionInfo> symbols = { {
        .name = "add",
        .isFunction = true,
        .parameterNames = { "x", "y" },
        .parameterTypes = { std::optional<std::string>("int"), std::optional<std::string>("int") },
        .returnType = "int",
    } };
    auto candidates = symbolCandidates(symbols);
    REQUIRE(candidates.size() == 1);
    CHECK(candidates[0].description.find("int") != std::string::npos);
    CHECK(candidates[0].description.find("-> int") != std::string::npos);
}

TEST_CASE("CompletionCandidates.symbolCandidates.formats_value", "[completion]")
{
    std::vector<SymbolDefinitionInfo> symbols = { {
        .name = "x",
        .isFunction = false,
    } };
    auto candidates = symbolCandidates(symbols);
    REQUIRE(candidates.size() == 1);
    CHECK(candidates[0].text == "x");
    CHECK(candidates[0].kind == CompletionKind::Variable);
    CHECK(candidates[0].description == "value");
}

TEST_CASE("CompletionCandidates.symbolCandidates.formats_mutable_value", "[completion]")
{
    std::vector<SymbolDefinitionInfo> symbols = { {
        .name = "counter",
        .isFunction = false,
        .isMutable = true,
    } };
    auto candidates = symbolCandidates(symbols);
    REQUIRE(candidates.size() == 1);
    CHECK(candidates[0].description == "mutable value");
}

TEST_CASE("CompletionCandidates.symbolCandidates.formats_recursive_function", "[completion]")
{
    std::vector<SymbolDefinitionInfo> symbols = { {
        .name = "fact",
        .isFunction = true,
        .parameterNames = { "n" },
        .parameterTypes = { std::nullopt },
        .isRecursive = true,
    } };
    auto candidates = symbolCandidates(symbols);
    REQUIRE(candidates.size() == 1);
    CHECK(candidates[0].description.find("rec") != std::string::npos);
}

// =============================================================================
// builtinArgumentCandidates tests
// =============================================================================

TEST_CASE("CompletionCandidates.builtinArgumentCandidates.preset_returns_all_values", "[completion]")
{
    auto candidates = builtinArgumentCandidates("shell_prompt_preset", "");
    CHECK(candidates.size() == 10);
    CHECK(hasCandidate(candidates, "minimal-arrow"));
    CHECK(hasCandidate(candidates, "powerline"));
    CHECK(hasCandidate(candidates, "endo-signature"));
    for (auto const& c: candidates)
        CHECK(c.kind == CompletionKind::EnumValue);
}

TEST_CASE("CompletionCandidates.builtinArgumentCandidates.preset_filters_by_prefix", "[completion]")
{
    auto candidates = builtinArgumentCandidates("shell_prompt_preset", "pow");
    REQUIRE(candidates.size() == 1);
    CHECK(candidates[0].text == "powerline");
}

TEST_CASE("CompletionCandidates.builtinArgumentCandidates.layout_returns_values", "[completion]")
{
    auto candidates = builtinArgumentCandidates("shell_prompt_layout", "");
    CHECK(candidates.size() == 4);
    CHECK(hasCandidate(candidates, "single-line"));
    CHECK(hasCandidate(candidates, "two-line"));
    CHECK(hasCandidate(candidates, "boxed"));
    CHECK(hasCandidate(candidates, "powerline"));
}

TEST_CASE("CompletionCandidates.builtinArgumentCandidates.separator_returns_values", "[completion]")
{
    auto candidates = builtinArgumentCandidates("shell_prompt_separator", "");
    CHECK(candidates.size() == 5);
    CHECK(hasCandidate(candidates, "none"));
    CHECK(hasCandidate(candidates, "bar"));
    CHECK(hasCandidate(candidates, "powerline"));
    CHECK(hasCandidate(candidates, "rounded"));
    CHECK(hasCandidate(candidates, "boxed"));
}

TEST_CASE("CompletionCandidates.builtinArgumentCandidates.transient_returns_values", "[completion]")
{
    auto candidates = builtinArgumentCandidates("shell_prompt_transient", "");
    CHECK(candidates.size() == 3);
    CHECK(hasCandidate(candidates, "off"));
    CHECK(hasCandidate(candidates, "minimal"));
    CHECK(hasCandidate(candidates, "arrow"));
}

TEST_CASE("CompletionCandidates.builtinArgumentCandidates.indicator_returns_empty", "[completion]")
{
    auto candidates = builtinArgumentCandidates("shell_prompt_indicator", "");
    CHECK(candidates.empty());
}

TEST_CASE("CompletionCandidates.builtinArgumentCandidates.duration_threshold_returns_empty", "[completion]")
{
    auto candidates = builtinArgumentCandidates("shell_prompt_duration_threshold", "");
    CHECK(candidates.empty());
}

TEST_CASE("CompletionCandidates.builtinArgumentCandidates.unknown_command_returns_empty", "[completion]")
{
    auto candidates = builtinArgumentCandidates("ls", "");
    CHECK(candidates.empty());
}

TEST_CASE("CompletionCandidates.builtinArgumentCandidates.claude_model_returns_values", "[completion]")
{
    auto candidates = builtinArgumentCandidates("agent_claude_model", "");
    CHECK(candidates.size() == 5);
    CHECK(hasCandidate(candidates, "claude-opus-4-6"));
    CHECK(hasCandidate(candidates, "claude-sonnet-4-6"));
    CHECK(hasCandidate(candidates, "claude-haiku-4-5-20251001"));
    CHECK(hasCandidate(candidates, "claude-sonnet-4-5-20250929"));
    CHECK(hasCandidate(candidates, "claude-opus-4-20250514"));
}

TEST_CASE("CompletionCandidates.builtinArgumentCandidates.openai_model_returns_values", "[completion]")
{
    auto candidates = builtinArgumentCandidates("agent_openai_model", "");
    CHECK(candidates.size() == 4);
    CHECK(hasCandidate(candidates, "gpt-4o"));
    CHECK(hasCandidate(candidates, "gpt-4o-mini"));
    CHECK(hasCandidate(candidates, "o3-mini"));
    CHECK(hasCandidate(candidates, "o1"));
}

TEST_CASE("CompletionCandidates.builtinArgumentCandidates.openai_compat_model_returns_values", "[completion]")
{
    auto candidates = builtinArgumentCandidates("agent_openai_compat_model", "");
    CHECK(candidates.size() == 4);
    CHECK(hasCandidate(candidates, "gpt-4o"));
}

TEST_CASE("CompletionCandidates.builtinArgumentCandidates.gemini_model_returns_values", "[completion]")
{
    auto candidates = builtinArgumentCandidates("agent_gemini_model", "");
    CHECK(candidates.size() == 3);
    CHECK(hasCandidate(candidates, "gemini-2.5-flash"));
    CHECK(hasCandidate(candidates, "gemini-2.5-pro"));
    CHECK(hasCandidate(candidates, "gemini-2.0-flash"));
}

TEST_CASE("CompletionCandidates.builtinArgumentCandidates.thinking_mode_returns_values", "[completion]")
{
    auto candidates = builtinArgumentCandidates("agent_claude_thinking_mode", "");
    CHECK(candidates.size() == 3);
    CHECK(hasCandidate(candidates, "off"));
    CHECK(hasCandidate(candidates, "normal"));
    CHECK(hasCandidate(candidates, "extended"));
}

TEST_CASE("CompletionCandidates.builtinArgumentCandidates.thinking_mode_all_providers", "[completion]")
{
    for (auto const* prop: { "agent_claude_thinking_mode",
                             "agent_openai_thinking_mode",
                             "agent_openai_compat_thinking_mode",
                             "agent_gemini_thinking_mode" })
    {
        auto candidates = builtinArgumentCandidates(prop, "");
        CHECK(candidates.size() == 3);
    }
}

TEST_CASE("CompletionCandidates.builtinArgumentCandidates.auth_type_returns_values", "[completion]")
{
    auto candidates = builtinArgumentCandidates("agent_claude_auth_type", "");
    CHECK(candidates.size() == 3);
    CHECK(hasCandidate(candidates, "auto"));
    CHECK(hasCandidate(candidates, "oauth"));
    CHECK(hasCandidate(candidates, "api_key"));
}

TEST_CASE("CompletionCandidates.builtinArgumentCandidates.model_filters_by_prefix", "[completion]")
{
    auto candidates = builtinArgumentCandidates("agent_claude_model", "claude-opus");
    REQUIRE(candidates.size() == 2);
    CHECK(hasCandidate(candidates, "claude-opus-4-6"));
    CHECK(hasCandidate(candidates, "claude-opus-4-20250514"));
}

// =============================================================================
// isBuiltinWithArgumentCompletion tests
// =============================================================================

TEST_CASE("CompletionCandidates.isBuiltinWithArgumentCompletion.set_prompt_commands", "[completion]")
{
    CHECK(isBuiltinWithArgumentCompletion("shell_prompt_preset"));
    CHECK_FALSE(
        isBuiltinWithArgumentCompletion("shell_prompt_indicator")); // free-form string, no enum values
    CHECK(isBuiltinWithArgumentCompletion("shell_prompt_layout"));
    CHECK(isBuiltinWithArgumentCompletion("shell_prompt_separator"));
    CHECK(isBuiltinWithArgumentCompletion("shell_prompt_transient"));
    CHECK_FALSE(
        isBuiltinWithArgumentCompletion("shell_prompt_duration_threshold")); // numeric, no enum values
}

TEST_CASE("CompletionCandidates.isBuiltinWithArgumentCompletion.agent_model_and_thinking", "[completion]")
{
    CHECK(isBuiltinWithArgumentCompletion("agent_claude_model"));
    CHECK(isBuiltinWithArgumentCompletion("agent_openai_model"));
    CHECK(isBuiltinWithArgumentCompletion("agent_openai_compat_model"));
    CHECK(isBuiltinWithArgumentCompletion("agent_gemini_model"));
    CHECK(isBuiltinWithArgumentCompletion("agent_claude_thinking_mode"));
    CHECK(isBuiltinWithArgumentCompletion("agent_openai_thinking_mode"));
    CHECK(isBuiltinWithArgumentCompletion("agent_openai_compat_thinking_mode"));
    CHECK(isBuiltinWithArgumentCompletion("agent_gemini_thinking_mode"));
    CHECK(isBuiltinWithArgumentCompletion("agent_claude_auth_type"));
}

TEST_CASE("CompletionCandidates.isBuiltinWithArgumentCompletion.non_builtins_return_false", "[completion]")
{
    CHECK_FALSE(isBuiltinWithArgumentCompletion("echo"));
    CHECK_FALSE(isBuiltinWithArgumentCompletion("ls"));
    CHECK_FALSE(isBuiltinWithArgumentCompletion("cd"));
    CHECK_FALSE(isBuiltinWithArgumentCompletion("shell_prompt"));
}

// =============================================================================
// standardLibraryCandidates tests
// =============================================================================

TEST_CASE("CompletionCandidates.standardLibraryCandidates.returns_all_entries", "[completion][stdlib]")
{
    auto stdlib = standardLibraryCandidates();
    CHECK(stdlib.size() == 65);
}

TEST_CASE("CompletionCandidates.standardLibraryCandidates.all_have_function_kind", "[completion][stdlib]")
{
    auto stdlib = standardLibraryCandidates();
    for (auto const& entry: stdlib)
        CHECK(entry.kind == CompletionKind::Function);
}

TEST_CASE("CompletionCandidates.standardLibraryCandidates.all_have_descriptions", "[completion][stdlib]")
{
    auto stdlib = standardLibraryCandidates();
    for (auto const& entry: stdlib)
        CHECK(!entry.description.empty());
}

TEST_CASE("CompletionCandidates.standardLibraryCandidates.type_conversion_functions", "[completion][stdlib]")
{
    auto stdlib = standardLibraryCandidates();
    CHECK(hasCandidate(stdlib, "string_length"));
    CHECK(hasCandidate(stdlib, "int_of_string"));
    CHECK(hasCandidate(stdlib, "string_of_int"));
    CHECK(hasCandidate(stdlib, "not"));
}

TEST_CASE("CompletionCandidates.standardLibraryCandidates.string_operations", "[completion][stdlib]")
{
    auto stdlib = standardLibraryCandidates();
    CHECK(hasCandidate(stdlib, "trim"));
    CHECK(hasCandidate(stdlib, "toLower"));
    CHECK(hasCandidate(stdlib, "toUpper"));
    CHECK(hasCandidate(stdlib, "contains"));
    CHECK(hasCandidate(stdlib, "startsWith"));
    CHECK(hasCandidate(stdlib, "endsWith"));
    CHECK(hasCandidate(stdlib, "replace"));
    CHECK(hasCandidate(stdlib, "split"));
    CHECK(hasCandidate(stdlib, "join"));
}

TEST_CASE("CompletionCandidates.standardLibraryCandidates.list_basic_operations", "[completion][stdlib]")
{
    auto stdlib = standardLibraryCandidates();
    CHECK(hasCandidate(stdlib, "head"));
    CHECK(hasCandidate(stdlib, "tail"));
    CHECK(hasCandidate(stdlib, "length"));
    CHECK(hasCandidate(stdlib, "isEmpty"));
    CHECK(hasCandidate(stdlib, "nth"));
    CHECK(hasCandidate(stdlib, "last"));
    CHECK(hasCandidate(stdlib, "replicate"));
}

TEST_CASE("CompletionCandidates.standardLibraryCandidates.list_hofs", "[completion][stdlib]")
{
    auto stdlib = standardLibraryCandidates();
    CHECK(hasCandidate(stdlib, "map"));
    CHECK(hasCandidate(stdlib, "filter"));
    CHECK(hasCandidate(stdlib, "fold"));
    CHECK(hasCandidate(stdlib, "reduce"));
    CHECK(hasCandidate(stdlib, "find"));
    CHECK(hasCandidate(stdlib, "exists"));
    CHECK(hasCandidate(stdlib, "forall"));
    CHECK(hasCandidate(stdlib, "each"));
}

TEST_CASE("CompletionCandidates.standardLibraryCandidates.list_transforms", "[completion][stdlib]")
{
    auto stdlib = standardLibraryCandidates();
    CHECK(hasCandidate(stdlib, "sort"));
    CHECK(hasCandidate(stdlib, "reverse"));
    CHECK(hasCandidate(stdlib, "distinct"));
    CHECK(hasCandidate(stdlib, "sortBy"));
    CHECK(hasCandidate(stdlib, "groupBy"));
    CHECK(hasCandidate(stdlib, "take"));
    CHECK(hasCandidate(stdlib, "drop"));
    CHECK(hasCandidate(stdlib, "zip"));
    CHECK(hasCandidate(stdlib, "flatten"));
}

TEST_CASE("CompletionCandidates.standardLibraryCandidates.formatting_helpers", "[completion][stdlib]")
{
    auto stdlib = standardLibraryCandidates();
    CHECK(hasCandidate(stdlib, "formatNumber"));
    CHECK(hasCandidate(stdlib, "formatDateTime"));
    CHECK(hasCandidate(stdlib, "formatMode"));
    CHECK(hasCandidate(stdlib, "toText"));
    CHECK(hasCandidate(stdlib, "string"));
}

TEST_CASE("CompletionCandidates.standardLibraryCandidates.permission_tests", "[completion][stdlib]")
{
    auto stdlib = standardLibraryCandidates();
    CHECK(hasCandidate(stdlib, "isReadable"));
    CHECK(hasCandidate(stdlib, "isWritable"));
    CHECK(hasCandidate(stdlib, "isExecutable"));
}

TEST_CASE("CompletionCandidates.standardLibraryCandidates.env_system_functions", "[completion][stdlib]")
{
    auto stdlib = standardLibraryCandidates();
    CHECK(hasCandidate(stdlib, "env"));
    CHECK(hasCandidate(stdlib, "which"));
    CHECK(hasCandidate(stdlib, "ps"));
    CHECK(hasCandidate(stdlib, "ls"));
    CHECK(hasCandidate(stdlib, "rand"));
    CHECK(hasCandidate(stdlib, "fetch"));
}

TEST_CASE("CompletionCandidates.standardLibraryCandidates.module_functions_from_registry",
          "[completion][stdlib]")
{
    // Module function constructors (DateTime.*, Size.*, FileMode.*) are now generated
    // from the TypeRegistry via moduleFunctionStdLibCandidates(), not stdLibFunctions.
    CoreVM::TypeRegistry registry;
    auto moduleCandidates = moduleFunctionStdLibCandidates(registry);
    CHECK(hasCandidate(moduleCandidates, "DateTime.now"));
    CHECK(hasCandidate(moduleCandidates, "DateTime.fromEpoch"));
    CHECK(hasCandidate(moduleCandidates, "FileMode.fromBits"));
    CHECK(hasCandidate(moduleCandidates, "Size.fromBytes"));
    CHECK(hasCandidate(moduleCandidates, "Size.fromKB"));
    CHECK(hasCandidate(moduleCandidates, "Size.fromMB"));
    CHECK(hasCandidate(moduleCandidates, "Size.fromGB"));
    CHECK(hasCandidate(moduleCandidates, "Size.fromTB"));
    CHECK(hasCandidate(moduleCandidates, "TimeSpan.fromMilliseconds"));
    CHECK(hasCandidate(moduleCandidates, "TimeSpan.fromSeconds"));
    CHECK(hasCandidate(moduleCandidates, "TimeSpan.fromMinutes"));
    CHECK(hasCandidate(moduleCandidates, "TimeSpan.fromHours"));
    CHECK(hasCandidate(moduleCandidates, "TimeSpan.fromDays"));
}

TEST_CASE("CompletionCandidates.standardLibraryCandidates.excludes_builtins", "[completion][stdlib]")
{
    auto stdlib = standardLibraryCandidates();
    CHECK(!hasCandidate(stdlib, "print"));
    CHECK(!hasCandidate(stdlib, "println"));
}

TEST_CASE("CompletionCandidates.standardLibraryCandidates.signature_descriptions", "[completion][stdlib]")
{
    auto stdlib = standardLibraryCandidates();
    auto const* mapEntry = findCandidate(stdlib, "map");
    REQUIRE(mapEntry != nullptr);
    CHECK(mapEntry->description == "map f lst -> list<'b>");

    auto const* filterEntry = findCandidate(stdlib, "filter");
    REQUIRE(filterEntry != nullptr);
    CHECK(filterEntry->description == "filter pred lst -> list<'a>");

    auto const* foldEntry = findCandidate(stdlib, "fold");
    REQUIRE(foldEntry != nullptr);
    CHECK(foldEntry->description == "fold f init lst -> 'b");
}

// =============================================================================
// resolvePipelineSourceType tests
// =============================================================================

TEST_CASE("CompletionCandidates.resolvePipelineSourceType.simple_command", "[completion]")
{
    std::unordered_map<std::string, std::string> outputTypes;
    outputTypes["ls"] = "FileInfo";
    outputTypes["ps"] = "ProcessInfo";

    CHECK(resolvePipelineSourceType("ls |> map _.", outputTypes) == "FileInfo");
    CHECK(resolvePipelineSourceType("ps |> filter (_.cpu > 5)", outputTypes) == "ProcessInfo");
}

TEST_CASE("CompletionCandidates.resolvePipelineSourceType.chained_pipeline", "[completion]")
{
    std::unordered_map<std::string, std::string> outputTypes;
    outputTypes["ls"] = "FileInfo";

    // Chained pipelines: first |> determines source
    CHECK(resolvePipelineSourceType("ls |> filter (_.isDir) |> map _.", outputTypes) == "FileInfo");
}

TEST_CASE("CompletionCandidates.resolvePipelineSourceType.no_pipeline", "[completion]")
{
    std::unordered_map<std::string, std::string> outputTypes;
    outputTypes["ls"] = "FileInfo";

    CHECK(resolvePipelineSourceType("echo hello", outputTypes).empty());
}

TEST_CASE("CompletionCandidates.resolvePipelineSourceType.unknown_command", "[completion]")
{
    std::unordered_map<std::string, std::string> outputTypes;
    outputTypes["ls"] = "FileInfo";

    CHECK(resolvePipelineSourceType("someCommand |> map _.", outputTypes).empty());
}

TEST_CASE("CompletionCandidates.resolvePipelineSourceType.multi_word_command", "[completion]")
{
    std::unordered_map<std::string, std::string> outputTypes;
    // NUL-separated key for multi-word command
    std::string key = "docker";
    key += '\0';
    key += "ps";
    outputTypes[key] = "DockerContainer";

    CHECK(resolvePipelineSourceType("docker ps |> map _.names", outputTypes) == "DockerContainer");
}

TEST_CASE("CompletionCandidates.resolvePipelineSourceType.empty_input", "[completion]")
{
    std::unordered_map<std::string, std::string> outputTypes;
    CHECK(resolvePipelineSourceType("", outputTypes).empty());
    CHECK(resolvePipelineSourceType("|> map _.", outputTypes).empty());
}

TEST_CASE("CompletionCandidates.dotAccess.underscore_with_pipeline_type", "[completion]")
{
    std::unordered_map<std::string, std::vector<RecordFieldInfo>> fields;
    fields["FileInfo"] = { { .name = "name", .typeName = "str" }, { .name = "isDir", .typeName = "bool" } };
    fields["ProcessInfo"] = { { .name = "pid", .typeName = "int" }, { .name = "cpu", .typeName = "float" } };

    // With pipeline type: only FileInfo fields
    auto candidates = dotAccessCandidates("_", "", fields, {}, "FileInfo");
    CHECK(candidates.size() == 2);
    CHECK(hasCandidate(candidates, "_.name"));
    CHECK(hasCandidate(candidates, "_.isDir"));
    CHECK(!hasCandidate(candidates, "_.pid"));
}

TEST_CASE("CompletionCandidates.dotAccess.underscore_without_pipeline_type", "[completion]")
{
    std::unordered_map<std::string, std::vector<RecordFieldInfo>> fields;
    fields["FileInfo"] = { { .name = "name", .typeName = "str" }, { .name = "isDir", .typeName = "bool" } };
    fields["ProcessInfo"] = { { .name = "pid", .typeName = "int" }, { .name = "cpu", .typeName = "float" } };

    // Without pipeline type: all fields (existing behavior)
    auto candidates = dotAccessCandidates("_", "", fields, {}, "");
    CHECK(candidates.size() == 4);
    CHECK(hasCandidate(candidates, "_.name"));
    CHECK(hasCandidate(candidates, "_.pid"));
}

// =============================================================================
// Nested underscore dot-access tests (pipeline type resolution)
// =============================================================================

TEST_CASE("CompletionCandidates.dotAccess.nested_underscore_mode_shows_FileMode_fields", "[completion]")
{
    std::unordered_map<std::string, std::vector<RecordFieldInfo>> fields;
    fields["FileInfo"] = {
        { .name = "name", .typeName = "str" },      { .name = "size", .typeName = "Size" },
        { .name = "mode", .typeName = "FileMode" }, { .name = "mtime", .typeName = "DateTime" },
        { .name = "isDir", .typeName = "bool" },
    };
    fields["FileMode"] = { { .name = "bits", .typeName = "int" } };
    fields["DateTime"] = { { .name = "year", .typeName = "int" },   { .name = "month", .typeName = "int" },
                           { .name = "day", .typeName = "int" },    { .name = "hour", .typeName = "int" },
                           { .name = "minute", .typeName = "int" }, { .name = "second", .typeName = "int" },
                           { .name = "epoch", .typeName = "int" } };
    fields["Size"] = { { .name = "bytes", .typeName = "int" } };

    auto candidates = dotAccessCandidates("_.mode", "", fields, {}, "FileInfo");
    REQUIRE(candidates.size() == 1);
    CHECK(hasCandidate(candidates, "_.mode.bits"));
}

TEST_CASE("CompletionCandidates.dotAccess.nested_underscore_mtime_shows_DateTime_fields", "[completion]")
{
    std::unordered_map<std::string, std::vector<RecordFieldInfo>> fields;
    fields["FileInfo"] = {
        { .name = "name", .typeName = "str" },      { .name = "size", .typeName = "Size" },
        { .name = "mode", .typeName = "FileMode" }, { .name = "mtime", .typeName = "DateTime" },
        { .name = "isDir", .typeName = "bool" },
    };
    fields["FileMode"] = { { .name = "bits", .typeName = "int" } };
    fields["DateTime"] = { { .name = "year", .typeName = "int" },   { .name = "month", .typeName = "int" },
                           { .name = "day", .typeName = "int" },    { .name = "hour", .typeName = "int" },
                           { .name = "minute", .typeName = "int" }, { .name = "second", .typeName = "int" },
                           { .name = "epoch", .typeName = "int" } };

    auto candidates = dotAccessCandidates("_.mtime", "", fields, {}, "FileInfo");
    CHECK(candidates.size() == 7);
    CHECK(hasCandidate(candidates, "_.mtime.year"));
    CHECK(hasCandidate(candidates, "_.mtime.month"));
    CHECK(hasCandidate(candidates, "_.mtime.epoch"));
}

TEST_CASE("CompletionCandidates.dotAccess.nested_underscore_mode_filter_by_prefix", "[completion]")
{
    std::unordered_map<std::string, std::vector<RecordFieldInfo>> fields;
    fields["FileInfo"] = { { .name = "mode", .typeName = "FileMode" } };
    fields["FileMode"] = { { .name = "bits", .typeName = "int" } };

    auto candidates = dotAccessCandidates("_.mode", "b", fields, {}, "FileInfo");
    REQUIRE(candidates.size() == 1);
    CHECK(candidates[0].text == "_.mode.bits");
}

TEST_CASE("CompletionCandidates.dotAccess.nested_underscore_no_pipeline_type_falls_back", "[completion]")
{
    std::unordered_map<std::string, std::vector<RecordFieldInfo>> fields;
    fields["FileInfo"] = { { .name = "mode", .typeName = "FileMode" } };
    fields["FileMode"] = { { .name = "bits", .typeName = "int" } };
    fields["ProcessInfo"] = { { .name = "pid", .typeName = "int" } };

    // No pipeline type → falls through to fallback (all fields)
    auto candidates = dotAccessCandidates("_.mode", "", fields, {}, "");
    // Can't resolve chain without pipeline type, so falls back to all fields
    CHECK(!candidates.empty());
}

// =============================================================================
// Shell builtin count stability test
// =============================================================================

TEST_CASE("CompletionCandidates.builtinCandidates.shell_builtin_count_stability", "[completion][invariants]")
{
    auto builtins = builtinCandidates();
    size_t nonPropertyCount = 0;
    for (auto const& b: builtins)
        if (b.kind == CompletionKind::Builtin)
            ++nonPropertyCount;
    // 70 shell builtins + 11 shell keywords + 65 stdlib functions = 146
    CHECK(nonPropertyCount == 146);
}

// =============================================================================
// detail field tests
// =============================================================================

TEST_CASE("CompletionCandidates.standardLibraryCandidates.all_have_detail", "[completion][detail]")
{
    auto stdlib = standardLibraryCandidates();
    for (auto const& entry: stdlib)
        CHECK(!entry.detail.empty());
}

TEST_CASE("CompletionCandidates.builtinCandidates.all_have_detail", "[completion][detail]")
{
    auto builtins = builtinCandidates();
    for (auto const& b: builtins)
        CHECK(!b.detail.empty());
}

TEST_CASE("CompletionCandidates.keywordCandidates.all_have_detail", "[completion][detail]")
{
    auto keywords = keywordCandidates();
    for (auto const& kw: keywords)
        CHECK(!kw.detail.empty());
}

TEST_CASE("CompletionCandidates.dotAccess.fields_have_detail", "[completion][detail]")
{
    std::unordered_map<std::string, std::vector<RecordFieldInfo>> fields;
    fields["ProcessInfo"] = { { .name = "pid", .typeName = "int" }, { .name = "cpu", .typeName = "float" } };
    auto candidates = dotAccessCandidates("_", "", fields);
    for (auto const& c: candidates)
        CHECK(!c.detail.empty());
}

TEST_CASE("CompletionCandidates.constructorCandidates.all_have_detail", "[completion][detail]")
{
    auto ctors = constructorCandidates();
    for (auto const& c: ctors)
        CHECK(!c.detail.empty());
}

TEST_CASE("CompletionCandidates.symbolCandidates.functions_have_detail", "[completion][detail]")
{
    std::vector<SymbolDefinitionInfo> symbols = { {
        .name = "add",
        .isFunction = true,
        .parameterNames = { "x", "y" },
        .parameterTypes = { std::nullopt, std::nullopt },
    } };
    auto candidates = symbolCandidates(symbols);
    REQUIRE(candidates.size() == 1);
    CHECK(!candidates[0].detail.empty());
    CHECK(candidates[0].detail.find("add") != std::string::npos);
}
