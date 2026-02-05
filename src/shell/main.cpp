// SPDX-License-Identifier: Apache-2.0
#include <crispy/logstore.h>

#include <cstdlib>
#include <print>
#include <span>
#include <string_view>

#include <unistd.h>

#include "LogConfig.hpp"

using namespace std::string_view_literals;

import Shell;

namespace
{

constexpr std::string_view Version = "0.1.0";

void printHelp(std::string_view programName)
{
    std::print(R"(endo - A modern shell written in C++23

Usage: {} [OPTIONS] [SCRIPT]
       {} -c <COMMAND>

Options:
  -h, --help         Show this help message and exit
  -v, --version      Show version information and exit
  -c <COMMAND>       Execute COMMAND and exit
  --log=<PATTERNS>   Enable logging for categories matching PATTERNS
                     (comma-separated, supports wildcards)
  --log-list         List all available log categories and exit

Log Categories:
  shell.debug        Shell execution debug output
  parser             Parser debug output
  pipe               Unix pipe operations
  vm.diag            VM diagnostics
  vm.pass            VM optimization passes

Examples:
  {}                           Start interactive shell
  {} script.sh                 Execute script file
  {} -c 'echo hello'           Execute command string
  {} --log=shell.debug         Enable shell debug logging
  {} --log='shell.*,parser'    Enable multiple log categories

)",
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
    std::string_view logPatterns;
    std::string_view command;
    std::string_view scriptFile;
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
        else if (arg.starts_with("--log="))
        {
            result.logPatterns = arg.substr(6);
        }
        else if (arg == "-c" && i + 1 < args.size())
        {
            result.command = args[++i];
        }
        else if (!arg.starts_with("-"))
        {
            result.scriptFile = arg;
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

} // namespace

int main(int argc, char const* argv[])
{
    auto const args = std::span(argv, static_cast<size_t>(argc));
    auto const programName = args.empty() ? "endo"sv : std::string_view(args[0]);

    auto const parsed = parseArguments(args);

    // Handle --log option first (patterns are stored for lazy category initialization)
    if (!parsed.logPatterns.empty())
        endo::log::Config::instance().setPatterns(parsed.logPatterns);

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

    auto shell = endo::Shell {};
    setsid();

    if (!parsed.command.empty())
        return shell.execute(std::string(parsed.command));

    if (!parsed.scriptFile.empty())
        return shell.execute(std::string(parsed.scriptFile));

    return shell.run();
}
