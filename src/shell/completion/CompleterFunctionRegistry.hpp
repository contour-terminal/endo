// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace endo
{

/// @brief Registry mapping command names to scripted completer function names.
///
/// Populated by `register_completer` calls in `.endo` completer scripts.
/// Used by ScriptedCompleter to dispatch tab-press completions.
class CompleterFunctionRegistry
{
  public:
    /// @brief Registers a completer function for a command.
    /// @param command The command name (e.g., "flatpak").
    /// @param functionName The endo function name to invoke (e.g., "flatpak_complete").
    void registerFunction(std::string command, std::string functionName)
    {
        _functions[std::move(command)] = std::move(functionName);
    }

    /// @brief Looks up the completer function for a command.
    /// @param command The command name.
    /// @return The function name, or nullopt if not registered.
    [[nodiscard]] std::optional<std::string> functionForCommand(std::string const& command) const
    {
        if (auto const it = _functions.find(command); it != _functions.end())
            return it->second;
        return std::nullopt;
    }

    /// @brief Returns all registered command names.
    [[nodiscard]] std::vector<std::string> commands() const
    {
        std::vector<std::string> result;
        result.reserve(_functions.size());
        for (auto const& [cmd, _]: _functions)
            result.push_back(cmd);
        return result;
    }

    /// @brief Returns true if the given command has a registered completer.
    [[nodiscard]] bool hasCommand(std::string const& command) const { return _functions.contains(command); }

  private:
    std::unordered_map<std::string, std::string> _functions;
};

} // namespace endo
