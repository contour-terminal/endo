// SPDX-License-Identifier: Apache-2.0
#include <shell/Shell.hpp>
#include <shell/util/CommandResolver.hpp>

#include <crispy/utils.h>

#include <filesystem>
#include <format>
#include <print>

#include <platform/Types.hpp>

namespace endo
{

void Shell::builtinBind(CoreVM::Params& context)
{
    // Get arguments (may be empty if called without arguments)
    std::vector<std::string> args;
    if (context.count() >= 1)
    {
        auto const& argArray = context.getStringArray(1);
        for (size_t i = 0; i < argArray.size(); ++i)
            args.push_back(argArray[i]);
    }

    // No arguments: list all bindings
    if (args.empty())
    {
        auto const& bindings = prompt.keyBindings().bindings();
        for (auto const& [chord, action]: bindings)
        {
            std::println("{}\t{}", chord.toString(), tui::editActionToString(action));
        }
        _exitCode = 0;
        context.setResult(CoreVM::CoreNumber(0));
        return;
    }

    // Check for flags
    if (args[0] == "-r" || args[0] == "--remove")
    {
        // Remove binding: bind -r <key>
        if (args.size() < 2)
        {
            error("bind: -r requires a key argument");
            _exitCode = 1;
            context.setResult(CoreVM::CoreNumber(1));
            return;
        }

        auto const chord = tui::KeyChord::parse(args[1]);
        if (!chord)
        {
            error("bind: invalid key chord: {}", args[1]);
            _exitCode = 1;
            context.setResult(CoreVM::CoreNumber(1));
            return;
        }

        prompt.unbindKey(*chord);
        _exitCode = 0;
        context.setResult(CoreVM::CoreNumber(0));
        return;
    }

    if (args[0] == "--reset")
    {
        // Reset to defaults: bind --reset
        prompt.resetKeyBindings();
        _exitCode = 0;
        context.setResult(CoreVM::CoreNumber(0));
        return;
    }

    if (args[0] == "-l" || args[0] == "--list")
    {
        // List bindings (same as no arguments)
        auto const& bindings = prompt.keyBindings().bindings();
        for (auto const& [chord, action]: bindings)
        {
            std::println("{}\t{}", chord.toString(), tui::editActionToString(action));
        }
        _exitCode = 0;
        context.setResult(CoreVM::CoreNumber(0));
        return;
    }

    if (args[0] == "-h" || args[0] == "--help")
    {
        std::println("Usage: bind [options] [key action]");
        std::println("");
        std::println("Options:");
        std::println("  -l, --list    List all keybindings");
        std::println("  -r, --remove  Remove a keybinding: bind -r ctrl+y");
        std::println("  --reset       Reset all keybindings to defaults");
        std::println("  -h, --help    Show this help message");
        std::println("");
        std::println("Examples:");
        std::println("  bind                     # List all bindings");
        std::println("  bind ctrl+y redo         # Bind Ctrl+Y to redo");
        std::println("  bind ctrl+y yank         # Bind Ctrl+Y to yank (Emacs-style)");
        std::println("  bind -r ctrl+y           # Remove Ctrl+Y binding");
        std::println("  bind --reset             # Reset to defaults");
        std::println("");
        std::println("Key format: [modifier+]...key");
        std::println("  Modifiers: ctrl, alt, shift, super");
        std::println("  Keys: a-z, enter, backspace, delete, tab, escape,");
        std::println("        up, down, left, right, home, end, f1-f12");
        std::println("");
        std::println("Actions:");
        std::println("  Movement: move-forward-char, move-backward-char, move-forward-word,");
        std::println("            move-backward-word, move-to-line-start, move-to-line-end,");
        std::println("            move-to-buffer-start, move-to-buffer-end, move-up, move-down");
        std::println("  Editing:  delete-char-backward, delete-char-forward, delete-word,");
        std::println("            delete-word-backward, kill-to-end, kill-to-start, transpose");
        std::println("  Undo:     undo, redo");
        std::println("  Kill Ring: yank, yank-pop");
        std::println("  Selection: select-all");
        std::println("  Clipboard: cut, copy, paste");
        std::println("  Control:  submit, abort, insert-newline");
        std::println("  History:  history-prev, history-next");
        _exitCode = 0;
        context.setResult(CoreVM::CoreNumber(0));
        return;
    }

    // Set binding: bind <key> <action>
    if (args.size() < 2)
    {
        error("bind: requires key and action arguments");
        error("Usage: bind <key> <action>");
        error("Run 'bind --help' for more information.");
        _exitCode = 1;
        context.setResult(CoreVM::CoreNumber(1));
        return;
    }

    auto const chord = tui::KeyChord::parse(args[0]);
    if (!chord)
    {
        error("bind: invalid key chord: {}", args[0]);
        _exitCode = 1;
        context.setResult(CoreVM::CoreNumber(1));
        return;
    }

    auto const action = tui::parseEditAction(args[1]);
    if (!action)
    {
        error("bind: unknown action: {}", args[1]);
        error("Run 'bind --help' to see available actions.");
        _exitCode = 1;
        context.setResult(CoreVM::CoreNumber(1));
        return;
    }

    prompt.bindKey(*chord, *action);
    _exitCode = 0;
    context.setResult(CoreVM::CoreNumber(0));
}

void Shell::builtinWhich(CoreVM::Params& context)
{
    // Get arguments (may be empty if called without arguments)
    std::vector<std::string> args;
    if (context.count() >= 1)
    {
        auto const& argArray = context.getStringArray(1);
        for (size_t i = 0; i < argArray.size(); ++i)
            args.push_back(argArray[i]);
    }

    // Parse flags
    bool showAll = false;
    bool showHelp = false;
    bool readAlias = false;
    std::vector<std::string> programs;

    for (auto const& arg: args)
    {
        if (arg == "-h" || arg == "--help")
            showHelp = true;
        else if (arg == "-a" || arg == "--all")
            showAll = true;
        else if (arg == "-i" || arg == "--read-alias")
            readAlias = true;
        else if (arg.starts_with("-"))
        {
            error("which: invalid option: {}", arg);
            _exitCode = 1;
            context.setResult(CoreVM::CoreNumber(1));
            return;
        }
        else
            programs.push_back(arg);
    }

    // Helper to write output to the effective stdout (respects redirects and test environments)
    auto writeOutput = [this](std::string const& str) {
        NativeHandle const outputFd =
            _redirectState.getEffectiveStdoutFd(_currentPipelineBuilder.defaultStdoutFd, _processManager);
        [[maybe_unused]] auto written = platformWrite(outputFd, str.data(), str.size());
    };

    // Show help if requested or no arguments given
    if (showHelp || programs.empty())
    {
        std::string help = "Usage: which [OPTIONS] PROGRAM...\n"
                           "\n"
                           "Locate executables in the PATH.\n"
                           "\n"
                           "Options:\n"
                           "  -a, --all         Print all matching executables in PATH, not just the first\n"
                           "  -h, --help        Show this help message\n"
                           "  -i, --read-alias  Also show aliases (not yet implemented)\n"
                           "\n"
                           "Exit status:\n"
                           "  0  if all programs were found\n"
                           "  1  if one or more programs were not found\n";
        writeOutput(help);
        _exitCode = 0;
        context.setResult(CoreVM::CoreNumber(0));
        return;
    }

    // Warn about --read-alias since aliases aren't implemented yet
    if (readAlias)
    {
        error("which: --read-alias: aliases not yet implemented");
    }

    // Get PATH
    auto const pathEnv = _env.get("PATH");
    if (!pathEnv.has_value())
    {
        error("which: PATH not set");
        _exitCode = 1;
        context.setResult(CoreVM::CoreNumber(1));
        return;
    }

    auto const paths = crispy::split(pathEnv.value(), ':');
    bool allFound = true;

    // Search for each program
    for (auto const& program: programs)
    {
        bool found = false;

        // If program contains '/', treat as path
        if (program.contains('/'))
        {
            if (std::filesystem::exists(program))
            {
                writeOutput(program + "\n");
                found = true;
            }
        }
        else
        {
            // Search PATH
            for (auto const& pathStr: paths)
            {
                auto const programPath = std::filesystem::path(pathStr) / program;
                if (std::filesystem::exists(programPath))
                {
                    writeOutput(programPath.string() + "\n");
                    found = true;
                    if (!showAll)
                        break; // Only show first match unless -a is specified
                }
            }
        }

        if (!found)
        {
            error("which: no {} in ({})", program, pathEnv.value());
            allFound = false;
        }
    }

    _exitCode = allFound ? 0 : 1;
    context.setResult(CoreVM::CoreNumber(_exitCode));
}

} // namespace endo
