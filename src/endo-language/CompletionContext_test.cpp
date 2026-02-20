// SPDX-License-Identifier: Apache-2.0
#include <endo-language/CompletionContext.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace endo;

// =============================================================================
// CompletionContextAnalyzer::analyze tests
// =============================================================================

TEST_CASE("CompletionContext.analyze.empty_input_is_command", "[completion][context]")
{
    auto ctx = CompletionContextAnalyzer::analyze("", 0);
    CHECK(ctx.type == CompletionContextType::Command);
    CHECK(ctx.prefix.empty());
}

TEST_CASE("CompletionContext.analyze.first_word_is_command", "[completion][context]")
{
    auto ctx = CompletionContextAnalyzer::analyze("gi", 2);
    CHECK(ctx.type == CompletionContextType::Command);
    CHECK(ctx.prefix == "gi");
}

TEST_CASE("CompletionContext.analyze.second_word_is_argument", "[completion][context]")
{
    auto ctx = CompletionContextAnalyzer::analyze("git reb", 7);
    CHECK(ctx.type == CompletionContextType::Argument);
    CHECK(ctx.prefix == "reb");
    CHECK(ctx.command == "git");
}

TEST_CASE("CompletionContext.analyze.slash_in_argument_is_argument_not_filepath", "[completion][context]")
{
    // This is the key fix: origin/m should be Argument, not FilePath
    auto ctx = CompletionContextAnalyzer::analyze("git rebase origin/m", 19);
    CHECK(ctx.type == CompletionContextType::Argument);
    CHECK(ctx.prefix == "origin/m");
    CHECK(ctx.command == "git");
}

TEST_CASE("CompletionContext.analyze.dot_slash_in_command_position_is_filepath", "[completion][context]")
{
    auto ctx = CompletionContextAnalyzer::analyze("./scr", 5);
    CHECK(ctx.type == CompletionContextType::FilePath);
    CHECK(ctx.prefix == "./scr");
}

TEST_CASE("CompletionContext.analyze.tilde_in_command_position_is_filepath", "[completion][context]")
{
    auto ctx = CompletionContextAnalyzer::analyze("~/bin/my", 8);
    CHECK(ctx.type == CompletionContextType::FilePath);
    CHECK(ctx.prefix == "~/bin/my");
}

TEST_CASE("CompletionContext.analyze.absolute_path_in_command_position_is_filepath", "[completion][context]")
{
    auto ctx = CompletionContextAnalyzer::analyze("/usr/bin/py", 11);
    CHECK(ctx.type == CompletionContextType::FilePath);
    CHECK(ctx.prefix == "/usr/bin/py");
}

TEST_CASE("CompletionContext.analyze.path_in_argument_is_argument", "[completion][context]")
{
    // File paths in argument position should be Argument (FileCompleter handles it)
    auto ctx = CompletionContextAnalyzer::analyze("cat some/path", 13);
    CHECK(ctx.type == CompletionContextType::Argument);
    CHECK(ctx.prefix == "some/path");
    CHECK(ctx.command == "cat");
}

TEST_CASE("CompletionContext.analyze.variable_context", "[completion][context]")
{
    auto ctx = CompletionContextAnalyzer::analyze("echo $HO", 8);
    CHECK(ctx.type == CompletionContextType::Variable);
    CHECK(ctx.prefix == "HO");
}

TEST_CASE("CompletionContext.analyze.option_context", "[completion][context]")
{
    auto ctx = CompletionContextAnalyzer::analyze("git commit --am", 15);
    CHECK(ctx.type == CompletionContextType::Option);
    CHECK(ctx.prefix == "--am");
    CHECK(ctx.command == "git");
}

TEST_CASE("CompletionContext.analyze.redirect_context", "[completion][context]")
{
    auto ctx = CompletionContextAnalyzer::analyze("echo hello > ou", 15);
    CHECK(ctx.type == CompletionContextType::Redirect);
    CHECK(ctx.prefix == "ou");
}

TEST_CASE("CompletionContext.analyze.after_pipe_is_command", "[completion][context]")
{
    auto ctx = CompletionContextAnalyzer::analyze("ls | gr", 7);
    CHECK(ctx.type == CompletionContextType::Command);
    CHECK(ctx.prefix == "gr");
}

TEST_CASE("CompletionContext.analyze.after_semicolon_is_command", "[completion][context]")
{
    auto ctx = CompletionContextAnalyzer::analyze("echo hi; gi", 11);
    CHECK(ctx.type == CompletionContextType::Command);
    CHECK(ctx.prefix == "gi");
}

TEST_CASE("CompletionContext.analyze.git_checkout_with_slash_is_argument", "[completion][context]")
{
    auto ctx = CompletionContextAnalyzer::analyze("git checkout feature/my", 23);
    CHECK(ctx.type == CompletionContextType::Argument);
    CHECK(ctx.prefix == "feature/my");
    CHECK(ctx.command == "git");
}

// =============================================================================
// Left-arrow (<-) assignment context analysis tests
// =============================================================================

TEST_CASE("CompletionContext.analyze.left_arrow_empty_value", "[completion][context]")
{
    // "shell_prompt_preset <- " = 23 chars, cursor past end
    auto ctx = CompletionContextAnalyzer::analyze("shell_prompt_preset <- ", 23);
    CHECK(ctx.type == CompletionContextType::Argument);
    CHECK(ctx.prefix.empty());
    CHECK(ctx.command.has_value());
    CHECK(*ctx.command == "shell_prompt_preset");
}

TEST_CASE("CompletionContext.analyze.left_arrow_partial_value", "[completion][context]")
{
    // "shell_prompt_preset <- pow" = 26 chars, cursor past end
    auto ctx = CompletionContextAnalyzer::analyze("shell_prompt_preset <- pow", 26);
    CHECK(ctx.type == CompletionContextType::Argument);
    CHECK(ctx.prefix == "pow");
    CHECK(ctx.command.has_value());
    CHECK(*ctx.command == "shell_prompt_preset");
}

TEST_CASE("CompletionContext.analyze.left_arrow_agent_model", "[completion][context]")
{
    // "agent_claude_model <- " = 22 chars, cursor past end
    auto ctx = CompletionContextAnalyzer::analyze("agent_claude_model <- ", 22);
    CHECK(ctx.type == CompletionContextType::Argument);
    CHECK(ctx.prefix.empty());
    CHECK(ctx.command.has_value());
    CHECK(*ctx.command == "agent_claude_model");
}

TEST_CASE("CompletionContext.analyze.left_arrow_multiline_resolves_correct_property", "[completion][context]")
{
    // Multi-line document: the property on the second line must be resolved, not the first identifier
    auto ctx = CompletionContextAnalyzer::analyze("let x = 5\nagent_claude_model <- ", 32);
    CHECK(ctx.type == CompletionContextType::Argument);
    CHECK(ctx.prefix.empty());
    CHECK(ctx.command.has_value());
    CHECK(*ctx.command == "agent_claude_model");
}
