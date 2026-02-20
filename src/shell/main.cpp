// SPDX-License-Identifier: Apache-2.0
#include <endo-language/LogCategories.hpp>
#include <endo-language/LogConfig.hpp>

#include <lsp/LspServer.hpp>

#include <crispy/logstore.h>

#include <cerrno>
#include <clocale>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <print>
#include <span>
#include <sstream>
#include <string>
#include <string_view>

#if defined(_WIN32)
    #include <windows.h>
#endif

#include "CrashHandler.hpp"
#include "Shell.hpp"
#include <agent/auth/LoginCommand.hpp>
#include <agent/tracing/TraceReplay.hpp>

using namespace std::string_view_literals;

namespace
{

constexpr std::string_view Version = "0.1.0";

void printHelp(std::string_view programName)
{
    std::print(R"(endo - A modern shell written in C++23

Usage: {} [OPTIONS] [SCRIPT [ARGS...]]
       {} -c <COMMAND> [ARGS...]

Options:
  -h, --help         Show this help message and exit
  -v, --version      Show version information and exit
  -c <COMMAND>       Execute COMMAND and exit
  --check            Compile without executing (syntax and semantic check)
  --lsp              Launch Language Server Protocol server over stdio
  --log=<PATTERNS>   Enable logging for categories matching PATTERNS
                     (comma-separated, supports wildcards)
  --log-list         List all available log categories and exit

Agent Commands:
  agent login [PROVIDER]    Authenticate with an LLM provider (claude, openai, gemini)
                            For Claude, offers OAuth (MAX/Pro/Teams) or API key login
  agent status              Show configured providers and active selection
  agent switch [PROVIDER]   Switch the active LLM provider
  agent logout [PROVIDER]   Remove stored credentials for a provider (OAuth and API key)
  agent trace replay <FILE> Replay a tool trace JSONL file

Agent Options:
  --agent-trace[=FILE]     Enable tool I/O tracing (auto-generated path if FILE omitted)

Log Categories:
  shell.debug        Shell execution debug output
  vm.trace           VM instruction execution trace
  vm.ir              VM IR (SSA) and bytecode dump
  parser             Parser debug output
  pipe               Unix pipe operations
  vm.diag            VM diagnostics
  vm.pass            VM optimization passes

Script Execution:
  When executing a script file, arguments after the script become positional
  parameters ($1, $2, ...). The script path is available as $0.

  When using -c, arguments after the command become positional parameters.
  The program name is available as $0.

  Scripts may start with a shebang line (#!/usr/bin/env endo) which is ignored.

Examples:
  {}                              Start interactive shell
  {} script.sh                    Execute script file
  {} script.sh arg1 arg2          Execute script with arguments ($1=arg1, $2=arg2)
  {} -c 'echo hello'              Execute command string
  {} -c 'echo $1' foo             Execute command with argument ($1=foo)
  {} --log=shell.debug            Enable shell debug logging
  {} --log='shell.*,parser'       Enable multiple log categories
  {} agent login claude           Authenticate with Claude
  {} agent status                 Show provider status

)",
               programName,
               programName,
               programName,
               programName,
               programName,
               programName,
               programName,
               programName,
               programName,
               programName,
               programName);
}

void printVersion()
{
    std::print("endo version {}\n", Version);
}

void printLogCategories()
{
    std::print("Available log categories:\n\n");
    std::print("  {:<20} {:<10} {}\n", "Name", "State", "Description");
    std::print("  {:-<20} {:-<10} {:-<40}\n", "", "", "");

    for (auto const& cat: logstore::get())
    {
        auto const& category = cat.get();
        auto const state = category.is_enabled() ? "enabled" : "disabled";
        std::print("  {:<20} {:<10} {}\n", category.name(), state, category.description());
    }
    std::print("\n");
}

struct ParsedArgs
{
    bool showHelp = false;
    bool showVersion = false;
    bool showLogList = false;
    bool launchLsp = false;
    bool checkOnly = false;
    std::string_view logPatterns;
    std::string_view command;
    std::vector<std::string_view> commandArgs; ///< Arguments after -c command ($1, $2, ...)
    std::string_view scriptFile;
    std::vector<std::string_view> scriptArgs;  ///< Arguments after script file ($1, $2, ...)
    std::optional<std::string> agentTracePath; ///< Agent trace file path (nullopt = disabled).
};

ParsedArgs parseArguments(std::span<char const* const> args)
{
    ParsedArgs result;

    for (size_t i = 1; i < args.size(); ++i)
    {
        std::string_view arg = args[i];

        if (arg == "-h" || arg == "--help")
        {
            result.showHelp = true;
        }
        else if (arg == "-v" || arg == "--version")
        {
            result.showVersion = true;
        }
        else if (arg == "--log-list")
        {
            result.showLogList = true;
        }
        else if (arg == "--lsp")
        {
            result.launchLsp = true;
        }
        else if (arg == "--check")
        {
            result.checkOnly = true;
        }
        else if (arg == "--agent-trace")
        {
            result.agentTracePath = ""; // Empty = auto-generate path.
        }
        else if (arg.starts_with("--agent-trace="))
        {
            result.agentTracePath = std::string(arg.substr(14));
        }
        else if (arg.starts_with("--log="))
        {
            result.logPatterns = arg.substr(6);
        }
        else if (arg == "-c" && i + 1 < args.size())
        {
            result.command = args[++i];
            // Remaining arguments become $1, $2, ... for the command
            for (size_t j = i + 1; j < args.size(); ++j)
                result.commandArgs.push_back(args[j]);
            break; // Stop parsing after -c command and its args
        }
        else if (arg == "--")
        {
            // Everything after -- is script arguments (if script is set)
            for (size_t j = i + 1; j < args.size(); ++j)
                result.scriptArgs.push_back(args[j]);
            break;
        }
        else if (!arg.starts_with("-"))
        {
            result.scriptFile = arg;
            // Remaining arguments become $1, $2, ... for the script
            for (size_t j = i + 1; j < args.size(); ++j)
                result.scriptArgs.push_back(args[j]);
            break; // Stop parsing after script file and its args
        }
        else
        {
            std::print(stderr, "Unknown option: {}\n", arg);
            std::print(stderr, "Try '--help' for more information.\n");
            std::exit(EXIT_FAILURE);
        }
    }

    return result;
}

int executeScript(endo::Shell& shell,
                  std::string_view scriptPath,
                  std::span<std::string_view const> scriptArgs,
                  std::string_view programName)
{
    // 1. Open and read file
    auto const scriptPathStr = std::string(scriptPath);
    std::ifstream file(scriptPathStr);
    if (!file)
    {
        std::print(stderr, "endo: {}: {}\n", scriptPath, strerror(errno));
        return EXIT_FAILURE;
    }

    std::ostringstream ss;
    ss << file.rdbuf();
    std::string content = ss.str();

    // 2. Strip shebang line if present
    if (content.starts_with("#!"))
    {
        auto const pos = content.find('\n');
        if (pos != std::string::npos)
            content = content.substr(pos + 1);
        else
            content.clear(); // Script is only a shebang
    }

    // 3. Set up non-interactive mode
    shell.setInteractive(false);

    // 4. Set positional parameters ($0 = script, $1... = args)
    std::vector<std::string> params;
    params.push_back(std::string(scriptPath));
    for (auto const& arg: scriptArgs)
        params.push_back(std::string(arg));
    shell.setPositionalParameters(std::move(params));

    // 5. Execute (parser validates entire script before execution)
    return shell.execute(content);
}

#if defined(_WIN32)
/// @brief Configures the Windows console and C runtime for UTF-8 encoding.
///
/// Sets both input and output console code pages to CP_UTF8 so that multi-byte
/// UTF-8 sequences are correctly interpreted. Also aligns the C/C++ locale.
/// Must be called before any I/O operations.
void setupWindowsUtf8()
{
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    std::setlocale(LC_ALL, ".UTF-8");
}
#endif

} // namespace

int main(int argc, char const* argv[])
{
#if defined(_WIN32)
    setupWindowsUtf8();
#endif

    endo::CrashHandler::initialize(Version.data());

    auto const args = std::span(argv, static_cast<size_t>(argc));
    auto const programName = args.empty() ? "endo"sv : std::string_view(args[0]);

    // Handle `endo agent <subcommand>` before general argument parsing
    if (args.size() >= 3 && std::string_view(args[1]) == "agent"sv)
    {
        auto const subcommand = std::string_view(args[2]);
        auto const hint = (args.size() >= 4) ? std::string_view(args[3]) : ""sv;
        if (subcommand == "login")
            return endo::agent::runLoginCommand(hint);
        if (subcommand == "status")
            return endo::agent::runStatusCommand();
        if (subcommand == "logout")
            return endo::agent::runLogoutCommand(hint);
        if (subcommand == "trace" && hint == "replay" && args.size() >= 5)
            return endo::agent::runTraceReplay(std::string_view(args[4]));
        if (subcommand == "trace" && hint == "replay")
        {
            std::print(stderr, "Usage: {} agent trace replay <FILE>\n", programName);
            return EXIT_FAILURE;
        }
        std::print(stderr, "Unknown agent command: {}\n", subcommand);
        std::print(stderr, "Available commands: login, status, logout, trace\n");
        return EXIT_FAILURE;
    }

    auto const parsed = parseArguments(args);

    // Handle --log option first (patterns are stored for lazy category initialization)
    if (!parsed.logPatterns.empty())
    {
        endo::log::Config::instance().setPatterns(parsed.logPatterns);
        // Enable the console sink so log output is visible
        logstore::sink::console().set_enabled(true);
    }

    // Register all known log categories so they appear in --log-list
    endo::log::registerAllCategories();

    if (parsed.showHelp)
    {
        printHelp(programName);
        return EXIT_SUCCESS;
    }

    if (parsed.showVersion)
    {
        printVersion();
        return EXIT_SUCCESS;
    }

    if (parsed.showLogList)
    {
        printLogCategories();
        return EXIT_SUCCESS;
    }

    // Handle --lsp mode (launch LSP server over stdio)
    if (parsed.launchLsp)
        return endo::lsp::LspServer {}.run();

    // Validate --check requires -c or script file
    if (parsed.checkOnly && parsed.command.empty() && parsed.scriptFile.empty())
    {
        std::print(stderr, "endo: --check requires -c <command> or a script file\n");
        return EXIT_FAILURE;
    }

    auto shell = endo::Shell {};

    if (parsed.agentTracePath.has_value())
        shell.setAgentTracePath(*parsed.agentTracePath);

    if (parsed.checkOnly)
        shell.setCheckOnly(true);

    // Handle -c command with optional arguments
    if (!parsed.command.empty())
    {
        shell.setInteractive(false);

        // Set positional parameters: $0 = "endo", $1... = commandArgs
        std::vector<std::string> params;
        params.push_back(std::string(programName));
        for (auto const& arg: parsed.commandArgs)
            params.push_back(std::string(arg));
        shell.setPositionalParameters(std::move(params));

        return shell.execute(std::string(parsed.command));
    }

    // Handle script file with optional arguments
    if (!parsed.scriptFile.empty())
    {
        return executeScript(shell, parsed.scriptFile, parsed.scriptArgs, programName);
    }

    // Interactive mode
    return shell.run();
}
