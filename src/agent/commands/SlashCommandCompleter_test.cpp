// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <agent/commands/SlashCommandCompleter.hpp>
#include <agent/commands/SlashCommandRegistry.hpp>
#include <agent/commands/SlashCommands.hpp>

using namespace endo::agent;

namespace
{

/// Helper to create a registry with built-in commands for testing.
SlashCommandRegistry createTestRegistry()
{
    auto registry = SlashCommandRegistry {};
    registerBuiltinSlashCommands(registry);
    return registry;
}

} // namespace

TEST_CASE("SlashCommandCompleter.slash_returns_all_commands", "[agent][slash][completer]")
{
    auto registry = createTestRegistry();
    auto completer = SlashCommandCompleter(registry);

    auto const items = completer.complete("/", 1);
    REQUIRE(items.size() == 2);

    // Both /help and /plan should appear
    auto hasHelp = false;
    auto hasPlan = false;
    for (auto const& item: items)
    {
        if (item.text == "/help")
            hasHelp = true;
        if (item.text == "/plan")
            hasPlan = true;
    }
    CHECK(hasHelp);
    CHECK(hasPlan);
}

TEST_CASE("SlashCommandCompleter.prefix_match_pl", "[agent][slash][completer]")
{
    auto registry = createTestRegistry();
    auto completer = SlashCommandCompleter(registry);

    auto const items = completer.complete("/pl", 3);
    REQUIRE(items.size() == 1);
    CHECK(items[0].text == "/plan");
}

TEST_CASE("SlashCommandCompleter.prefix_match_he", "[agent][slash][completer]")
{
    auto registry = createTestRegistry();
    auto completer = SlashCommandCompleter(registry);

    auto const items = completer.complete("/he", 3);
    REQUIRE(items.size() == 1);
    CHECK(items[0].text == "/help");
}

TEST_CASE("SlashCommandCompleter.fuzzy_match_pln", "[agent][slash][completer]")
{
    auto registry = createTestRegistry();
    auto completer = SlashCommandCompleter(registry);

    // "pln" should fuzzy-match "plan" (p-l-n skipping 'a')
    auto const items = completer.complete("/pln", 4);
    REQUIRE(items.size() == 1);
    CHECK(items[0].text == "/plan");
}

TEST_CASE("SlashCommandCompleter.non_slash_input_returns_empty", "[agent][slash][completer]")
{
    auto registry = createTestRegistry();
    auto completer = SlashCommandCompleter(registry);

    CHECK(completer.complete("hello", 5).empty());
    CHECK(completer.complete("", 0).empty());
    CHECK(completer.complete("plan", 4).empty());
}

TEST_CASE("SlashCommandCompleter.cursor_past_space_returns_empty", "[agent][slash][completer]")
{
    auto registry = createTestRegistry();
    auto completer = SlashCommandCompleter(registry);

    // User is typing arguments, not the command name
    CHECK(completer.complete("/plan something", 15).empty());
    CHECK(completer.complete("/plan ", 6).empty());
}

TEST_CASE("SlashCommandCompleter.descriptions_propagate", "[agent][slash][completer]")
{
    auto registry = createTestRegistry();
    auto completer = SlashCommandCompleter(registry);

    auto const items = completer.complete("/pl", 3);
    REQUIRE(items.size() == 1);
    CHECK(!items[0].description.empty());
    CHECK(items[0].description == "Enter plan mode (Shift+Tab to cycle)");
}

TEST_CASE("SlashCommandCompleter.dynamically_registered_commands_appear", "[agent][slash][completer]")
{
    auto registry = createTestRegistry();

    // Register a dynamic command
    registry.registerCommand(std::make_unique<CallbackSlashCommand>(
        "commit", "Generate a git commit", [](std::string_view) -> SlashCommandResult {
            return DirectOutput { .text = "commit" };
        }));

    auto completer = SlashCommandCompleter(registry);

    auto const items = completer.complete("/co", 3);
    REQUIRE(items.size() == 1);
    CHECK(items[0].text == "/commit");
    CHECK(items[0].description == "Generate a git commit");
}

TEST_CASE("SlashCommandCompleter.completion_text_includes_leading_slash", "[agent][slash][completer]")
{
    auto registry = createTestRegistry();
    auto completer = SlashCommandCompleter(registry);

    auto const items = completer.complete("/", 1);
    for (auto const& item: items)
    {
        CHECK(item.text.starts_with("/"));
    }
}

TEST_CASE("SlashCommandCompleter.no_match_returns_empty", "[agent][slash][completer]")
{
    auto registry = createTestRegistry();
    auto completer = SlashCommandCompleter(registry);

    CHECK(completer.complete("/xyz", 4).empty());
    CHECK(completer.complete("/zzz", 4).empty());
}

TEST_CASE("SlashCommandCompleter.priority_is_100", "[agent][slash][completer]")
{
    auto registry = createTestRegistry();
    auto completer = SlashCommandCompleter(registry);
    CHECK(completer.priority() == 100);
}

TEST_CASE("SlashCommandCompleter.model_argument_completion_with_prefix", "[agent][slash][completer]")
{
    auto registry = createTestRegistry();
    registry.registerCommand(std::make_unique<CallbackSlashCommand>(
        "model", "Switch model", [](std::string_view) -> SlashCommandResult {
            return DirectOutput { .text = "ok" };
        }));
    auto completer = SlashCommandCompleter(registry);

    // "son" should match "claude-sonnet-4-6" (and possibly others with "sonnet")
    auto const items = completer.complete("/model son", 10);
    CHECK(!items.empty());
    // All items should start with "/model "
    for (auto const& item: items)
    {
        CHECK(item.text.starts_with("/model "));
        CHECK(!item.description.empty()); // should have provider name
    }
}

TEST_CASE("SlashCommandCompleter.model_argument_completion_all_models", "[agent][slash][completer]")
{
    auto registry = createTestRegistry();
    registry.registerCommand(std::make_unique<CallbackSlashCommand>(
        "model", "Switch model", [](std::string_view) -> SlashCommandResult {
            return DirectOutput { .text = "ok" };
        }));
    auto completer = SlashCommandCompleter(registry);

    // Empty prefix after space should list all models.
    auto const items = completer.complete("/model ", 7);
    CHECK(items.size() == 12); // 5 Claude + 4 OpenAI + 3 Gemini
}
