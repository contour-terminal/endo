// SPDX-License-Identifier: Apache-2.0
#include <format>
#include <string>

#include <agent/SlashCommandRegistry.hpp>
#include <agent/SlashCommands.hpp>

namespace endo::agent
{

namespace
{

    /// @brief Built-in /help command — lists all registered commands with descriptions.
    class HelpCommand final: public SlashCommand
    {
      public:
        explicit HelpCommand(SlashCommandRegistry const& registry): _registry(registry) {}

        [[nodiscard]] std::string_view name() const override { return "help"; }

        [[nodiscard]] std::string_view description() const override { return "List available commands"; }

        [[nodiscard]] SlashCommandResult execute(std::string_view /*arguments*/) const override
        {
            auto text = std::string { "Available commands:\n" };
            for (auto const& cmd: _registry.commands())
                text += std::format("  /{:<12} {}\n", cmd->name(), cmd->description());
            return DirectOutput { .text = std::move(text) };
        }

      private:
        SlashCommandRegistry const& _registry;
    };

    /// @brief Built-in /plan command — enters plan mode with the given query.
    class PlanCommand final: public SlashCommand
    {
      public:
        [[nodiscard]] std::string_view name() const override { return "plan"; }

        [[nodiscard]] std::string_view description() const override
        {
            return "Enter plan mode (Shift+Tab to cycle)";
        }

        [[nodiscard]] SlashCommandResult execute(std::string_view arguments) const override
        {
            return PlanModeRequest { .query = std::string(arguments) };
        }
    };

} // namespace

void registerBuiltinSlashCommands(SlashCommandRegistry& registry)
{
    registry.registerCommand(std::make_unique<HelpCommand>(registry));
    registry.registerCommand(std::make_unique<PlanCommand>());
}

// --- CallbackSlashCommand ---

CallbackSlashCommand::CallbackSlashCommand(std::string name,
                                           std::string description,
                                           std::function<SlashCommandResult(std::string_view)> handler):
    _name(std::move(name)), _description(std::move(description)), _handler(std::move(handler))
{
}

std::string_view CallbackSlashCommand::name() const
{
    return _name;
}

std::string_view CallbackSlashCommand::description() const
{
    return _description;
}

SlashCommandResult CallbackSlashCommand::execute(std::string_view arguments) const
{
    return _handler(arguments);
}

} // namespace endo::agent
