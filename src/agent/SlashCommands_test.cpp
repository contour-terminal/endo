// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <agent/SlashCommand.hpp>
#include <agent/SlashCommandRegistry.hpp>
#include <agent/SlashCommands.hpp>

using namespace endo::agent;

TEST_CASE("SlashCommands.registerBuiltinSlashCommands_registers_two", "[agent][slash]")
{
    auto registry = SlashCommandRegistry {};
    registerBuiltinSlashCommands(registry);

    CHECK(registry.commands().size() == 2);
    CHECK(registry.findCommand("help") != nullptr);
    CHECK(registry.findCommand("plan") != nullptr);
}

TEST_CASE("SlashCommands.plan_returns_PlanModeRequest", "[agent][slash]")
{
    auto registry = SlashCommandRegistry {};
    registerBuiltinSlashCommands(registry);

    auto const* cmd = registry.findCommand("plan");
    REQUIRE(cmd != nullptr);

    auto const result = cmd->execute("refactor auth module");
    auto const* planReq = std::get_if<PlanModeRequest>(&result);
    REQUIRE(planReq != nullptr);
    CHECK(planReq->query == "refactor auth module");
}

TEST_CASE("SlashCommands.plan_with_empty_args", "[agent][slash]")
{
    auto registry = SlashCommandRegistry {};
    registerBuiltinSlashCommands(registry);

    auto const* cmd = registry.findCommand("plan");
    REQUIRE(cmd != nullptr);

    auto const result = cmd->execute("");
    auto const* planReq = std::get_if<PlanModeRequest>(&result);
    REQUIRE(planReq != nullptr);
    CHECK(planReq->query.empty());
}

TEST_CASE("SlashCommands.help_returns_DirectOutput", "[agent][slash]")
{
    auto registry = SlashCommandRegistry {};
    registerBuiltinSlashCommands(registry);

    auto const* cmd = registry.findCommand("help");
    REQUIRE(cmd != nullptr);

    auto const result = cmd->execute("");
    auto const* output = std::get_if<DirectOutput>(&result);
    REQUIRE(output != nullptr);
    CHECK(output->text.find("/help") != std::string::npos);
    CHECK(output->text.find("/plan") != std::string::npos);
}

TEST_CASE("SlashCommands.help_includes_dynamically_registered_commands", "[agent][slash]")
{
    auto registry = SlashCommandRegistry {};
    registerBuiltinSlashCommands(registry);

    // Dynamically register a new command
    registry.registerCommand(std::make_unique<CallbackSlashCommand>(
        "commit", "Generate a git commit", [](std::string_view) -> SlashCommandResult {
            return PromptRewrite { .prompt = "Review staged changes" };
        }));

    auto const* cmd = registry.findCommand("help");
    REQUIRE(cmd != nullptr);

    auto const result = cmd->execute("");
    auto const* output = std::get_if<DirectOutput>(&result);
    REQUIRE(output != nullptr);
    CHECK(output->text.find("/commit") != std::string::npos);
    CHECK(output->text.find("Generate a git commit") != std::string::npos);
}

TEST_CASE("SlashCommands.CallbackSlashCommand_works", "[agent][slash]")
{
    auto cmd =
        CallbackSlashCommand("test", "A test command", [](std::string_view args) -> SlashCommandResult {
            return DirectOutput { .text = "got:" + std::string(args) };
        });

    CHECK(cmd.name() == "test");
    CHECK(cmd.description() == "A test command");

    auto const result = cmd.execute("hello world");
    auto const* output = std::get_if<DirectOutput>(&result);
    REQUIRE(output != nullptr);
    CHECK(output->text == "got:hello world");
}

TEST_CASE("SlashCommands.CallbackSlashCommand_PromptRewrite", "[agent][slash]")
{
    auto cmd =
        CallbackSlashCommand("rewrite", "Rewrite prompt", [](std::string_view args) -> SlashCommandResult {
            return PromptRewrite { .prompt = "rewritten: " + std::string(args) };
        });

    auto const result = cmd.execute("foo");
    auto const* rewrite = std::get_if<PromptRewrite>(&result);
    REQUIRE(rewrite != nullptr);
    CHECK(rewrite->prompt == "rewritten: foo");
}
