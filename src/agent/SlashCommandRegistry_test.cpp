// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <agent/SlashCommand.hpp>
#include <agent/SlashCommandRegistry.hpp>

using namespace endo::agent;

namespace
{

/// Simple test command for registry tests.
class TestCommand final: public SlashCommand
{
  public:
    explicit TestCommand(std::string cmdName, std::string desc = "Test command"):
        _name(std::move(cmdName)), _description(std::move(desc))
    {
    }

    [[nodiscard]] std::string_view name() const override { return _name; }

    [[nodiscard]] std::string_view description() const override { return _description; }

    [[nodiscard]] SlashCommandResult execute(std::string_view arguments) const override
    {
        return DirectOutput { .text = "executed:" + std::string(arguments) };
    }

  private:
    std::string _name;
    std::string _description;
};

} // namespace

TEST_CASE("SlashCommandRegistry.register_and_find", "[agent][slash]")
{
    auto registry = SlashCommandRegistry {};
    registry.registerCommand(std::make_unique<TestCommand>("test"));

    auto const* cmd = registry.findCommand("test");
    REQUIRE(cmd != nullptr);
    CHECK(cmd->name() == "test");
}

TEST_CASE("SlashCommandRegistry.find_returns_nullptr_for_unknown", "[agent][slash]")
{
    auto registry = SlashCommandRegistry {};
    registry.registerCommand(std::make_unique<TestCommand>("test"));

    CHECK(registry.findCommand("unknown") == nullptr);
    CHECK(registry.findCommand("") == nullptr);
}

TEST_CASE("SlashCommandRegistry.commands_returns_all_in_order", "[agent][slash]")
{
    auto registry = SlashCommandRegistry {};
    registry.registerCommand(std::make_unique<TestCommand>("alpha"));
    registry.registerCommand(std::make_unique<TestCommand>("beta"));
    registry.registerCommand(std::make_unique<TestCommand>("gamma"));

    auto const cmds = registry.commands();
    REQUIRE(cmds.size() == 3);
    CHECK(cmds[0]->name() == "alpha");
    CHECK(cmds[1]->name() == "beta");
    CHECK(cmds[2]->name() == "gamma");
}

TEST_CASE("SlashCommandRegistry.empty_registry", "[agent][slash]")
{
    auto registry = SlashCommandRegistry {};

    CHECK(registry.findCommand("anything") == nullptr);
    CHECK(registry.commands().empty());
}

TEST_CASE("SlashCommandRegistry.find_returns_first_match", "[agent][slash]")
{
    auto registry = SlashCommandRegistry {};
    registry.registerCommand(std::make_unique<TestCommand>("dup", "first"));
    registry.registerCommand(std::make_unique<TestCommand>("dup", "second"));

    auto const* cmd = registry.findCommand("dup");
    REQUIRE(cmd != nullptr);
    CHECK(cmd->description() == "first");
}
