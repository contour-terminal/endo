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

#include <dap/DapServer.hpp>

#if defined(_WIN32)
    #include <windows.h>
#endif

#include "CrashHandler.hpp"
#include "FormatCommand.hpp"
#include "HelpPrinter.hpp"
#include "Shell.hpp"
#if defined(ENDO_ENABLE_AGENT) && ENDO_ENABLE_AGENT
    #include <agent/RunCommand.hpp>
    #include <agent/auth/LoginCommand.hpp>
    #include <agent/providers/local/ModelsCommand.hpp>
    #include <agent/tracing/TraceReplay.hpp>
#endif

using namespace std::string_view_literals;

namespace
{

constexpr std::string_view Version = "0.1.0";

void printLogCategories()
{
    std::print("Available log categories:\n\n");
    std::print("  {:<20} {:<10} {}\n", "Name", "State", "Description");
    std::print("  {:-<20} {:-<10} {:-<40}\n", "", "", "");

    for (auto const& cat: logstore::get())
    {
        auto const& category = cat.get();
        auto const state = category.is_enabled() ? "enabled"sv : "disabled"sv;
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
    bool launchDap = false;
    bool checkOnly = false;
    bool unusedDetection = false;
    std::string_view logPatterns;
    std::string_view command;
    std::vector<std::string_view> commandArgs; ///< Arguments after -c command ($1, $2, ...)
    std::string_view scriptFile;
    std::vector<std::string_view> scriptArgs; ///< Arguments after script file ($1, $2, ...)
#if defined(ENDO_ENABLE_AGENT) && ENDO_ENABLE_AGENT
    std::optional<std::string> agentTracePath; ///< Agent trace file path (nullopt = disabled).
#endif
    std::optional<std::string> logFile;        ///< Log file path for protocol messages (DAP, etc.).
    std::vector<std::string_view> modulePaths; ///< Additional module search paths (--module-path).
    bool noProfile = false;                    ///< Skip loading init.endo profile.
};

/// Tries to extract the value for a long option.
///
/// Supports both `--key=value` (returns the part after '=') and `--key value`
/// (consumes the next argument). Returns std::nullopt when the option name does
/// not match. Prints an error and exits when the option matches but no value is
/// available.
std::optional<std::string_view> consumeOptionValue(std::string_view arg,
                                                   std::string_view name,
                                                   size_t& i,
                                                   std::span<char const* const> args)
{
    // --key=value form
    if (arg.starts_with(name) && arg.size() > name.size() && arg[name.size()] == '=')
        return arg.substr(name.size() + 1);

    // --key value form
    if (arg == name)
    {
        if (i + 1 < args.size())
            return std::string_view(args[++i]);

        std::print(stderr, "{} requires a value\n", name);
        std::print(stderr, "Try '--help' for more information.\n");
        std::exit(EXIT_FAILURE);
    }

    return std::nullopt;
}

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
        else if (arg == "--dap")
        {
            result.launchDap = true;
        }
        else if (arg == "--check")
        {
            result.checkOnly = true;
        }
        else if (arg == "--unused-detection")
        {
            result.unusedDetection = true;
        }
#if defined(ENDO_ENABLE_AGENT) && ENDO_ENABLE_AGENT
        else if (arg == "--agent-trace")
        {
            result.agentTracePath = ""; // Empty = auto-generate path.
        }
        else if (auto val = consumeOptionValue(arg, "--agent-trace", i, args))
        {
            result.agentTracePath = std::string(*val);
        }
#endif
        else if (auto val = consumeOptionValue(arg, "--log-file", i, args))
        {
            result.logFile = std::string(*val);
        }
        else if (auto val = consumeOptionValue(arg, "--log", i, args))
        {
            result.logPatterns = *val;
        }
        else if (arg == "--no-profile")
        {
            result.noProfile = true;
        }
        else if (auto val = consumeOptionValue(arg, "--module-path", i, args))
        {
            result.modulePaths.emplace_back(*val);
        }
        else if (arg == "-c" && i + 1 < args.size())
        {
            result.command = args[++i];
            // Remaining arguments become $1, $2, ... for the command
            for (size_t j = i + 1; j < args.size(); ++j)
                result.commandArgs.emplace_back(args[j]);
            break; // Stop parsing after -c command and its args
        }
        else if (arg == "--")
        {
            // Everything after -- is script arguments (if script is set)
            for (size_t j = i + 1; j < args.size(); ++j)
                result.scriptArgs.emplace_back(args[j]);
            break;
        }
        else if (!arg.starts_with("-"))
        {
            result.scriptFile = arg;
            // Remaining arguments become $1, $2, ... for the script
            for (size_t j = i + 1; j < args.size(); ++j)
                result.scriptArgs.emplace_back(args[j]);
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

    // 4. Set source file path for relative module resolution
    shell.setSourceFile(std::filesystem::canonical(scriptPathStr));

    // 5. Set positional parameters ($0 = script, $1... = args)
    std::vector<std::string> params;
    params.emplace_back(scriptPath);
    for (auto const& arg: scriptArgs)
        params.emplace_back(arg);
    shell.setPositionalParameters(std::move(params));

    // 6. Execute (parser validates entire script before execution)
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

    endo::CrashHandler::initialize(std::string(Version).c_str());

    auto const args = std::span(argv, static_cast<size_t>(argc));
    auto const programName = args.empty() ? "endo"sv : std::string_view(args[0]);

    // Handle `endo format <files>` before general argument parsing
    if (args.size() >= 2 && std::string_view(args[1]) == "format"sv)
        return endo::format::runFormatCommand(args.subspan(2));

    // Handle `endo agent <subcommand>` before general argument parsing
    if (args.size() >= 2 && std::string_view(args[1]) == "agent"sv)
    {
#if defined(ENDO_ENABLE_AGENT) && ENDO_ENABLE_AGENT
        if (args.size() < 3)
        {
            std::print(stderr, "Usage: {} agent <command>\n", programName);
            std::print(stderr, "Available commands: login, status, logout, models, trace, run\n");
            return EXIT_FAILURE;
        }
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
        if (subcommand == "models")
            return endo::agent::local::runModelsCommand(args.subspan(3));
        if (subcommand == "run")
        {
            auto const runArgs = args.subspan(3);
            auto parsedRun = endo::agent::parseAgentRunArgs(runArgs);
            if (!parsedRun.has_value())
            {
                std::print(stderr, "endo agent run: {}\n", parsedRun.error());
                return EXIT_FAILURE;
            }
            auto shell = endo::Shell {};
            shell.setInteractive(false);
            shell.loadInitScript();
            return shell.runAgentHeadless(*parsedRun);
        }
        std::print(stderr, "Unknown agent command: {}\n", subcommand);
        std::print(stderr, "Available commands: login, status, logout, models, trace, run\n");
        return EXIT_FAILURE;
#else
        std::print(stderr, "endo: agent features are not available in this build\n");
        return EXIT_FAILURE;
#endif
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
        endo::printHelp();
        return EXIT_SUCCESS;
    }

    if (parsed.showVersion)
    {
        endo::printVersion();
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

    // Handle --dap mode (launch DAP server over stdio)
    if (parsed.launchDap)
    {
        endo::dap::DapServer server;
        if (parsed.logFile.has_value())
            server.setLogFile(*parsed.logFile);
        return server.run();
    }

    // Validate --check requires -c or script file
    if (parsed.checkOnly && parsed.command.empty() && parsed.scriptFile.empty())
    {
        std::print(stderr, "endo: --check requires -c <command> or a script file\n");
        return EXIT_FAILURE;
    }

    auto shell = endo::Shell {};

    for (auto const& mp: parsed.modulePaths)
        shell.addModuleSearchPath(std::filesystem::path(mp));

#if defined(ENDO_ENABLE_AGENT) && ENDO_ENABLE_AGENT
    if (parsed.agentTracePath.has_value())
        shell.setAgentTracePath(*parsed.agentTracePath);
#endif

    if (parsed.checkOnly)
        shell.setCheckOnly(true);

    if (parsed.unusedDetection)
        shell.setUnusedValueDetection(true);

    if (parsed.noProfile)
        shell.setNoProfile(true);

    // Handle -c command with optional arguments
    if (!parsed.command.empty())
    {
        shell.setInteractive(false);

        // Set positional parameters: $0 = "endo", $1... = commandArgs
        std::vector<std::string> params;
        params.emplace_back(programName);
        for (auto const& arg: parsed.commandArgs)
            params.emplace_back(arg);
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
