// SPDX-License-Identifier: Apache-2.0
#include "BuiltinArgumentCompleter.hpp"
#include <shell/completion/CompletionAdapter.hpp>

#include <endo-language/ide/CompletionCandidates.hpp>

#include <tui/KeyBindings.hpp>

#include <array>
#include <string_view>

namespace endo
{

namespace
{
    /// Counts the number of whitespace-separated tokens after "bind" in the input.
    /// Returns 0 if cursor is still on "bind" itself.
    size_t bindArgIndex(std::string_view fullInput, size_t cursorPosition)
    {
        auto const input = fullInput.substr(0, cursorPosition);

        // Skip leading whitespace
        auto pos = input.find_first_not_of(' ');
        if (pos == std::string_view::npos)
            return 0;

        // Skip the command name ("bind")
        auto end = input.find(' ', pos);
        if (end == std::string_view::npos)
            return 0;

        // Count completed tokens after the command
        size_t count = 0;
        pos = end;
        while (pos < input.size())
        {
            pos = input.find_first_not_of(' ', pos);
            if (pos == std::string_view::npos)
                break;
            end = input.find(' ', pos);
            if (end == std::string_view::npos)
                return count; // cursor is on this (incomplete) token
            ++count;
            pos = end;
        }
        return count;
    }

    /// Returns the first argument after "bind" (e.g., "-r", "ctrl+c").
    std::string_view bindFirstArg(std::string_view fullInput, size_t cursorPosition)
    {
        auto const input = fullInput.substr(0, cursorPosition);

        // Skip "bind"
        auto pos = input.find_first_not_of(' ');
        if (pos == std::string_view::npos)
            return {};
        auto end = input.find(' ', pos);
        if (end == std::string_view::npos)
            return {};

        // Find first arg
        pos = input.find_first_not_of(' ', end);
        if (pos == std::string_view::npos)
            return {};
        end = input.find(' ', pos);
        if (end == std::string_view::npos)
            end = input.size();
        return input.substr(pos, end - pos);
    }

    std::vector<CompletionCandidate> bindKeyCandidates(std::string_view prefix)
    {
        std::vector<CompletionCandidate> results;
        for (auto const name: tui::allKeyNames())
        {
            if (name.starts_with(prefix))
                results.push_back(CompletionCandidate {
                    .text = std::string(name),
                    .displayText = std::string(name),
                    .description = "key",
                    .kind = CompletionKind::EnumValue,
                });
        }
        // Also suggest modifier prefixes when prefix is empty or matches
        for (auto const* const mod: { "ctrl+", "alt+", "shift+", "super+" })
        {
            auto const sv = std::string_view(mod);
            if (sv.starts_with(prefix) && sv != prefix)
                results.push_back(CompletionCandidate {
                    .text = std::string(sv),
                    .displayText = std::string(sv),
                    .description = "modifier",
                    .kind = CompletionKind::EnumValue,
                });
        }
        return results;
    }

    std::vector<CompletionCandidate> bindActionCandidates(std::string_view prefix)
    {
        std::vector<CompletionCandidate> results;
        for (auto const& info: tui::allEditActionNames())
        {
            if (info.name.starts_with(prefix))
                results.push_back(CompletionCandidate {
                    .text = std::string(info.name),
                    .displayText = std::string(info.name),
                    .description = std::string(info.description),
                    .kind = CompletionKind::EnumValue,
                });
        }
        return results;
    }

    std::vector<CompletionCandidate> bindFlagCandidates(std::string_view prefix)
    {
        using Flag = std::pair<std::string_view, std::string_view>;
        static constexpr std::array<Flag, 7> flags = { {
            { "-r", "Remove a keybinding" },
            { "--remove", "Remove a keybinding" },
            { "-l", "List all keybindings" },
            { "--list", "List all keybindings" },
            { "--reset", "Reset to default keybindings" },
            { "-h", "Show help" },
            { "--help", "Show help" },
        } };

        std::vector<CompletionCandidate> results;
        for (auto const& [flag, desc]: flags)
        {
            if (flag.starts_with(prefix))
                results.push_back(CompletionCandidate {
                    .text = std::string(flag),
                    .displayText = std::string(flag),
                    .description = std::string(desc),
                    .kind = CompletionKind::EnumValue,
                });
        }
        return results;
    }

    std::vector<CompletionCandidate> completeBindArgs(CompletionContext const& context)
    {
        auto const argIdx = bindArgIndex(context.fullInput, context.cursorPosition);
        auto const& prefix = context.prefix;

        // First argument: flags or key chords
        if (argIdx == 0)
        {
            if (prefix.starts_with("-"))
                return bindFlagCandidates(prefix);

            auto results = bindFlagCandidates(prefix);
            auto keys = bindKeyCandidates(prefix);
            results.insert(results.end(), keys.begin(), keys.end());
            return results;
        }

        auto const firstArg = bindFirstArg(context.fullInput, context.cursorPosition);

        // After -r/--remove: complete key names
        if (firstArg == "-r" || firstArg == "--remove")
            return bindKeyCandidates(prefix);

        // Second argument (after a key chord): complete action names
        if (argIdx == 1)
            return bindActionCandidates(prefix);

        return {};
    }
} // namespace

std::vector<CompletionItem> BuiltinArgumentCompleter::complete(CompletionContext const& context)
{
    if (!context.command.has_value())
        return {};

    // Special handling for bind
    if (*context.command == "bind")
    {
        auto candidates = completeBindArgs(context);
        return applyFuzzyScoring(candidates, context.prefix, 80);
    }

    if (!isBuiltinWithArgumentCompletion(*context.command))
        return {};

    auto candidates = builtinArgumentCandidates(*context.command, context.prefix);
    return applyFuzzyScoring(candidates, context.prefix, 80);
}

bool BuiltinArgumentCompleter::canHandle(CompletionContextType type) const
{
    return type == CompletionContextType::Argument;
}

bool BuiltinArgumentCompleter::isExclusiveFor(CompletionContext const& context) const
{
    return context.command.has_value() && *context.command == "bind";
}

} // namespace endo
