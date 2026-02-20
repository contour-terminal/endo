// SPDX-License-Identifier: Apache-2.0
#include <endo-language/Completer.hpp>
#include <endo-language/CompletionContext.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using namespace endo;

namespace
{

/// @brief Helper to check if a specific text is in the candidates.
bool hasCandidate(std::vector<CompletionCandidate> const& items, std::string const& text)
{
    return std::ranges::any_of(items, [&](auto const& c) { return c.text == text; });
}

} // namespace

// =============================================================================
// CompletionContextAnalyzer tests
// =============================================================================

TEST_CASE("CompletionContext.empty_input_is_command", "[completion][context]")
{
    auto ctx = CompletionContextAnalyzer::analyze("", 0);
    CHECK(ctx.type == CompletionContextType::Command);
    CHECK(ctx.prefix.empty());
}

TEST_CASE("CompletionContext.first_word_is_command", "[completion][context]")
{
    auto ctx = CompletionContextAnalyzer::analyze("le", 2);
    CHECK(ctx.type == CompletionContextType::Command);
    CHECK(ctx.prefix == "le");
}

TEST_CASE("CompletionContext.after_command_is_argument", "[completion][context]")
{
    auto ctx = CompletionContextAnalyzer::analyze("echo he", 7);
    CHECK(ctx.type == CompletionContextType::Argument);
    CHECK(ctx.prefix == "he");
}

TEST_CASE("CompletionContext.dollar_is_variable", "[completion][context]")
{
    auto ctx = CompletionContextAnalyzer::analyze("$HO", 3);
    CHECK(ctx.type == CompletionContextType::Variable);
    CHECK(ctx.prefix == "HO");
}

TEST_CASE("CompletionContext.dollar_brace_is_variable_brace", "[completion][context]")
{
    auto ctx = CompletionContextAnalyzer::analyze("${HO", 4);
    CHECK(ctx.type == CompletionContextType::VariableBrace);
    CHECK(ctx.prefix == "HO");
}

TEST_CASE("CompletionContext.path_prefix_is_filepath", "[completion][context]")
{
    auto ctx = CompletionContextAnalyzer::analyze("./src", 5);
    CHECK(ctx.type == CompletionContextType::FilePath);
    CHECK(ctx.prefix == "./src");
}

TEST_CASE("CompletionContext.dash_is_option", "[completion][context]")
{
    auto ctx = CompletionContextAnalyzer::analyze("ls -l", 5);
    CHECK(ctx.type == CompletionContextType::Option);
}

TEST_CASE("CompletionContext.after_pipe_is_command", "[completion][context]")
{
    auto ctx = CompletionContextAnalyzer::analyze("ls | gr", 7);
    CHECK(ctx.type == CompletionContextType::Command);
    CHECK(ctx.prefix == "gr");
}

TEST_CASE("CompletionContext.after_redirect_is_redirect", "[completion][context]")
{
    auto ctx = CompletionContextAnalyzer::analyze("echo > ", 7);
    CHECK(ctx.type == CompletionContextType::Redirect);
}

// =============================================================================
// computeCompletions integration tests
// =============================================================================

TEST_CASE("Completer.empty_input_returns_keywords_and_builtins", "[completion][completer]")
{
    CompletionDataSource dataSource;
    auto results = computeCompletions("", 0, dataSource);
    CHECK(!results.empty());
    CHECK(hasCandidate(results, "let"));
    CHECK(hasCandidate(results, "cd"));
    CHECK(hasCandidate(results, "Some"));
}

TEST_CASE("Completer.le_prefix_returns_let", "[completion][completer]")
{
    CompletionDataSource dataSource;
    auto results = computeCompletions("le", 2, dataSource);
    CHECK(hasCandidate(results, "let"));
    // Should not contain unrelated items
    CHECK(!hasCandidate(results, "cd"));
}

TEST_CASE("Completer.Option_dot_returns_methods", "[completion][completer]")
{
    CompletionDataSource dataSource;
    auto results = computeCompletions("Option.", 7, dataSource);
    CHECK(hasCandidate(results, "Option.map"));
    CHECK(hasCandidate(results, "Option.bind"));
    CHECK(hasCandidate(results, "Option.defaultValue"));
}

TEST_CASE("Completer.dollar_returns_additional_candidates", "[completion][completer]")
{
    CompletionDataSource dataSource;
    dataSource.additionalCandidates = {
        { "HOME", "HOME", "/home/user", "", CompletionKind::Other },
        { "PATH", "PATH", "/usr/bin", "", CompletionKind::Other },
    };
    auto results = computeCompletions("$", 1, dataSource);
    CHECK(hasCandidate(results, "HOME"));
    CHECK(hasCandidate(results, "PATH"));
}

TEST_CASE("Completer.symbols_appear_in_command_position", "[completion][completer]")
{
    CompletionDataSource dataSource;
    dataSource.symbols = { {
        .name = "myFunc",
        .isFunction = true,
        .parameterNames = { "x" },
    } };
    auto results = computeCompletions("my", 2, dataSource);
    CHECK(hasCandidate(results, "myFunc"));
}

TEST_CASE("Completer.symbols_appear_in_argument_position", "[completion][completer]")
{
    CompletionDataSource dataSource;
    dataSource.symbols = { {
        .name = "myVal",
        .isFunction = false,
    } };
    auto results = computeCompletions("echo my", 7, dataSource);
    CHECK(hasCandidate(results, "myVal"));
}

TEST_CASE("Completer.constructors_in_argument_position", "[completion][completer]")
{
    CompletionDataSource dataSource;
    auto results = computeCompletions("echo So", 7, dataSource);
    CHECK(hasCandidate(results, "Some"));
}

TEST_CASE("Completer.filepath_context_uses_additional", "[completion][completer]")
{
    CompletionDataSource dataSource;
    dataSource.additionalCandidates = {
        { "./src", "./src", "directory", "", CompletionKind::Other },
    };
    auto results = computeCompletions("./s", 3, dataSource);
    CHECK(hasCandidate(results, "./src"));
}

TEST_CASE("Completer.prefix_filtering_works", "[completion][completer]")
{
    CompletionDataSource dataSource;
    auto results = computeCompletions("mat", 3, dataSource);
    CHECK(hasCandidate(results, "match"));
    CHECK(!hasCandidate(results, "let"));
    CHECK(!hasCandidate(results, "fun"));
}

TEST_CASE("Completer.dot_access_with_record_fields", "[completion][completer]")
{
    CompletionDataSource dataSource;
    dataSource.recordFields["ProcessInfo"] = { { "pid", "int" }, { "user", "str" }, { "cpu", "float" } };
    auto results = computeCompletions("_.p", 3, dataSource);
    CHECK(hasCandidate(results, "_.pid"));
    CHECK(!hasCandidate(results, "_.user"));
}

TEST_CASE("Completer.record_field_completion", "[completion][completer]")
{
    CompletionDataSource dataSource;
    dataSource.recordFields["Person"] = { { "name", "str" }, { "age", "int" } };
    auto results = computeCompletions("_.", 2, dataSource);
    CHECK(hasCandidate(results, "_.name"));
    CHECK(hasCandidate(results, "_.age"));
}

TEST_CASE("Completer.variable_specific_completion", "[completion][completer]")
{
    CompletionDataSource dataSource;
    dataSource.recordFields["Person"] = { { "name", "str" }, { "age", "int" } };
    dataSource.recordFields["ProcessInfo"] = { { "pid", "int" } };
    dataSource.variableTypes["alice"] = "Person";
    auto results = computeCompletions("alice.", 6, dataSource);
    CHECK(hasCandidate(results, "alice.name"));
    CHECK(hasCandidate(results, "alice.age"));
    CHECK(!hasCandidate(results, "alice.pid"));
    CHECK(!hasCandidate(results, "alice.map")); // No Option methods for known type
}

// =============================================================================
// collectRecordInfo tests
// =============================================================================

TEST_CASE("Completer.collectRecordInfo.extracts_record_fields", "[completion][completer]")
{
    auto info = collectRecordInfo("type Person = { name: str; age: int }");
    REQUIRE(info.recordFields.count("Person") == 1);
    auto const& fields = info.recordFields.at("Person");
    REQUIRE(fields.size() == 2);
    CHECK(fields[0].name == "name");
    CHECK(fields[0].typeName == "str");
    CHECK(fields[1].name == "age");
    CHECK(fields[1].typeName == "int");
}

TEST_CASE("Completer.collectRecordInfo.extracts_variable_types", "[completion][completer]")
{
    auto info = collectRecordInfo(
        "type Person = { name: str; age: int }\nlet alice = { name = \"Alice\"; age = 30 }");
    CHECK(info.variableTypes.count("alice") == 1);
    CHECK(info.variableTypes.at("alice") == "Person");
}

TEST_CASE("Completer.collectRecordInfo.no_type_for_anonymous_record", "[completion][completer]")
{
    auto info = collectRecordInfo("let r = { x = 1 }");
    CHECK(info.variableTypes.count("r") == 0);
}

// =============================================================================
// Builtin argument completion integration tests
// =============================================================================

TEST_CASE("Completer.shell_prompt_preset_offers_presets", "[completion][completer]")
{
    CompletionDataSource dataSource;
    auto results = computeCompletions("shell_prompt_preset ", 20, dataSource);
    CHECK(hasCandidate(results, "minimal-arrow"));
    CHECK(hasCandidate(results, "powerline"));
    CHECK(hasCandidate(results, "endo-signature"));
}

TEST_CASE("Completer.shell_prompt_preset_filters_by_prefix", "[completion][completer]")
{
    CompletionDataSource dataSource;
    auto results = computeCompletions("shell_prompt_preset pow", 23, dataSource);
    CHECK(hasCandidate(results, "powerline"));
    CHECK(!hasCandidate(results, "minimal-arrow"));
}

TEST_CASE("Completer.shell_prompt_layout_offers_values", "[completion][completer]")
{
    CompletionDataSource dataSource;
    auto results = computeCompletions("shell_prompt_layout ", 20, dataSource);
    CHECK(hasCandidate(results, "single-line"));
    CHECK(hasCandidate(results, "two-line"));
    CHECK(hasCandidate(results, "boxed"));
    CHECK(hasCandidate(results, "powerline"));
}

TEST_CASE("Completer.shell_prompt_prefix_offers_builtins", "[completion][completer]")
{
    CompletionDataSource dataSource;
    auto results = computeCompletions("shell_prompt_", 13, dataSource);
    CHECK(hasCandidate(results, "shell_prompt_preset"));
    CHECK(hasCandidate(results, "shell_prompt_layout"));
    CHECK(hasCandidate(results, "shell_prompt_separator"));
    CHECK(hasCandidate(results, "shell_prompt_transient"));
    CHECK(hasCandidate(results, "shell_prompt_indicator"));
    CHECK(hasCandidate(results, "shell_prompt_duration_threshold"));
}

TEST_CASE("Completer.shell_prompt_indicator_no_candidates", "[completion][completer]")
{
    CompletionDataSource dataSource;
    auto results = computeCompletions("shell_prompt_indicator ", 23, dataSource);
    // Free-form string argument: no enum values, no constructors, no symbols
    CHECK(results.empty());
}

TEST_CASE("Completer.shell_prompt_preset_no_constructors", "[completion][completer]")
{
    CompletionDataSource dataSource;
    auto results = computeCompletions("shell_prompt_preset ", 20, dataSource);
    // Should have enum values but NOT constructors
    CHECK(!hasCandidate(results, "Some"));
    CHECK(!hasCandidate(results, "None"));
    CHECK(!hasCandidate(results, "Ok"));
    CHECK(!hasCandidate(results, "Error"));
    // Should still have preset enum values
    CHECK(hasCandidate(results, "minimal-arrow"));
}

TEST_CASE("Completer.non_builtin_argument_still_has_constructors", "[completion][completer]")
{
    CompletionDataSource dataSource;
    auto results = computeCompletions("echo ", 5, dataSource);
    // Non-builtin command should still show constructors
    CHECK(hasCandidate(results, "Some"));
    CHECK(hasCandidate(results, "None"));
    CHECK(hasCandidate(results, "Ok"));
    CHECK(hasCandidate(results, "Error"));
}

// =============================================================================
// Quoted-string completion tests (context analyzer with unterminated quotes)
// =============================================================================

TEST_CASE("CompletionContext.quoted_argument_detects_command", "[completion][context]")
{
    // Cursor after opening quote: shell_prompt_preset "
    auto ctx = CompletionContextAnalyzer::analyze("shell_prompt_preset \"", 21);
    CHECK(ctx.type == CompletionContextType::Argument);
    CHECK(ctx.command.has_value());
    CHECK(*ctx.command == "shell_prompt_preset");
}

TEST_CASE("CompletionContext.quoted_argument_with_partial_text", "[completion][context]")
{
    // Cursor after partial text inside quote: shell_prompt_preset "pow
    auto ctx = CompletionContextAnalyzer::analyze("shell_prompt_preset \"pow", 24);
    CHECK(ctx.type == CompletionContextType::Argument);
    CHECK(ctx.prefix == "pow");
    CHECK(ctx.command.has_value());
    CHECK(*ctx.command == "shell_prompt_preset");
}

TEST_CASE("CompletionContext.single_quoted_argument_detects_command", "[completion][context]")
{
    // Single-quoted variant: shell_prompt_layout '
    auto ctx = CompletionContextAnalyzer::analyze("shell_prompt_layout '", 21);
    CHECK(ctx.type == CompletionContextType::Argument);
    CHECK(ctx.command.has_value());
    CHECK(*ctx.command == "shell_prompt_layout");
}

TEST_CASE("Completer.quoted_shell_prompt_preset_offers_all_presets", "[completion][completer]")
{
    CompletionDataSource dataSource;
    // Cursor after opening quote — empty prefix, all presets
    auto results = computeCompletions("shell_prompt_preset \"", 21, dataSource);
    CHECK(hasCandidate(results, "minimal-arrow"));
    CHECK(hasCandidate(results, "powerline"));
    CHECK(hasCandidate(results, "endo-signature"));
}

TEST_CASE("Completer.quoted_shell_prompt_preset_filters_by_prefix", "[completion][completer]")
{
    CompletionDataSource dataSource;
    // Cursor after partial text inside quote
    auto results = computeCompletions("shell_prompt_preset \"pow", 24, dataSource);
    CHECK(hasCandidate(results, "powerline"));
    CHECK(!hasCandidate(results, "minimal-arrow"));
}

TEST_CASE("Completer.quoted_shell_prompt_layout_offers_values", "[completion][completer]")
{
    CompletionDataSource dataSource;
    auto results = computeCompletions("shell_prompt_layout \"", 21, dataSource);
    CHECK(hasCandidate(results, "single-line"));
    CHECK(hasCandidate(results, "two-line"));
    CHECK(hasCandidate(results, "boxed"));
}

// =============================================================================
// Standard library completion integration tests
// =============================================================================

TEST_CASE("Completer.stdlib.ma_prefix_returns_map_and_match", "[completion][completer][stdlib]")
{
    CompletionDataSource dataSource;
    auto results = computeCompletions("ma", 2, dataSource);
    CHECK(hasCandidate(results, "map"));
    CHECK(hasCandidate(results, "match"));
}

TEST_CASE("Completer.stdlib.fil_prefix_returns_filter_not_map", "[completion][completer][stdlib]")
{
    CompletionDataSource dataSource;
    auto results = computeCompletions("fil", 3, dataSource);
    CHECK(hasCandidate(results, "filter"));
    CHECK(!hasCandidate(results, "map"));
}

TEST_CASE("Completer.stdlib.user_symbol_deduplicates_with_stdlib", "[completion][completer][stdlib]")
{
    CompletionDataSource dataSource;
    dataSource.symbols = { {
        .name = "map",
        .isFunction = true,
        .parameterNames = { "f", "xs" },
    } };
    auto results = computeCompletions("ma", 2, dataSource);
    // User-defined "map" should appear, stdlib "map" should be deduplicated away
    auto mapCount = std::ranges::count_if(results, [](auto const& c) { return c.text == "map"; });
    CHECK(mapCount == 1);
}

TEST_CASE("Completer.stdlib.shell_prompt_preset_does_not_show_stdlib", "[completion][completer][stdlib]")
{
    CompletionDataSource dataSource;
    auto results = computeCompletions("shell_prompt_preset ", 20, dataSource);
    CHECK(!hasCandidate(results, "map"));
    CHECK(!hasCandidate(results, "filter"));
    CHECK(!hasCandidate(results, "fold"));
}

TEST_CASE("Completer.stdlib.argument_position_returns_stdlib", "[completion][completer][stdlib]")
{
    CompletionDataSource dataSource;
    auto results = computeCompletions("echo len", 8, dataSource);
    CHECK(hasCandidate(results, "length"));
}

TEST_CASE("Completer.stdlib.empty_input_includes_stdlib", "[completion][completer][stdlib]")
{
    CompletionDataSource dataSource;
    auto results = computeCompletions("", 0, dataSource);
    CHECK(hasCandidate(results, "map"));
    CHECK(hasCandidate(results, "filter"));
    CHECK(hasCandidate(results, "fold"));
    CHECK(hasCandidate(results, "head"));
    CHECK(hasCandidate(results, "trim"));
}

TEST_CASE("Completer.shell_is_interactive_prefix_completion", "[completion][completer]")
{
    CompletionDataSource dataSource;
    auto results = computeCompletions("shell_is", 8, dataSource);
    CHECK(hasCandidate(results, "shell_is_interactive"));
}

// =============================================================================
// Left-arrow (<-) assignment value completion tests
// =============================================================================

TEST_CASE("Completer.left_arrow_shell_prompt_preset_offers_presets", "[completion][completer]")
{
    CompletionDataSource dataSource;
    // "shell_prompt_preset <- " = 23 chars
    auto results = computeCompletions("shell_prompt_preset <- ", 23, dataSource);
    CHECK(hasCandidate(results, "minimal-arrow"));
    CHECK(hasCandidate(results, "powerline"));
    CHECK(hasCandidate(results, "endo-signature"));
}

TEST_CASE("Completer.left_arrow_shell_prompt_preset_filters_by_prefix", "[completion][completer]")
{
    CompletionDataSource dataSource;
    // "shell_prompt_preset <- pow" = 26 chars
    auto results = computeCompletions("shell_prompt_preset <- pow", 26, dataSource);
    CHECK(hasCandidate(results, "powerline"));
    CHECK(!hasCandidate(results, "minimal-arrow"));
}

TEST_CASE("Completer.left_arrow_shell_prompt_preset_no_constructors", "[completion][completer]")
{
    CompletionDataSource dataSource;
    // "shell_prompt_preset <- " = 23 chars
    auto results = computeCompletions("shell_prompt_preset <- ", 23, dataSource);
    CHECK(!hasCandidate(results, "Some"));
    CHECK(!hasCandidate(results, "None"));
    CHECK(!hasCandidate(results, "Ok"));
    CHECK(!hasCandidate(results, "Error"));
}

TEST_CASE("Completer.left_arrow_agent_claude_model_offers_models", "[completion][completer]")
{
    CompletionDataSource dataSource;
    // "agent_claude_model <- " = 22 chars
    auto results = computeCompletions("agent_claude_model <- ", 22, dataSource);
    CHECK(hasCandidate(results, "claude-opus-4-6"));
    CHECK(hasCandidate(results, "claude-sonnet-4-6"));
}

TEST_CASE("Completer.left_arrow_agent_trace_enabled_offers_booleans", "[completion][completer]")
{
    CompletionDataSource dataSource;
    // "agent_trace_enabled <- " = 23 chars
    auto results = computeCompletions("agent_trace_enabled <- ", 23, dataSource);
    CHECK(hasCandidate(results, "true"));
    CHECK(hasCandidate(results, "false"));
}

TEST_CASE("Completer.left_arrow_shell_prompt_layout_offers_layouts", "[completion][completer]")
{
    CompletionDataSource dataSource;
    // "shell_prompt_layout <- " = 23 chars
    auto results = computeCompletions("shell_prompt_layout <- ", 23, dataSource);
    CHECK(hasCandidate(results, "single-line"));
    CHECK(hasCandidate(results, "two-line"));
    CHECK(hasCandidate(results, "boxed"));
    CHECK(hasCandidate(results, "powerline"));
}

TEST_CASE("Completer.left_arrow_quoted_shell_prompt_preset_filters", "[completion][completer]")
{
    CompletionDataSource dataSource;
    // "shell_prompt_preset <- \"pow" = 27 chars in memory
    auto results = computeCompletions("shell_prompt_preset <- \"pow", 27, dataSource);
    CHECK(hasCandidate(results, "powerline"));
    CHECK(!hasCandidate(results, "minimal-arrow"));
}

TEST_CASE("Completer.left_arrow_agent_claude_thinking_mode_offers_modes", "[completion][completer]")
{
    CompletionDataSource dataSource;
    // "agent_claude_thinking_mode <- " = 30 chars
    auto results = computeCompletions("agent_claude_thinking_mode <- ", 30, dataSource);
    CHECK(hasCandidate(results, "off"));
    CHECK(hasCandidate(results, "normal"));
    CHECK(hasCandidate(results, "extended"));
}

TEST_CASE("Completer.left_arrow_agent_claude_auth_type_offers_auth_types", "[completion][completer]")
{
    CompletionDataSource dataSource;
    // "agent_claude_auth_type <- " = 26 chars
    auto results = computeCompletions("agent_claude_auth_type <- ", 26, dataSource);
    CHECK(hasCandidate(results, "auto"));
    CHECK(hasCandidate(results, "oauth"));
    CHECK(hasCandidate(results, "api_key"));
}

TEST_CASE("Completer.left_arrow_multiline_agent_model_offers_models", "[completion][completer]")
{
    CompletionDataSource dataSource;
    // Multi-line: previous lines must not interfere with property resolution
    auto results = computeCompletions("let x = 5\nagent_claude_model <- ", 32, dataSource);
    CHECK(hasCandidate(results, "claude-opus-4-6"));
    CHECK(hasCandidate(results, "claude-sonnet-4-6"));
}
