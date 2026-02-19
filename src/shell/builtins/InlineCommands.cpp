// SPDX-License-Identifier: Apache-2.0
#include <shell/Shell.hpp>

#include <chrono>
#include <filesystem>
#include <format>
#include <iostream>
#include <thread>

#include <fcntl.h>

#include <platform/Process.hpp>
#include <platform/Types.hpp>

namespace
{

std::string processEscapeSequences(std::string_view input)
{
    std::string result;
    result.reserve(input.size());

    for (size_t i = 0; i < input.size(); ++i)
    {
        if (input[i] == '\\' && i + 1 < input.size())
        {
            char next = input[i + 1];
            switch (next)
            {
                case '\\':
                    result += '\\';
                    ++i;
                    break;
                case 'a':
                    result += '\a';
                    ++i;
                    break;
                case 'b':
                    result += '\b';
                    ++i;
                    break;
                case 'e':
                    result += '\x1B';
                    ++i;
                    break;
                case 'f':
                    result += '\f';
                    ++i;
                    break;
                case 'n':
                    result += '\n';
                    ++i;
                    break;
                case 'r':
                    result += '\r';
                    ++i;
                    break;
                case 't':
                    result += '\t';
                    ++i;
                    break;
                case 'v':
                    result += '\v';
                    ++i;
                    break;
                case '0': {
                    // Octal: \0, \0n, \0nn, \0nnn
                    ++i; // skip backslash
                    ++i; // skip '0'
                    int value = 0;
                    int digits = 0;
                    while (i < input.size() && digits < 3 && input[i] >= '0' && input[i] <= '7')
                    {
                        value = value * 8 + (input[i] - '0');
                        ++i;
                        ++digits;
                    }
                    --i; // compensate for loop increment
                    result += static_cast<char>(value);
                    break;
                }
                case 'x': {
                    // Hex: \xH, \xHH
                    ++i; // skip backslash
                    ++i; // skip 'x'
                    int value = 0;
                    int digits = 0;
                    while (i < input.size() && digits < 2)
                    {
                        char c = input[i];
                        if (c >= '0' && c <= '9')
                            value = value * 16 + (c - '0');
                        else if (c >= 'a' && c <= 'f')
                            value = value * 16 + (c - 'a' + 10);
                        else if (c >= 'A' && c <= 'F')
                            value = value * 16 + (c - 'A' + 10);
                        else
                            break;
                        ++i;
                        ++digits;
                    }
                    --i; // compensate for loop increment
                    if (digits > 0)
                        result += static_cast<char>(value);
                    else
                        result += "\\x"; // invalid escape, keep literal
                    break;
                }
                default:
                    // Unknown escape - keep literal
                    result += input[i];
                    break;
            }
        }
        else
        {
            result += input[i];
        }
    }
    return result;
}

} // namespace

namespace endo
{

int Shell::executeInlineEcho(CoreVM::CoreStringArray const& args, NativeHandle outputFd)
{
    std::vector<std::string> echoArgs;
    for (size_t i = 1; i < args.size(); ++i)
        echoArgs.push_back(args.at(i));

    bool suppressNewline = false;
    bool interpretEscapes = false;
    size_t argStart = 0;

    // Parse flags
    for (size_t i = 0; i < echoArgs.size(); ++i)
    {
        std::string_view arg = echoArgs[i];

        if (arg == "--")
        {
            argStart = i + 1;
            break;
        }

        if (arg.starts_with("-") && arg.size() > 1 && arg[1] != '-')
        {
            bool validFlag = true;
            for (size_t j = 1; j < arg.size(); ++j)
            {
                if (arg[j] == 'n')
                    suppressNewline = true;
                else if (arg[j] == 'e')
                    interpretEscapes = true;
                else
                {
                    validFlag = false;
                    break;
                }
            }

            if (validFlag)
            {
                argStart = i + 1;
                continue;
            }
        }

        argStart = i;
        break;
    }

    // Build output string
    std::string output;
    for (size_t i = argStart; i < echoArgs.size(); ++i)
    {
        if (i > argStart)
            output += ' ';
        output += echoArgs[i];
    }

    // Process escape sequences if -e flag is set
    if (interpretEscapes)
        output = processEscapeSequences(output);

    // Add newline if not suppressed
    if (!suppressNewline)
        output += '\n';

    [[maybe_unused]] auto written = platformWrite(outputFd, output.data(), output.size());
    return 0;
}

int Shell::executeInlineCat(CoreVM::CoreStringArray const& args, NativeHandle outputFd, NativeHandle stdinFd)
{
    std::vector<std::string> catArgs;
    for (size_t i = 1; i < args.size(); ++i)
        catArgs.push_back(args.at(i));

    // Parse flags
    bool numberLines = false;
    bool numberNonBlank = false;
    bool squeezeBlank = false;
    bool showEnds = false;
    bool showTabs = false;
    bool showHelp = false;
    std::vector<std::string> files;

    for (size_t i = 0; i < catArgs.size(); ++i)
    {
        std::string_view arg = catArgs[i];

        if (arg == "--")
        {
            for (size_t j = i + 1; j < catArgs.size(); ++j)
                files.push_back(catArgs[j]);
            break;
        }

        if (arg == "--help")
        {
            showHelp = true;
            continue;
        }
        if (arg == "--number")
        {
            numberLines = true;
            continue;
        }
        if (arg == "--number-nonblank")
        {
            numberNonBlank = true;
            continue;
        }
        if (arg == "--squeeze-blank")
        {
            squeezeBlank = true;
            continue;
        }
        if (arg == "--show-ends")
        {
            showEnds = true;
            continue;
        }
        if (arg == "--show-tabs")
        {
            showTabs = true;
            continue;
        }
        if (arg == "--show-all")
        {
            showEnds = true;
            showTabs = true;
            continue;
        }

        if (arg.starts_with("-") && arg.size() > 1 && arg[1] != '-')
        {
            bool validFlag = true;
            for (size_t j = 1; j < arg.size(); ++j)
            {
                switch (arg[j])
                {
                    case 'n': numberLines = true; break;
                    case 'b': numberNonBlank = true; break;
                    case 's': squeezeBlank = true; break;
                    case 'E': showEnds = true; break;
                    case 'T': showTabs = true; break;
                    case 'A':
                        showEnds = true;
                        showTabs = true;
                        break;
                    case 'h': showHelp = true; break;
                    default: validFlag = false; break;
                }
                if (!validFlag)
                    break;
            }
            if (validFlag)
                continue;
        }

        files.push_back(std::string(arg));
    }

    // -b overrides -n
    if (numberNonBlank)
        numberLines = false;

    auto writeOutput = [outputFd](std::string const& str) {
        [[maybe_unused]] auto written = platformWrite(outputFd, str.data(), str.size());
    };

    if (showHelp)
    {
        writeOutput("Usage: cat [OPTION]... [FILE]...\n");
        writeOutput("Concatenate FILE(s) to standard output.\n");
        writeOutput("With no FILE, or when FILE is -, read standard input.\n");
        writeOutput("\n");
        writeOutput("  -n, --number           number all output lines\n");
        writeOutput("  -b, --number-nonblank  number non-blank output lines (overrides -n)\n");
        writeOutput("  -s, --squeeze-blank    suppress repeated empty output lines\n");
        writeOutput("  -E, --show-ends        display $ at end of each line\n");
        writeOutput("  -T, --show-tabs        display TAB characters as ^I\n");
        writeOutput("  -A, --show-all         equivalent to -ET\n");
        writeOutput("  -h, --help             display this help and exit\n");
        return 0;
    }

    int lineNumber = 1;
    bool lastLineWasBlank = false;

    auto processContent = [&](std::string const& content) {
        std::string line;
        for (size_t i = 0; i < content.size(); ++i)
        {
            char c = content[i];
            if (c == '\n')
            {
                bool isBlank = line.empty();

                if (squeezeBlank && isBlank && lastLineWasBlank)
                {
                    line.clear();
                    continue;
                }
                lastLineWasBlank = isBlank;

                if (showTabs)
                {
                    std::string processed;
                    for (char ch: line)
                    {
                        if (ch == '\t')
                            processed += "^I";
                        else
                            processed += ch;
                    }
                    line = std::move(processed);
                }

                std::string output;
                if (numberNonBlank && !isBlank)
                    output = std::format("{:>6}\t", lineNumber++);
                else if (numberLines)
                    output = std::format("{:>6}\t", lineNumber++);
                output += line;
                if (showEnds)
                    output += '$';
                output += '\n';
                writeOutput(output);
                line.clear();
            }
            else
            {
                line += c;
            }
        }
        // Handle last line without newline
        if (!line.empty())
        {
            if (showTabs)
            {
                std::string processed;
                for (char ch: line)
                {
                    if (ch == '\t')
                        processed += "^I";
                    else
                        processed += ch;
                }
                line = std::move(processed);
            }

            std::string output;
            if (numberNonBlank && !line.empty())
                output = std::format("{:>6}\t", lineNumber++);
            else if (numberLines)
                output = std::format("{:>6}\t", lineNumber++);
            output += line;
            writeOutput(output);
        }
    };

    auto readFromFd = [](NativeHandle fd) -> std::string {
        std::string content;
        char buffer[4096];
        intptr_t bytesRead;
        while ((bytesRead = platformRead(fd, buffer, sizeof(buffer))) > 0)
            content.append(buffer, static_cast<size_t>(bytesRead));
        return content;
    };

    bool success = true;

    if (files.empty())
        files.push_back("-");

    for (auto const& file: files)
    {
        if (file == "-")
        {
            std::string content = readFromFd(stdinFd);
            processContent(content);
        }
        else
        {
            auto const result = _processManager.openFile(file, O_RDONLY);
            if (!result.has_value())
            {
                error("cat: {}: {}", file, strerror(errno));
                success = false;
                continue;
            }
            NativeHandle fd = result.value();
            std::string content = readFromFd(fd);
            _processManager.closeHandle(fd);
            processContent(content);
        }
    }

    return success ? 0 : 1;
}

int Shell::executeInlineSleep(CoreVM::CoreStringArray const& args, NativeHandle outputFd)
{
    std::vector<std::string> sleepArgs;
    for (size_t i = 1; i < args.size(); ++i)
        sleepArgs.push_back(args.at(i));

    auto writeOutput = [outputFd](std::string const& str) {
        [[maybe_unused]] auto written = platformWrite(outputFd, str.data(), str.size());
    };

    // Check for help
    for (auto const& arg: sleepArgs)
    {
        if (arg == "-h" || arg == "--help")
        {
            writeOutput("Usage: sleep NUMBER[SUFFIX]...\n");
            writeOutput("Pause for NUMBER seconds.\n");
            writeOutput("\n");
            writeOutput("SUFFIX may be:\n");
            writeOutput("  s   seconds (default)\n");
            writeOutput("  m   minutes\n");
            writeOutput("  h   hours\n");
            writeOutput("  d   days\n");
            writeOutput("\n");
            writeOutput("Multiple arguments are summed together.\n");
            writeOutput("NUMBER may be an integer or floating-point number.\n");
            return 0;
        }
    }

    // No arguments - error
    if (sleepArgs.empty())
    {
        error("sleep: missing operand");
        return 1;
    }

    // Parse duration arguments
    double totalSeconds = 0.0;
    for (auto const& arg: sleepArgs)
    {
        if (arg.empty())
            continue;

        double multiplier = 1.0;
        std::string numStr = arg;

        // Check for suffix
        char lastChar = arg.back();
        if (lastChar == 's' || lastChar == 'S')
        {
            multiplier = 1.0;
            numStr = arg.substr(0, arg.size() - 1);
        }
        else if (lastChar == 'm' || lastChar == 'M')
        {
            multiplier = 60.0;
            numStr = arg.substr(0, arg.size() - 1);
        }
        else if (lastChar == 'h' || lastChar == 'H')
        {
            multiplier = 3600.0;
            numStr = arg.substr(0, arg.size() - 1);
        }
        else if (lastChar == 'd' || lastChar == 'D')
        {
            multiplier = 86400.0;
            numStr = arg.substr(0, arg.size() - 1);
        }

        if (numStr.empty())
        {
            error("sleep: invalid time interval '{}'", arg);
            return 1;
        }

        try
        {
            size_t pos = 0;
            double value = std::stod(numStr, &pos);
            if (pos != numStr.size() || value < 0)
            {
                error("sleep: invalid time interval '{}'", arg);
                return 1;
            }
            totalSeconds += value * multiplier;
        }
        catch (std::exception const&)
        {
            error("sleep: invalid time interval '{}'", arg);
            return 1;
        }
    }

    // Sleep
    if (totalSeconds > 0)
    {
        auto const duration = std::chrono::duration<double>(totalSeconds);
        std::this_thread::sleep_for(duration);
    }

    return 0;
}

int Shell::executeInlineRm(CoreVM::CoreStringArray const& args, NativeHandle outputFd)
{
    auto writeOutput = [outputFd](std::string const& str) {
        [[maybe_unused]] auto written = platformWrite(outputFd, str.data(), str.size());
    };

    bool recursive = false;
    bool force = false;
    bool removeEmptyDirs = false;
    bool verbose = false;
    bool interactive = false;
    bool endOfOptions = false;
    std::vector<std::string> paths;

    for (size_t i = 1; i < args.size(); ++i)
    {
        std::string_view const arg = args.at(i);

        if (!endOfOptions && arg == "--")
        {
            endOfOptions = true;
            continue;
        }

        if (!endOfOptions && arg == "--help")
        {
            writeOutput("Usage: rm [OPTION]... [FILE]...\n");
            writeOutput("Remove (unlink) the FILE(s).\n");
            writeOutput("\n");
            writeOutput("Options:\n");
            writeOutput("  -f, --force       ignore nonexistent files, never prompt\n");
            writeOutput("  -i                prompt before every removal\n");
            writeOutput("  -r, -R, --recursive  remove directories and their contents recursively\n");
            writeOutput("  -d, --dir         remove empty directories\n");
            writeOutput("  -v, --verbose     explain what is being done\n");
            writeOutput("      --help        display this help and exit\n");
            return 0;
        }

        if (!endOfOptions && arg == "--recursive")
        {
            recursive = true;
            continue;
        }
        if (!endOfOptions && arg == "--force")
        {
            force = true;
            continue;
        }
        if (!endOfOptions && arg == "--dir")
        {
            removeEmptyDirs = true;
            continue;
        }
        if (!endOfOptions && arg == "--verbose")
        {
            verbose = true;
            continue;
        }

        // Parse combined short flags: -rf, -rv, etc.
        if (!endOfOptions && arg.size() > 1 && arg[0] == '-' && arg[1] != '-')
        {
            bool validFlags = true;
            for (size_t j = 1; j < arg.size(); ++j)
            {
                switch (arg[j])
                {
                    case 'r':
                    case 'R': recursive = true; break;
                    case 'f': force = true; break;
                    case 'd': removeEmptyDirs = true; break;
                    case 'v': verbose = true; break;
                    case 'i': interactive = true; break;
                    default: validFlags = false; break;
                }
                if (!validFlags)
                    break;
            }
            if (validFlags)
                continue;
        }

        paths.emplace_back(arg);
    }

    if (paths.empty())
    {
        if (!force)
        {
            error("rm: missing operand");
            return 1;
        }
        return 0;
    }

    bool success = true;
    for (auto const& path: paths)
    {
        namespace fs = std::filesystem;

        // Preserve root: reject "/"
        auto const canonical = fs::path(path).lexically_normal();
        if (canonical == "/" || canonical == "//" || canonical.string() == "\\")
        {
            error("rm: it is dangerous to operate recursively on '/'");
            success = false;
            continue;
        }

        // Reject paths ending in . or ..
        auto const filename = canonical.filename().string();
        if (filename == "." || filename == "..")
        {
            error("rm: refusing to remove '.' or '..' directory: skipping '{}'", path);
            success = false;
            continue;
        }

        std::error_code ec;
        auto const status = fs::symlink_status(path, ec);
        if (ec || !fs::exists(status))
        {
            if (!force)
            {
                error("rm: cannot remove '{}': No such file or directory", path);
                success = false;
            }
            continue;
        }

        // Interactive prompt
        if (interactive && !force)
        {
            std::string prompt;
            if (fs::is_directory(status))
                prompt = std::format("rm: remove directory '{}'? ", path);
            else
                prompt = std::format("rm: remove file '{}'? ", path);
            // Write prompt to stderr, read response from stdin
            [[maybe_unused]] auto w = platformWrite(standardError(), prompt.data(), prompt.size());
            // In non-interactive/test contexts, skip (treat as 'no')
            if (!_tty.isTerminal())
                continue;
            std::string response;
            std::getline(std::cin, response);
            if (response.empty() || (response[0] != 'y' && response[0] != 'Y'))
                continue;
        }

        if (fs::is_directory(status) && !fs::is_symlink(status))
        {
            if (recursive)
            {
                if (verbose)
                {
                    // Manual recursive traversal to print each entry (matches GNU coreutils rm -vr)
                    auto entries = std::vector<fs::path> {};
                    for (auto const& entry: fs::recursive_directory_iterator(
                             path, fs::directory_options::skip_permission_denied, ec))
                    {
                        if (ec)
                            break;
                        entries.push_back(entry.path());
                    }
                    if (ec)
                    {
                        error("rm: cannot remove '{}': {}", path, ec.message());
                        success = false;
                    }
                    else
                    {
                        // Reverse for leaf-to-root removal order
                        std::ranges::reverse(entries);
                        auto allOk = true;
                        for (auto const& entry: entries)
                        {
                            if (!fs::remove(entry, ec) || ec)
                            {
                                error("rm: cannot remove '{}': {}", entry.string(), ec.message());
                                allOk = false;
                                break;
                            }
                            writeOutput(std::format("removed '{}'\n", entry.string()));
                        }
                        if (!allOk)
                        {
                            success = false;
                        }
                        else if (!fs::remove(path, ec) || ec)
                        {
                            error("rm: cannot remove '{}': {}", path, ec.message());
                            success = false;
                        }
                        else
                        {
                            writeOutput(std::format("removed '{}'\n", path));
                        }
                    }
                }
                else
                {
                    auto const count = fs::remove_all(path, ec);
                    if (ec)
                    {
                        error("rm: cannot remove '{}': {}", path, ec.message());
                        success = false;
                    }
                    (void) count;
                }
            }
            else if (removeEmptyDirs)
            {
                if (!fs::remove(path, ec) || ec)
                {
                    error("rm: cannot remove '{}': {}", path, ec ? ec.message() : "Directory not empty");
                    success = false;
                }
                else if (verbose)
                {
                    writeOutput(std::format("removed '{}'\n", path));
                }
            }
            else
            {
                error("rm: cannot remove '{}': Is a directory", path);
                success = false;
            }
        }
        else
        {
            if (!fs::remove(path, ec) || ec)
            {
                error("rm: cannot remove '{}': {}", path, ec ? ec.message() : "Unknown error");
                success = false;
            }
            else if (verbose)
            {
                writeOutput(std::format("removed '{}'\n", path));
            }
        }
    }

    return success ? 0 : 1;
}

int Shell::executeInlineMkdir(CoreVM::CoreStringArray const& args, NativeHandle outputFd)
{
    auto writeOutput = [outputFd](std::string const& str) {
        [[maybe_unused]] auto written = platformWrite(outputFd, str.data(), str.size());
    };

    bool parents = false;
    bool verbose = false;
    bool endOfOptions = false;
    std::vector<std::string> paths;

    for (size_t i = 1; i < args.size(); ++i)
    {
        std::string_view const arg = args.at(i);

        if (!endOfOptions && arg == "--")
        {
            endOfOptions = true;
            continue;
        }

        if (!endOfOptions && arg == "--help")
        {
            writeOutput("Usage: mkdir [OPTION]... DIRECTORY...\n");
            writeOutput("Create the DIRECTORY(ies), if they do not already exist.\n");
            writeOutput("\n");
            writeOutput("Options:\n");
            writeOutput("  -p, --parents     make parent directories as needed\n");
            writeOutput("  -v, --verbose     print a message for each created directory\n");
            writeOutput("      --help        display this help and exit\n");
            return 0;
        }

        if (!endOfOptions && arg == "--parents")
        {
            parents = true;
            continue;
        }
        if (!endOfOptions && arg == "--verbose")
        {
            verbose = true;
            continue;
        }

        // Parse combined short flags: -pv, -vp, etc.
        if (!endOfOptions && arg.size() > 1 && arg[0] == '-' && arg[1] != '-')
        {
            bool validFlags = true;
            for (size_t j = 1; j < arg.size(); ++j)
            {
                switch (arg[j])
                {
                    case 'p': parents = true; break;
                    case 'v': verbose = true; break;
                    default: validFlags = false; break;
                }
                if (!validFlags)
                    break;
            }
            if (validFlags)
                continue;
        }

        paths.emplace_back(arg);
    }

    if (paths.empty())
    {
        error("mkdir: missing operand");
        return 1;
    }

    bool success = true;
    for (auto const& path: paths)
    {
        namespace fs = std::filesystem;
        std::error_code ec;

        if (parents)
        {
            fs::create_directories(path, ec);
            if (ec)
            {
                error("mkdir: cannot create directory '{}': {}", path, ec.message());
                success = false;
                continue;
            }
            if (verbose && fs::exists(path))
                writeOutput(std::format("mkdir: created directory '{}'\n", path));
        }
        else
        {
            if (fs::exists(path, ec))
            {
                error("mkdir: cannot create directory '{}': File exists", path);
                success = false;
                continue;
            }
            if (!fs::create_directory(path, ec) || ec)
            {
                error("mkdir: cannot create directory '{}': {}",
                      path,
                      ec ? ec.message() : "No such file or directory");
                success = false;
                continue;
            }
            if (verbose)
                writeOutput(std::format("mkdir: created directory '{}'\n", path));
        }
    }

    return success ? 0 : 1;
}

void Shell::finalizePipelineBuiltin(bool lastInChain,
                                    CoreVM::CoreStringArray const& args,
                                    std::string_view programName,
                                    CoreVM::Params& context)
{
    if (!lastInChain)
        _currentPipelineBuilder.closeCurrentPipeWriter();

    // Track command for job table
    std::string cmdString(programName);
    for (size_t i = 1; i < args.size(); ++i)
    {
        cmdString += ' ';
        cmdString += args.at(i);
    }
    _pipelineCommands.push_back(std::move(cmdString));

    if (lastInChain)
    {
        // Wait for downstream processes to complete
        for (ProcessId const processPid: _currentProcessGroupPids)
        {
            auto const waitResult = _processManager.wait(processPid);
            if (waitResult.has_value())
                _exitCode = waitResult->exitCode;
        }
        _currentProcessGroupPids.clear();
        _pipelineCommands.clear();

        // Reclaim terminal control
        auto const setFgResult = _processManager.setForegroundPgrp(_tty.inputFd(), _shellPgid);
        if (!setFgResult)
        {
            // Log failure (debugLog not accessible here, use same pattern as other builtins)
        }
    }

    context.setResult(CoreVM::CoreNumber(_exitCode));
}

} // namespace endo
