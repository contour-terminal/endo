// SPDX-License-Identifier: Apache-2.0
#include "HelpPrinter.hpp"
#include <shell/output/FileTypeStyle.hpp>
#include <shell/ui/SyntaxHighlighter.hpp>

#include <tui/GenericSyntaxHighlighter.hpp>
#include <tui/Theme.hpp>

#include <cstdlib>
#include <format>
#include <print>
#include <string>
#include <string_view>

#if defined(_WIN32)
    #include <io.h>
    #define isatty    _isatty
    #define STDOUT_FD 1
#else
    #include <unistd.h>
    #define STDOUT_FD STDOUT_FILENO
#endif

using namespace std::string_view_literals;

namespace
{

constexpr std::string_view Version = "0.1.0";
constexpr std::string_view DocsUrl = "https://contour-terminal.github.io/endo/";
constexpr std::string_view GitHubUrl = "https://github.com/contour-terminal/endo";

constexpr std::string_view Reset = "\033[m";

/// @brief Checks whether stdout supports ANSI color output.
[[nodiscard]] bool shouldUseColor()
{
    auto const* noColor = std::getenv("NO_COLOR");
    return isatty(STDOUT_FD) && (noColor == nullptr || noColor[0] == '\0');
}

/// @brief Helper to build styled help output with optional ANSI colors.
class HelpBuilder
{
  public:
    explicit HelpBuilder(bool useColor): _useColor(useColor), _theme(tui::darkTheme()) {}

    /// @brief Appends a bold, colored section header with a blank line before it.
    void header(std::string_view text)
    {
        _out += '\n';
        if (_useColor)
        {
            auto style = tui::Style { .fg = _theme.colors.primary, .bold = true };
            _out += endo::sgrSequence(style);
            _out += text;
            _out += Reset;
        }
        else
        {
            _out += text;
        }
        _out += '\n';
    }

    /// @brief Appends an option line with colored flags and normal description.
    void option(std::string_view flags, std::string_view desc)
    {
        _out += "  ";
        if (_useColor)
        {
            auto style = tui::Style { .fg = _theme.syntaxColors.keyword, .bold = true };
            _out += endo::sgrSequence(style);
            _out += flags;
            _out += Reset;
        }
        else
        {
            _out += flags;
        }

        // Pad to alignment column (26 chars from left margin)
        constexpr int alignCol = 26;
        auto const flagsLen = static_cast<int>(flags.size());
        auto const padding = (flagsLen < alignCol) ? (alignCol - flagsLen) : 2;
        _out.append(static_cast<size_t>(padding), ' ');

        _out += desc;
        _out += '\n';
    }

    /// @brief Appends a subcommand line with colored command name and description.
    void subcommand(std::string_view name, std::string_view desc)
    {
        _out += "  ";
        if (_useColor)
        {
            auto style = tui::Style { .fg = _theme.colors.success, .bold = true };
            _out += endo::sgrSequence(style);
            _out += name;
            _out += Reset;
        }
        else
        {
            _out += name;
        }

        constexpr int alignCol = 26;
        auto const nameLen = static_cast<int>(name.size());
        auto const padding = (nameLen < alignCol) ? (alignCol - nameLen) : 2;
        _out.append(static_cast<size_t>(padding), ' ');

        _out += desc;
        _out += '\n';
    }

    /// @brief Appends a syntax-highlighted Endo code snippet, prefixed with "    $ ".
    void example(std::string_view description, std::string_view command)
    {
        // Description line
        _out += "  ";
        if (_useColor)
        {
            _out += endo::sgrSequence(tui::Style { .fg = _theme.colors.text });
            _out += description;
            _out += Reset;
        }
        else
        {
            _out += description;
        }
        _out += '\n';

        // Command line with syntax highlighting
        _out += "    ";
        if (_useColor)
        {
            _out += endo::sgrSequence(tui::Style { .fg = _theme.colors.textMuted });
            _out += "$ ";
            _out += Reset;
            _out += highlightEndoCode(command);
        }
        else
        {
            _out += "$ ";
            _out += command;
        }
        _out += '\n';
    }

    /// @brief Appends plain text with indentation.
    void text(std::string_view str)
    {
        _out += "  ";
        _out += str;
        _out += '\n';
    }

    /// @brief Appends muted/dimmed text with indentation.
    void muted(std::string_view str)
    {
        _out += "  ";
        if (_useColor)
        {
            _out += endo::sgrSequence(tui::Style { .fg = _theme.colors.textMuted, .dim = true });
            _out += str;
            _out += Reset;
        }
        else
        {
            _out += str;
        }
        _out += '\n';
    }

    /// @brief Appends a clickable hyperlink (OSC 8) with label, or plain URL when color is off.
    void link(std::string_view label, std::string_view url)
    {
        _out += "  ";
        if (_useColor)
        {
            auto style = tui::Style { .fg = _theme.colors.info, .underline = true };
            // OSC 8 hyperlink: \033]8;;URL\033\\LABEL\033]8;;\033\\
            _out += std::format("\033]8;;{}\033\\", url);
            _out += endo::sgrSequence(style);
            _out += label;
            _out += Reset;
            _out += "\033]8;;\033\\";
        }
        else
        {
            _out += label;
        }
        _out += "  ";
        if (_useColor)
        {
            _out += endo::sgrSequence(tui::Style { .fg = _theme.colors.textMuted });
            _out += url;
            _out += Reset;
        }
        else
        {
            _out += url;
        }
        _out += '\n';
    }

    /// @brief Appends raw text directly to the output buffer.
    void raw(std::string_view str) { _out += str; }

    /// @brief Appends a blank line.
    void blank() { _out += '\n'; }

    /// @brief Returns the accumulated help text.
    [[nodiscard]] std::string const& result() const noexcept { return _out; }

  private:
    [[nodiscard]] std::string highlightEndoCode(std::string_view code) const
    {
        if (!_useColor)
            return std::string(code);

        auto const endoMap = endo::computeHighlightMap(code);
        auto tuiMap = tui::HighlightMap(endoMap.size(), tui::HighlightCategory::Default);
        for (size_t i = 0; i < endoMap.size(); ++i)
            tuiMap[i] = static_cast<tui::HighlightCategory>(endoMap[i]);
        return tui::renderHighlightedLineToString(code, tuiMap, _theme);
    }

    bool _useColor;
    tui::Theme _theme;
    std::string _out;
};

} // namespace

namespace endo
{

void printHelp()
{
    auto const useColor = shouldUseColor();
    auto h = HelpBuilder(useColor);

    // Title
    {
        h.blank();
        if (useColor)
        {
            auto const titleStyle =
                tui::Style { .fg = tui::RgbColor { .r = 0x50, .g = 0x78, .b = 0xFF }, .bold = true };
            auto const taglineStyle = tui::Style { .fg = tui::darkTheme().colors.textMuted };
            h.raw("  ");
            h.raw(sgrSequence(titleStyle));
            h.raw("endo");
            h.raw(Reset);
            h.raw(" ");
            h.raw(sgrSequence(taglineStyle));
            h.raw("\u2014 A modern shell with functional programming");
            h.raw(Reset);
            h.raw("\n");
        }
        else
        {
            h.text("endo \u2014 A modern shell with functional programming");
        }
    }

    // Usage
    h.header("USAGE");
    h.text("endo [OPTIONS] [SCRIPT [ARGS...]]");
    h.text("endo -c COMMAND [ARGS...]");

    // Options
    h.header("OPTIONS");
    h.option("-h, --help", "Show this help message and exit");
    h.option("-v, --version", "Show version information and exit");
    h.option("-c COMMAND", "Execute COMMAND and exit");
    h.option("--check", "Compile without executing (syntax and semantic check)");
    h.option("--unused-detection", "Enable unused-value detection for F# bindings");
    h.option("--lsp", "Launch Language Server Protocol server");
    h.option("--dap", "Launch Debug Adapter Protocol server");
    h.option("--log-file=FILE", "Log protocol messages to FILE");
    h.option("--log=PATTERNS", "Enable logging for matching categories (comma-separated)");
    h.option("--log-list", "List all available log categories and exit");

    // Subcommands
    h.header("SUBCOMMANDS");
    h.subcommand("format FILE...", "Format Endo source files (see: endo format --help)");
    h.subcommand("agent COMMAND", "Manage AI agent providers and models");

    // Agent Commands
    h.header("AGENT COMMANDS");
    h.subcommand("agent login [PROVIDER]", "Authenticate (claude, openai, gemini)");
    h.subcommand("agent status", "Show configured providers and active selection");
    h.subcommand("agent switch [PROVIDER]", "Switch the active LLM provider");
    h.subcommand("agent logout [PROVIDER]", "Remove stored credentials");
    h.subcommand("agent models SUBCMD", "Manage local GGUF models (list, download, remove, info)");
    h.subcommand("agent run", "Run agent in headless mode");
    h.subcommand("agent trace replay FILE", "Replay a tool trace JSONL file");
    h.option("--agent-trace[=FILE]", "Enable tool I/O tracing (auto-generated path if omitted)");

    // Script Execution
    h.header("SCRIPT EXECUTION");
    h.muted("Arguments after the script file become positional parameters ($1, $2, ...).");
    h.muted("The script path is available as $0. Shebang lines (#!/usr/bin/env endo) are ignored.");
    h.muted("With -c, arguments after the command become $1, $2, ... and $0 is the program name.");

    // Examples
    h.header("EXAMPLES");
    h.example("Start interactive shell:", "endo");
    h.blank();
    h.example("Execute a shell command:", "endo -c 'echo hello world'");
    h.blank();
    h.example("Functional programming with pipelines:",
              "endo -c 'let xs = [1; 2; 3] in xs |> map (fun x -> x * 2) |> println'");
    h.blank();
    h.example(
        "Pattern matching:",
        R"(endo -c 'match Some 42 with | Some x -> println $"some value of {x}" | None -> print "nothing"')");
    h.blank();
    h.example("Sort and filter a list:", "endo -c '[3; 1; 4; 1; 5] |> sort |> filter (_ > 2) |> println'");
    h.blank();
    h.example("Execute a script with arguments:", "endo script.endo arg1 arg2");

    // Learn More
    h.header("LEARN MORE");
    h.link("Documentation", DocsUrl);
    h.link("GitHub", GitHubUrl);
    h.blank();

    std::print("{}", h.result());
}

void printVersion()
{
    auto const useColor = shouldUseColor();
    if (useColor)
    {
        auto const theme = tui::darkTheme();
        auto const nameStyle =
            tui::Style { .fg = tui::RgbColor { .r = 0x50, .g = 0x78, .b = 0xFF }, .bold = true };
        auto const versionStyle = tui::Style { .fg = theme.colors.success };
        std::print(
            "{}endo{} {}{}{}", sgrSequence(nameStyle), Reset, sgrSequence(versionStyle), Version, Reset);
    }
    else
    {
        std::print("endo version {}", Version);
    }
    std::print("\n");
}

} // namespace endo
