// SPDX-License-Identifier: Apache-2.0
#include <algorithm>

#include <agent/SlashCommandRegistry.hpp>

namespace endo::agent
{

void SlashCommandRegistry::registerCommand(std::unique_ptr<SlashCommand> command)
{
    _commands.push_back(std::move(command));
}

SlashCommand const* SlashCommandRegistry::findCommand(std::string_view name) const
{
    auto const it = std::ranges::find_if(_commands, [&](auto const& cmd) { return cmd->name() == name; });
    return it != _commands.end() ? it->get() : nullptr;
}

std::span<std::unique_ptr<SlashCommand> const> SlashCommandRegistry::commands() const
{
    return _commands;
}

} // namespace endo::agent
