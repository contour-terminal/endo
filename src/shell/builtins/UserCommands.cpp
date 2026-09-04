// SPDX-License-Identifier: Apache-2.0
#include <shell/Shell.hpp>
#include <shell/builtins/InlineArgParser.hpp>
#include <shell/builtins/WhichDescriptor.hpp>
#include <shell/commands/BindCommand.hpp>
#include <shell/output/TableFormatter.hpp>
#include <shell/util/CommandResolver.hpp>

#include <tui/MarkdownRenderer.hpp>
#include <tui/TerminalOutput.hpp>

#include <algorithm>
#include <format>

#include <platform/Types.hpp>

#if !defined(_WIN32)
    #include <unistd.h>
#endif

namespace endo
{

void Shell::builtinBind(CoreVM::Params& context)
{
    // Get arguments (may be empty if called without arguments)
    std::vector<std::string> args;
    if (context.count() >= 1)
    {
        auto const& argArray = context.getStringArray(1);
        for (const auto& i: argArray)
            args.push_back(i);
    }

    // No arguments: list all bindings as structured table
    if (args.empty())
    {
        BindCommand cmd(prompt.keyBindings());
        auto* list = cmd.execute(*_runner);

        auto const useColor = _tty.isTerminal();
        TableConfig config;
        config.style = useColor ? TableStyle::Bordered : TableStyle::Plain;
        config.useColor = useColor;
        _tty.writeToStdout(formatRecordTable(list, _runner, config));

        _exitCode = 0;
        context.setResult(static_cast<CoreVM::CoreNumber>(0));
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
            context.setResult(static_cast<CoreVM::CoreNumber>(1));
            return;
        }

        auto const chord = tui::KeyChord::parse(args[1]);
        if (!chord)
        {
            error("bind: invalid key chord: {}", args[1]);
            _exitCode = 1;
            context.setResult(static_cast<CoreVM::CoreNumber>(1));
            return;
        }

        prompt.unbindKey(*chord);
        _exitCode = 0;
        context.setResult(static_cast<CoreVM::CoreNumber>(0));
        return;
    }

    if (args[0] == "--reset")
    {
        // Reset to defaults: bind --reset
        prompt.resetKeyBindings();
        _exitCode = 0;
        context.setResult(static_cast<CoreVM::CoreNumber>(0));
        return;
    }

    if (args[0] == "-l" || args[0] == "--list")
    {
        // List bindings (same as no arguments)
        BindCommand cmd(prompt.keyBindings());
        auto* list = cmd.execute(*_runner);

        auto const useColor = _tty.isTerminal();
        TableConfig config;
        config.style = useColor ? TableStyle::Bordered : TableStyle::Plain;
        config.useColor = useColor;
        _tty.writeToStdout(formatRecordTable(list, _runner, config));

        _exitCode = 0;
        context.setResult(static_cast<CoreVM::CoreNumber>(0));
        return;
    }

    if (args[0] == "-h" || args[0] == "--help")
    {
        NativeHandle const outputFd =
            _redirectState.getEffectiveStdoutFd(_currentPipelineBuilder.defaultStdoutFd, _processManager);
        (void) renderMarkdownHelp(
            outputFd,
            "# bind\n"
            "\n"
            "Manage key bindings for the line editor.\n"
            "\n"
            "## Usage\n"
            "\n"
            "`bind [OPTIONS] [KEY ACTION]`\n"
            "\n"
            "## Options\n"
            "\n"
            "| Option | Description |\n"
            "|--------|-------------|\n"
            "| `-l`, `--list` | List all keybindings |\n"
            "| `-r`, `--remove` | Remove a keybinding: `bind -r ctrl+y` |\n"
            "| `--reset` | Reset all keybindings to defaults |\n"
            "| `-h`, `--help` | Show this help message |\n"
            "\n"
            "## Examples\n"
            "\n"
            "```\n"
            "bind                     # List all bindings\n"
            "bind ctrl+y redo         # Bind Ctrl+Y to redo\n"
            "bind ctrl+y yank         # Bind Ctrl+Y to yank (Emacs-style)\n"
            "bind -r ctrl+y           # Remove Ctrl+Y binding\n"
            "bind --reset             # Reset to defaults\n"
            "```\n"
            "\n"
            "## Key Format\n"
            "\n"
            "`[modifier+]...key`\n"
            "\n"
            "**Modifiers:** `ctrl`, `alt`, `shift`, `super`\n"
            "\n"
            "**Keys:** `a`-`z`, `enter`, `backspace`, `delete`, `tab`, `escape`,\n"
            "`up`, `down`, `left`, `right`, `home`, `end`, `f1`-`f12`\n"
            "\n"
            "## Actions\n"
            "\n"
            "| Category | Actions |\n"
            "|----------|----------|\n"
            "| Movement | `move-forward-char`, `move-backward-char`, `move-forward-word`, "
            "`move-backward-word`, `move-to-line-start`, `move-to-line-end`, "
            "`move-to-buffer-start`, `move-to-buffer-end`, `move-up`, `move-down`, "
            "`smart-move-to-line-start`, `smart-move-to-line-end` |\n"
            "| Editing | `delete-char-backward`, `delete-char-forward`, `delete-word`, "
            "`delete-word-backward`, `delete-big-word-backward`, `kill-to-end`, `kill-to-start`, "
            "`transpose`, `clear-buffer` |\n"
            "| Undo | `undo`, `redo` |\n"
            "| Kill Ring | `yank`, `yank-pop` |\n"
            "| Selection | `select-all` |\n"
            "| Clipboard | `cut`, `copy`, `paste` |\n"
            "| Control | `submit`, `abort`, `insert-newline`, `agent-mode`, "
            "`cycle-agent-mode`, `cycle-thinking-mode`, `cycle-model` |\n"
            "| History | `history-prev`, `history-next` |\n"
            "| Command Palette | `command-palette` |\n");
        _exitCode = 0;
        context.setResult(static_cast<CoreVM::CoreNumber>(0));
        return;
    }

    // Set binding: bind <key> <action>
    if (args.size() < 2)
    {
        error("bind: requires key and action arguments");
        error("Usage: bind <key> <action>");
        error("Run 'bind --help' for more information.");
        _exitCode = 1;
        context.setResult(static_cast<CoreVM::CoreNumber>(1));
        return;
    }

    auto const chord = tui::KeyChord::parse(args[0]);
    if (!chord)
    {
        error("bind: invalid key chord: {}", args[0]);
        _exitCode = 1;
        context.setResult(static_cast<CoreVM::CoreNumber>(1));
        return;
    }

    auto const action = tui::parseEditAction(args[1]);
    if (!action)
    {
        error("bind: unknown action: {}", args[1]);
        error("Run 'bind --help' to see available actions.");
        _exitCode = 1;
        context.setResult(static_cast<CoreVM::CoreNumber>(1));
        return;
    }

    prompt.bindKey(*chord, *action);
    _exitCode = 0;
    context.setResult(static_cast<CoreVM::CoreNumber>(0));
}

void Shell::builtinWhich(CoreVM::Params& context)
{
    // Get arguments (may be empty if called without arguments)
    std::vector<std::string> args;
    if (context.count() >= 1)
    {
        auto const& argArray = context.getStringArray(1);
        for (const auto& i: argArray)
            args.push_back(i);
    }

    // Parse against whichDescriptor()'s option table through the shared parser, so `which`
    // accepts the same forms as every other descriptor-driven builtin -- bundled shorts
    // (-ai), --flag=value and a `--` end-of-options marker -- from one declaration that also
    // drives the help text below and the completion spec.
    auto argv = CoreVM::CoreStringArray {};
    argv.reserve(args.size() + 1);
    argv.emplace_back("which"); // parseInlineArgs() skips argv[0] as the command name
    argv.insert(argv.end(), args.begin(), args.end());

    auto const parsed = parseInlineArgs(argv, whichDescriptor().options);
    auto const& programs = parsed.positionalArgs;
    bool const showAll = parsed.hasFlag("-a");
    bool const readAlias = parsed.hasFlag("-i");

    // parseInlineArgs() demotes an unrecognised flag to a positional argument. `which` reports
    // it rather than searching $PATH for a program literally named "-x" -- except after `--`,
    // where a leading dash is part of a genuine file name.
    if (std::ranges::find(args, "--") == args.end())
    {
        auto const unknown = std::ranges::find_if(programs, [](auto const& p) { return p.starts_with('-'); });
        if (unknown != programs.end())
        {
            error("which: invalid option: {}", *unknown);
            _exitCode = 1;
            context.setResult(static_cast<CoreVM::CoreNumber>(1));
            return;
        }
    }

    // Helper to write output to the effective stdout (respects redirects and test environments)
    auto writeOutput = [this](std::string const& str) {
        NativeHandle const outputFd =
            _redirectState.getEffectiveStdoutFd(_currentPipelineBuilder.defaultStdoutFd, _processManager);
        [[maybe_unused]] auto written = platformWrite(outputFd, str.data(), str.size());
    };

    // Show help if requested or no arguments given
    if (parsed.helpRequested || programs.empty())
    {
        NativeHandle const outputFd =
            _redirectState.getEffectiveStdoutFd(_currentPipelineBuilder.defaultStdoutFd, _processManager);
        // Title, usage and the options table come from the descriptor; generateInlineHelp()
        // has no notion of exit status, so that section is appended here.
        (void) renderMarkdownHelp(outputFd,
                                  generateInlineHelp(whichDescriptor())
                                      + "\n## Exit Status\n"
                                        "\n"
                                        "| Code | Meaning |\n"
                                        "|------|----------|\n"
                                        "| `0` | All programs were found |\n"
                                        "| `1` | One or more programs were not found |\n");
        _exitCode = 0;
        context.setResult(static_cast<CoreVM::CoreNumber>(0));
        return;
    }

    // Warn about --read-alias since aliases aren't implemented yet
    if (readAlias)
    {
        error("which: --read-alias: aliases not yet implemented");
    }

    // Use CommandResolver for PATH search (handles PATH separator and PATHEXT correctly)
    auto const resolver = CommandResolver(_env, _fs);
    bool allFound = true;

    // Search for each program
    for (auto const& program: programs)
    {
        bool found = false;

        // If program contains a path separator, treat as path
        if (program.contains('/') || program.contains('\\'))
        {
            if (_fs.exists(program))
            {
                writeOutput(program + "\n");
                found = true;
            }
        }
        else
        {
            auto const matches = resolver.findAllInPath(program);
            for (auto const& path: matches)
            {
                writeOutput(path + "\n");
                found = true;
                if (!showAll)
                    break;
            }
        }

        if (!found)
        {
            auto const pathEnv = _env.get("PATH").value_or("");
            error("which: no {} in ({})", program, pathEnv);
            allFound = false;
        }
    }

    _exitCode = allFound ? 0 : 1;
    context.setResult(static_cast<CoreVM::CoreNumber>(_exitCode));
}

} // namespace endo
