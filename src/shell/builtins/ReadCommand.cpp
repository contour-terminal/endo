// SPDX-License-Identifier: Apache-2.0
#include <shell/Shell.hpp>
#include <shell/TTY.hpp>

#include <tui/MarkdownRenderer.hpp>
#include <tui/TerminalOutput.hpp>

#include <chrono>
#include <format>
#include <iostream>
#include <print>

#include <platform/Process.hpp>
#include <platform/Types.hpp>

#if !defined(_WIN32)
    #include <poll.h>
    #include <termios.h>
    #include <unistd.h>
#else
    #include <thread>
#endif

namespace endo
{

std::vector<std::string> Shell::splitByIFS(std::string_view input) const
{
    // Get IFS, default to space/tab/newline (bash default)
    std::string const ifs = std::string(_env.get("IFS").value_or(" \t\n"));

    if (ifs.empty())
    {
        // Empty IFS = no splitting, return whole input as single element
        return { std::string(input) };
    }

    // IFS splitting: leading/trailing whitespace in IFS is trimmed, non-whitespace delimiters create
    // explicit fields
    std::vector<std::string> result;
    std::string current;

    // Track which characters in IFS are "whitespace" (space, tab, newline)
    auto isIFSWhitespace = [&ifs](char c) {
        return (c == ' ' || c == '\t' || c == '\n') && ifs.find(c) != std::string::npos;
    };

    auto isIFSChar = [&ifs](char c) {
        return ifs.find(c) != std::string::npos;
    };

    size_t i = 0;

    // Skip leading IFS whitespace
    while (i < input.size() && isIFSWhitespace(input[i]))
        ++i;

    while (i < input.size())
    {
        if (isIFSChar(input[i]))
        {
            // Save current field
            result.push_back(current);
            current.clear();

            // Skip IFS whitespace after delimiter
            ++i;
            while (i < input.size() && isIFSWhitespace(input[i]))
                ++i;
        }
        else
        {
            current += input[i];
            ++i;
        }
    }

    // Add last field (even if empty, unless input ended with IFS)
    if (!current.empty())
        result.push_back(current);

    return result;
}

std::string Shell::readInputLine(NativeHandle inputFd, ReadOptions const& options)
{
    std::string line;
    bool escape = false;

#if !defined(_WIN32)
    // Set up terminal for silent mode if needed
    struct termios oldTermios {};
    bool termiosChanged = false;
    if (options.silent && isatty(inputFd))
    {
        struct termios newTermios {};
        if (tcgetattr(inputFd, &oldTermios) == 0)
        {
            newTermios = oldTermios;
            newTermios.c_lflag &= ~static_cast<tcflag_t>(ECHO);
            tcsetattr(inputFd, TCSANOW, &newTermios);
            termiosChanged = true;
        }
    }
#endif

    // Set up timeout if specified
    auto const deadline = options.timeout.has_value()
                              ? std::optional(std::chrono::steady_clock::now() + *options.timeout)
                              : std::nullopt;

    while (true)
    {
        // Check timeout
        if (deadline.has_value())
        {
            auto const remaining = *deadline - std::chrono::steady_clock::now();
            if (remaining <= std::chrono::milliseconds::zero())
                break; // Timeout

#if !defined(_WIN32)
            // Use poll for timeout
            pollfd pfd { .fd = inputFd, .events = POLLIN, .revents = 0 };
            int const timeoutMs =
                static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count());
            int const pollResult = poll(&pfd, 1, timeoutMs);
            if (pollResult <= 0)
                break; // Timeout or error
#else
            // Windows: poll with PeekNamedPipe
            {
                auto const start = std::chrono::steady_clock::now();
                bool dataAvailable = false;
                while (std::chrono::steady_clock::now() - start < remaining)
                {
                    DWORD avail = 0;
                    if (PeekNamedPipe(inputFd, nullptr, 0, nullptr, &avail, nullptr) && avail > 0)
                    {
                        dataAvailable = true;
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                if (!dataAvailable)
                    break; // Timeout
            }
#endif
        }

        // Read one byte
        char ch {};
        auto const bytesRead = platformRead(inputFd, &ch, 1);
        if (bytesRead <= 0)
            break; // EOF or error

        // Check max chars
        if (options.maxChars.has_value() && line.size() >= *options.maxChars)
            break;

        // Handle delimiter
        if (ch == options.delimiter)
            break;

        // Handle backslash escape (unless in raw mode)
        if (!options.rawMode && !escape && ch == '\\')
        {
            escape = true;
            continue;
        }

        if (escape)
        {
            escape = false;
            if (ch == '\n')
                continue; // Line continuation
            // Otherwise, add the literal character (backslash is consumed)
            line += ch;
            continue;
        }

        line += ch;
    }

#if !defined(_WIN32)
    // Restore terminal settings
    if (termiosChanged)
    {
        tcsetattr(inputFd, TCSANOW, &oldTermios);
        if (options.silent)
        {
            // Print newline after silent input
            [[maybe_unused]] auto w = platformWrite(standardOutput(), "\n", 1);
        }
    }
#endif

    return line;
}

void Shell::builtinReadDefault(CoreVM::Params& context)
{
    NativeHandle const inputFd =
        _redirectState.getEffectiveStdinFd(_currentPipelineBuilder.defaultStdinFd, _processManager);

    ReadOptions options;
    auto const line = readInputLine(inputFd, options);

    // Set REPLY variable
    _env.set("REPLY", line);
    _exitCode = line.empty() ? 1 : 0;

    context.setResult(line);
}

void Shell::builtinRead(CoreVM::Params& context)
{
    CoreVM::CoreStringArray const& args = context.getStringArray(1);

    // Parse read options
    ReadOptions options;
    size_t i = 0;
    while (i < args.size())
    {
        std::string_view const arg = args.at(i);

        if (arg == "-h" || arg == "--help")
        {
            NativeHandle const outputFd =
                _redirectState.getEffectiveStdoutFd(_currentPipelineBuilder.defaultStdoutFd, _processManager);
            (void) renderMarkdownHelp(outputFd,
                                      "# read\n"
                                      "\n"
                                      "Read a line from standard input.\n"
                                      "\n"
                                      "## Usage\n"
                                      "\n"
                                      "`read [OPTIONS] [VAR...]`\n"
                                      "\n"
                                      "## Options\n"
                                      "\n"
                                      "| Option | Description |\n"
                                      "|--------|-------------|\n"
                                      "| `-p PROMPT` | Display PROMPT before reading |\n"
                                      "| `-r` | Raw mode (don't interpret backslashes) |\n"
                                      "| `-s` | Silent mode (don't echo input) |\n"
                                      "| `-n NCHARS` | Read at most NCHARS characters |\n"
                                      "| `-t TIMEOUT` | Timeout in seconds |\n"
                                      "| `-d DELIM` | Use DELIM as line delimiter (default: newline) |\n"
                                      "| `-h`, `--help` | Display this help |\n"
                                      "\n"
                                      "If no VAR specified, input is stored in `REPLY`.\n"
                                      "Multiple VARs split input by `$IFS` (default: space/tab/newline).\n");
            _exitCode = 0;
            context.setResult(CoreVM::CoreString(""));
            return;
        }
        if (arg == "-p" && i + 1 < args.size())
        {
            options.prompt = args.at(i + 1);
            i += 2;
            continue;
        }
        if (arg == "-r")
        {
            options.rawMode = true;
            ++i;
            continue;
        }
        if (arg == "-s")
        {
            options.silent = true;
            ++i;
            continue;
        }
        if (arg == "-n" && i + 1 < args.size())
        {
            try
            {
                options.maxChars = std::stoull(args.at(i + 1));
            }
            catch (...)
            {
                error("read: invalid count: {}", args.at(i + 1));
                _exitCode = 1;
                context.setResult("");
                return;
            }
            i += 2;
            continue;
        }
        if (arg == "-t" && i + 1 < args.size())
        {
            try
            {
                double seconds = std::stod(args.at(i + 1));
                options.timeout = std::chrono::milliseconds(static_cast<long long>(seconds * 1000));
            }
            catch (...)
            {
                error("read: invalid timeout: {}", args.at(i + 1));
                _exitCode = 1;
                context.setResult("");
                return;
            }
            i += 2;
            continue;
        }
        if (arg == "-d" && i + 1 < args.size())
        {
            auto const& delimStr = args.at(i + 1);
            if (!delimStr.empty())
                options.delimiter = delimStr[0];
            i += 2;
            continue;
        }

        // Reject unknown options (arguments starting with '-')
        if (arg.starts_with("-"))
        {
            error("read: {}: invalid option", arg);
            _exitCode = 1;
            context.setResult("");
            return;
        }

        // Not a flag, must be a variable name
        options.variableNames.emplace_back(arg);
        ++i;
    }

    // Display prompt if specified
    if (!options.prompt.empty())
    {
        NativeHandle const outputFd =
            _redirectState.getEffectiveStdoutFd(_currentPipelineBuilder.defaultStdoutFd, _processManager);
        [[maybe_unused]] auto w = platformWrite(outputFd, options.prompt.data(), options.prompt.size());
    }

    // Read input
    NativeHandle const inputFd =
        _redirectState.getEffectiveStdinFd(_currentPipelineBuilder.defaultStdinFd, _processManager);
    auto const line = readInputLine(inputFd, options);

    // Split by IFS
    auto fields = splitByIFS(line);

    // Assign to variables
    if (options.variableNames.empty())
    {
        // Default: set REPLY
        _env.set("REPLY", line);
    }
    else
    {
        for (size_t vi = 0; vi < options.variableNames.size(); ++vi)
        {
            if (vi < options.variableNames.size() - 1)
            {
                // Assign individual field
                if (vi < fields.size())
                    _env.set(options.variableNames[vi], fields[vi]);
                else
                    _env.set(options.variableNames[vi], "");
            }
            else
            {
                // Last variable gets the rest
                std::string rest;
                for (size_t fi = vi; fi < fields.size(); ++fi)
                {
                    if (fi > vi)
                        rest += ' ';
                    rest += fields[fi];
                }
                _env.set(options.variableNames[vi], rest);
            }
        }
    }

    _exitCode = line.empty() ? 1 : 0;
    context.setResult(line);
}

} // namespace endo
