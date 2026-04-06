// SPDX-License-Identifier: Apache-2.0
#include <shell/Shell.hpp>
#include <shell/commands/FindExpression.hpp>
#include <shell/commands/GrepCommand.hpp>
#include <shell/commands/KillCommand.hpp>
#include <shell/commands/TimeoutCommand.hpp>

#include <tui/GenericSyntaxHighlighter.hpp>
#include <tui/ImageLoader.hpp>
#include <tui/MarkdownRenderer.hpp>
#include <tui/Sixel.hpp>
#include <tui/TerminalOutput.hpp>
#include <tui/Theme.hpp>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <random>
#include <ranges>
#include <span>
#include <sstream>
#include <thread>
#include <utility>

#include <fcntl.h>

#include <platform/Process.hpp>
#include <platform/SignalHandler.hpp>
#include <platform/Types.hpp>

#if defined(_WIN32)
    #include <io.h>
    #include <windows.h>
    #define isatty    _isatty
    #define STDOUT_FD 1
#else
    #include <sys/utsname.h>

    #include <poll.h>
    #include <pwd.h>
    #include <unistd.h>
    #define STDOUT_FD STDOUT_FILENO
#endif

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
                        value = (value * 8) + (input[i] - '0');
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
                            value = (value * 16) + (c - '0');
                        else if (c >= 'a' && c <= 'f')
                            value = (value * 16) + (c - 'a' + 10);
                        else if (c >= 'A' && c <= 'F')
                            value = (value * 16) + (c - 'A' + 10);
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

/// Parses a range specification of the form "START..END", "START..", or "..END".
/// Returns a pair of optional ints representing the start and end of the range.
/// At least one side must be specified, values must be >= 1, and START <= END.
std::expected<std::pair<std::optional<int>, std::optional<int>>, std::string> parseRange(
    std::string_view range)
{
    auto const dotdot = range.find("..");
    if (dotdot == std::string_view::npos)
        return std::unexpected("invalid range format, expected START..END");

    auto const startStr = range.substr(0, dotdot);
    auto const endStr = range.substr(dotdot + 2);

    if (startStr.empty() && endStr.empty())
        return std::unexpected("empty range, expected at least START or END");

    std::optional<int> rangeStart;
    std::optional<int> rangeEnd;

    if (!startStr.empty())
    {
        int value = 0;
        auto const [ptr, ec] = std::from_chars(startStr.data(), startStr.data() + startStr.size(), value);
        if (ec != std::errc {} || ptr != startStr.data() + startStr.size())
            return std::unexpected(std::format("invalid start value '{}'", startStr));
        if (value < 1)
            return std::unexpected("start value must be >= 1");
        rangeStart = value;
    }

    if (!endStr.empty())
    {
        int value = 0;
        auto const [ptr, ec] = std::from_chars(endStr.data(), endStr.data() + endStr.size(), value);
        if (ec != std::errc {} || ptr != endStr.data() + endStr.size())
            return std::unexpected(std::format("invalid end value '{}'", endStr));
        if (value < 1)
            return std::unexpected("end value must be >= 1");
        rangeEnd = value;
    }

    if (rangeStart && rangeEnd && *rangeStart > *rangeEnd)
        return std::unexpected(std::format("start ({}) must be <= end ({})", *rangeStart, *rangeEnd));

    return std::pair { rangeStart, rangeEnd };
}

} // namespace

namespace endo
{

int Shell::renderMarkdownHelp(NativeHandle outputFd, std::string_view markdownContent)
{
    if (outputFd == standardOutput() && isTerminal(standardOutput()))
    {
        tui::TerminalOutput termOutput;
        termOutput.updateDimensions();
        tui::MarkdownRenderer renderer(termOutput);
        renderer.setMaxWidth(termOutput.columns());
        renderer.render(markdownContent);
        termOutput.flush();
        return 0;
    }
    [[maybe_unused]] auto written = platformWrite(outputFd, markdownContent.data(), markdownContent.size());
    written = platformWrite(outputFd, "\n", 1);
    return 0;
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
int Shell::executeInlineEcho(CoreVM::CoreStringArray const& args, NativeHandle outputFd)
{
    std::vector<std::string> echoArgs;
    for (auto const i: std::views::iota(1uz, args.size()))
        echoArgs.push_back(args.at(i));

    // Check for --help before flag parsing
    for (auto const& arg: echoArgs)
    {
        if (arg == "--help" || arg == "-h")
            return renderMarkdownHelp(outputFd,
                                      "# echo\n"
                                      "\n"
                                      "Write arguments to standard output.\n"
                                      "\n"
                                      "## Usage\n"
                                      "\n"
                                      "`echo [OPTION]... [STRING]...`\n"
                                      "\n"
                                      "## Options\n"
                                      "\n"
                                      "| Option | Description |\n"
                                      "|--------|-------------|\n"
                                      "| `-n` | Do not output the trailing newline |\n"
                                      "| `-e` | Enable interpretation of backslash escapes |\n"
                                      "| `--help` | Display this help |\n"
                                      "\n"
                                      "## Escape Sequences (with `-e`)\n"
                                      "\n"
                                      "| Sequence | Meaning |\n"
                                      "|----------|----------|\n"
                                      "| `\\\\` | Backslash |\n"
                                      "| `\\a` | Alert (bell) |\n"
                                      "| `\\b` | Backspace |\n"
                                      "| `\\e` | Escape character |\n"
                                      "| `\\f` | Form feed |\n"
                                      "| `\\n` | Newline |\n"
                                      "| `\\r` | Carriage return |\n"
                                      "| `\\t` | Horizontal tab |\n"
                                      "| `\\0NNN` | Octal value NNN (1 to 3 digits) |\n"
                                      "| `\\xHH` | Hexadecimal value HH (1 to 2 digits) |\n");
    }

    bool suppressNewline = false;
    bool interpretEscapes = false;
    size_t argStart = 0;

    // Parse flags
    for (auto const i: std::views::iota(0uz, echoArgs.size()))
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
            for (auto const j: std::views::iota(1uz, arg.size()))
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
    for (auto const i: std::views::iota(argStart, echoArgs.size()))
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
    for (auto const i: std::views::iota(1uz, args.size()))
        catArgs.push_back(args.at(i));

    // Parse flags
    bool numberLines = false;
    bool numberNonBlank = false;
    bool squeezeBlank = false;
    bool showEnds = false;
    bool showTabs = false;
    bool showHelp = false;
    bool rawMode = false;
    std::optional<int> rangeStart;
    std::optional<int> rangeEnd;
    bool rangeSpecified = false;
    std::optional<int> imageColumns;
    std::optional<int> imageRows;
    std::vector<std::string> files;

    for (size_t i = 0; i < catArgs.size(); ++i)
    {
        std::string_view arg = catArgs[i];

        if (arg == "--")
        {
            for (auto const j: std::views::iota(i + 1, catArgs.size()))
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
        if (arg == "--raw")
        {
            rawMode = true;
            continue;
        }
        if (arg == "--columns" || arg.starts_with("--columns="))
        {
            std::string_view colValue;
            if (arg == "--columns")
            {
                if (i + 1 >= catArgs.size())
                {
                    error("cat: --columns requires a value");
                    return 1;
                }
                colValue = catArgs[++i];
            }
            else
            {
                colValue = arg.substr(10); // skip "--columns="
            }
            int value = 0;
            auto const [ptr, ec] = std::from_chars(colValue.data(), colValue.data() + colValue.size(), value);
            if (ec != std::errc {} || ptr != colValue.data() + colValue.size() || value < 1)
            {
                error("cat: --columns: invalid value '{}'", colValue);
                return 1;
            }
            imageColumns = value;
            continue;
        }
        if (arg == "--rows" || arg.starts_with("--rows="))
        {
            std::string_view rowValue;
            if (arg == "--rows")
            {
                if (i + 1 >= catArgs.size())
                {
                    error("cat: --rows requires a value");
                    return 1;
                }
                rowValue = catArgs[++i];
            }
            else
            {
                rowValue = arg.substr(7); // skip "--rows="
            }
            int value = 0;
            auto const [ptr, ec] = std::from_chars(rowValue.data(), rowValue.data() + rowValue.size(), value);
            if (ec != std::errc {} || ptr != rowValue.data() + rowValue.size() || value < 1)
            {
                error("cat: --rows: invalid value '{}'", rowValue);
                return 1;
            }
            imageRows = value;
            continue;
        }
        if (arg == "--range" || arg.starts_with("--range="))
        {
            std::string_view rangeValue;
            if (arg == "--range")
            {
                if (i + 1 >= catArgs.size())
                {
                    error("cat: --range requires a value");
                    return 1;
                }
                rangeValue = catArgs[++i];
            }
            else
            {
                rangeValue = arg.substr(8); // skip "--range="
            }
            auto const parsed = parseRange(rangeValue);
            if (!parsed.has_value())
            {
                error("cat: --range: {}", parsed.error());
                return 1;
            }
            rangeStart = parsed->first;
            rangeEnd = parsed->second;
            rangeSpecified = true;
            continue;
        }

        if (arg.starts_with("-") && arg.size() > 1 && arg[1] != '-')
        {
            bool validFlag = true;
            for (auto const j: std::views::iota(1uz, arg.size()))
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
                    case 'c': {
                        // -c must be last in the flag cluster since it takes an argument
                        if (j + 1 != arg.size())
                        {
                            error("cat: -c must be last in combined flags (takes an argument)");
                            return 1;
                        }
                        if (i + 1 >= catArgs.size())
                        {
                            error("cat: -c requires a value");
                            return 1;
                        }
                        int value = 0;
                        auto const [ptr, ec] = std::from_chars(
                            catArgs[i + 1].data(), catArgs[i + 1].data() + catArgs[i + 1].size(), value);
                        if (ec != std::errc {} || ptr != catArgs[i + 1].data() + catArgs[i + 1].size()
                            || value < 1)
                        {
                            error("cat: -c: invalid value '{}'", catArgs[i + 1]);
                            return 1;
                        }
                        ++i;
                        imageColumns = value;
                        break;
                    }
                    case 'R': {
                        // -R must be last in the flag cluster since it takes an argument
                        if (j + 1 != arg.size())
                        {
                            error("cat: -R must be last in combined flags (takes an argument)");
                            return 1;
                        }
                        if (i + 1 >= catArgs.size())
                        {
                            error("cat: -R requires a value");
                            return 1;
                        }
                        int value = 0;
                        auto const [ptr, ec] = std::from_chars(
                            catArgs[i + 1].data(), catArgs[i + 1].data() + catArgs[i + 1].size(), value);
                        if (ec != std::errc {} || ptr != catArgs[i + 1].data() + catArgs[i + 1].size()
                            || value < 1)
                        {
                            error("cat: -R: invalid value '{}'", catArgs[i + 1]);
                            return 1;
                        }
                        ++i;
                        imageRows = value;
                        break;
                    }
                    case 'r': {
                        // -r must be last in the flag cluster since it takes an argument
                        if (j + 1 != arg.size())
                        {
                            error("cat: -r must be last in combined flags (takes an argument)");
                            return 1;
                        }
                        if (i + 1 >= catArgs.size())
                        {
                            error("cat: -r requires a value");
                            return 1;
                        }
                        auto const parsed = parseRange(catArgs[++i]);
                        if (!parsed.has_value())
                        {
                            error("cat: -r: {}", parsed.error());
                            return 1;
                        }
                        rangeStart = parsed->first;
                        rangeEnd = parsed->second;
                        rangeSpecified = true;
                        break;
                    }
                    default: validFlag = false; break;
                }
                if (!validFlag)
                    break;
            }
            if (validFlag)
                continue;
        }

        files.emplace_back(arg);
    }

    // -b overrides -n
    if (numberNonBlank)
        numberLines = false;

    auto writeOutput = [outputFd](std::string const& str) {
        [[maybe_unused]] auto written = platformWrite(outputFd, str.data(), str.size());
    };

    if (showHelp)
    {
        return renderMarkdownHelp(
            outputFd,
            "# cat\n"
            "\n"
            "Concatenate FILE(s) to standard output.\n"
            "\n"
            "## Usage\n"
            "\n"
            "`cat [OPTION]... [FILE]...`\n"
            "\n"
            "With no FILE, or when FILE is `-`, read standard input.\n"
            "\n"
            "## Options\n"
            "\n"
            "| Option | Description |\n"
            "|--------|-------------|\n"
            "| `-n`, `--number` | Number all output lines |\n"
            "| `-b`, `--number-nonblank` | Number non-blank output lines (overrides `-n`) |\n"
            "| `-s`, `--squeeze-blank` | Suppress repeated empty output lines |\n"
            "| `-E`, `--show-ends` | Display `$` at end of each line |\n"
            "| `-T`, `--show-tabs` | Display TAB characters as `^I` |\n"
            "| `-A`, `--show-all` | Equivalent to `-ET` |\n"
            "| `-r`, `--range START..END` | Show only lines in the given range |\n"
            "| `-c`, `--columns N` | Target image width in terminal columns |\n"
            "| `-R`, `--rows N` | Target image height in terminal rows |\n"
            "| `--raw` | Disable inline image rendering |\n"
            "| `-h`, `--help` | Display this help |\n");
    }

    // Detect whether output goes to a TTY for syntax highlighting
    bool const outputIsTty = isTerminal(outputFd);

    int lineNumber = 1;
    bool lastLineWasBlank = false;

    auto processContent = [&](std::string const& content, tui::LanguageId language) {
        auto highlightState = tui::HighlightState::Normal;
        auto const& theme = tui::currentTheme();
        auto const highlight = outputIsTty && language != tui::LanguageId::None;
        int physicalLineNumber = 0;

        std::string line;
        for (auto const i: std::views::iota(0uz, content.size()))
        {
            char c = content[i];
            if (c == '\n')
            {
                ++physicalLineNumber;
                bool isBlank = line.empty();

                if (squeezeBlank && isBlank && lastLineWasBlank)
                {
                    line.clear();
                    continue;
                }
                lastLineWasBlank = isBlank;

                // Range filtering
                if (rangeSpecified)
                {
                    if (rangeEnd && physicalLineNumber > *rangeEnd)
                        return;
                    if (rangeStart && physicalLineNumber < *rangeStart)
                    {
                        line.clear();
                        continue;
                    }
                }

                // Apply syntax highlighting before flag processing
                auto displayLine = std::string {};
                if (highlight && !isBlank)
                {
                    auto [highlights, nextState] = tui::highlightLine(line, language, highlightState);
                    highlightState = nextState;
                    displayLine = tui::renderHighlightedLineToString(line, highlights, theme);
                }
                else
                {
                    displayLine = line;
                }

                if (showTabs)
                {
                    std::string processed;
                    for (char ch: displayLine)
                    {
                        if (ch == '\t')
                            processed += "^I";
                        else
                            processed += ch;
                    }
                    displayLine = std::move(processed);
                }

                std::string output;
                if (numberNonBlank && !isBlank)
                    output = std::format("{:>6}\t", rangeSpecified ? physicalLineNumber : lineNumber++);
                else if (numberLines)
                    output = std::format("{:>6}\t", rangeSpecified ? physicalLineNumber : lineNumber++);
                output += displayLine;
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
            ++physicalLineNumber;

            // Range filtering for trailing line
            if (rangeSpecified)
            {
                if (rangeEnd && physicalLineNumber > *rangeEnd)
                    return;
                if (rangeStart && physicalLineNumber < *rangeStart)
                    return;
            }

            auto displayLine = std::string {};
            if (highlight)
            {
                auto [highlights, nextState] = tui::highlightLine(line, language, highlightState);
                displayLine = tui::renderHighlightedLineToString(line, highlights, theme);
            }
            else
            {
                displayLine = line;
            }

            if (showTabs)
            {
                std::string processed;
                for (char ch: displayLine)
                {
                    if (ch == '\t')
                        processed += "^I";
                    else
                        processed += ch;
                }
                displayLine = std::move(processed);
            }

            std::string output;
            if (numberNonBlank && !line.empty())
                output = std::format("{:>6}\t", rangeSpecified ? physicalLineNumber : lineNumber++);
            else if (numberLines)
                output = std::format("{:>6}\t", rangeSpecified ? physicalLineNumber : lineNumber++);
            output += displayLine;
            writeOutput(output);
        }
    };

    auto readFromFd = [](NativeHandle fd) -> std::string {
        std::string content;
        char buffer[4096];
        intptr_t bytesRead = 0;
        while ((bytesRead = platformRead(fd, buffer, sizeof(buffer))) > 0)
            content.append(buffer, static_cast<size_t>(bytesRead));
        return content;
    };

    bool success = true;

    if (files.empty())
        files.emplace_back("-");

    for (auto const& file: files)
    {
        if (file == "-")
        {
            auto const content = readFromFd(stdinFd);
            processContent(content, tui::LanguageId::None);
        }
        else
        {
            // Check for image file rendering via sixel
            auto const ext = std::filesystem::path(file).extension().string();
            auto const forceImage = imageColumns.has_value() || imageRows.has_value();
            if (tui::isImageExtension(ext) && (outputIsTty || forceImage) && !rawMode)
            {
                auto imageResult = tui::loadImage(file);
                if (!imageResult.has_value())
                {
                    error("cat: {}: {}", file, imageResult.error());
                    success = false;
                    continue;
                }

                auto& image = imageResult.value();

                // Determine target pixel dimensions
                auto const termSize = _tty.getSize();
                auto const termCols = termSize.has_value() ? termSize->cols : uint16_t { 80 };
                auto const termXpixel = termSize.has_value() ? termSize->xpixel : uint16_t { 0 };
                auto const termYpixel = termSize.has_value() ? termSize->ypixel : uint16_t { 0 };
                auto const termRows = termSize.has_value() ? termSize->rows : uint16_t { 25 };

                // Cell pixel size (fallback: 8x16 if pixel dims unavailable)
                auto const cellWidth = termXpixel > 0 ? termXpixel / termCols : 8;
                auto const cellHeight = termYpixel > 0 ? termYpixel / termRows : 16;

                auto targetPixelWidth = 0;
                auto targetPixelHeight = 0;

                if (imageColumns.has_value())
                    targetPixelWidth = *imageColumns * cellWidth;
                if (imageRows.has_value())
                    targetPixelHeight = *imageRows * cellHeight;

                // Auto-sizing: fit to terminal width if no explicit size given
                if (!imageColumns.has_value() && !imageRows.has_value())
                {
                    auto const maxPixelWidth = static_cast<int>(termCols) * cellWidth;
                    if (image.width > maxPixelWidth)
                    {
                        targetPixelWidth = maxPixelWidth;
                        targetPixelHeight = 0; // auto from aspect ratio
                    }
                }

                // Resize if target differs from source
                if (targetPixelWidth > 0 || targetPixelHeight > 0)
                {
                    auto const tw = targetPixelWidth > 0 ? targetPixelWidth : 0;
                    auto const th = targetPixelHeight > 0 ? targetPixelHeight : 0;
                    if (tw != image.width || th != image.height)
                    {
                        auto resized = tui::resizeImage(image, tw, th);
                        if (!resized.has_value())
                        {
                            error("cat: {}: {}", file, resized.error());
                            success = false;
                            continue;
                        }
                        image = std::move(resized.value());
                    }
                }

                // Encode as sixel
                auto const imageData =
                    tui::ImageData { .pixels = image.pixels, .width = image.width, .height = image.height };
                auto sixelResult = tui::encodeSixel(imageData, 256);
                if (!sixelResult.has_value())
                {
                    error("cat: {}: sixel encode failed: {}", file, sixelResult.error());
                    success = false;
                    continue;
                }

                // Write DCS-framed sixel sequence
                auto const sixelOutput = std::format("\033P0;1q{}\033\\", sixelResult.value());
                [[maybe_unused]] auto written =
                    platformWrite(outputFd, sixelOutput.data(), sixelOutput.size());

                // Trailing newline
                writeOutput("\n");
                continue;
            }

            auto const result = _processManager.openFile(file, O_RDONLY);
            if (!result.has_value())
            {
                error("cat: {}: {}", file, strerror(errno));
                success = false;
                continue;
            }
            auto const fd = result.value();
            auto const content = readFromFd(fd);
            _processManager.closeHandle(fd);

            // Detect binary content (non-image files)
            if (!rawMode && outputIsTty)
            {
                if (content.find('\0') != std::string::npos)
                {
                    error("cat: {}: binary file (use --raw to force output)", file);
                    success = false;
                    continue;
                }
            }

            auto const language = tui::detectLanguageFromPath(file);
            processContent(content, language);
        }
    }

    return success ? 0 : 1;
}

int Shell::executeInlineSleep(CoreVM::CoreStringArray const& args, NativeHandle outputFd)
{
    std::vector<std::string> sleepArgs;
    for (auto const i: std::views::iota(1uz, args.size()))
        sleepArgs.push_back(args.at(i));

    // Check for help
    for (auto const& arg: sleepArgs)
    {
        if (arg == "-h" || arg == "--help")
        {
            return renderMarkdownHelp(outputFd,
                                      "# sleep\n"
                                      "\n"
                                      "Pause for NUMBER seconds.\n"
                                      "\n"
                                      "## Usage\n"
                                      "\n"
                                      "`sleep NUMBER[SUFFIX]...`\n"
                                      "\n"
                                      "## Suffixes\n"
                                      "\n"
                                      "| Suffix | Meaning |\n"
                                      "|--------|----------|\n"
                                      "| `s` | Seconds (default) |\n"
                                      "| `m` | Minutes |\n"
                                      "| `h` | Hours |\n"
                                      "| `d` | Days |\n"
                                      "\n"
                                      "Multiple arguments are summed together.\n"
                                      "NUMBER may be an integer or floating-point number.\n");
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

    // Sleep in short intervals to allow Ctrl+C (SIGINT) interruption
    if (totalSeconds > 0)
    {
        SignalHandler::clearPendingSigint();
        auto const deadline = std::chrono::steady_clock::now() + std::chrono::duration<double>(totalSeconds);
        while (std::chrono::steady_clock::now() < deadline)
        {
            // Drain any pending signals from signalfd (Linux) so SIGINT flag gets set
            SignalHandler::processSignalFd();
            if (SignalHandler::hasPendingSigint())
            {
                SignalHandler::clearPendingSigint();
                return 130; // 128 + SIGINT(2)
            }
            auto const remaining = deadline - std::chrono::steady_clock::now();
            auto const maxInterval = std::chrono::milliseconds(100);
            if (remaining > maxInterval)
                std::this_thread::sleep_for(maxInterval);
            else if (remaining > std::chrono::steady_clock::duration::zero())
                std::this_thread::sleep_for(remaining);
        }
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

    for (auto const i: std::views::iota(1uz, args.size()))
    {
        std::string_view const arg = args.at(i);

        if (!endOfOptions && arg == "--")
        {
            endOfOptions = true;
            continue;
        }

        if (!endOfOptions && (arg == "--help" || arg == "-h"))
        {
            return renderMarkdownHelp(
                outputFd,
                "# rm\n"
                "\n"
                "Remove (unlink) the FILE(s).\n"
                "\n"
                "## Usage\n"
                "\n"
                "`rm [OPTION]... [FILE]...`\n"
                "\n"
                "## Options\n"
                "\n"
                "| Option | Description |\n"
                "|--------|-------------|\n"
                "| `-f`, `--force` | Ignore nonexistent files, never prompt |\n"
                "| `-i` | Prompt before every removal |\n"
                "| `-r`, `-R`, `--recursive` | Remove directories and their contents recursively |\n"
                "| `-d`, `--dir` | Remove empty directories |\n"
                "| `-v`, `--verbose` | Explain what is being done |\n"
                "| `--help` | Display this help |\n");
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
            for (auto const j: std::views::iota(1uz, arg.size()))
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
        // Preserve root: reject "/"
        auto const canonical = std::filesystem::path(path).lexically_normal();
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

        if (!_fs.exists(path))
        {
            if (!force)
            {
                error("rm: cannot remove '{}': No such file or directory", path);
                success = false;
            }
            continue;
        }

        auto const pathIsDirectory = _fs.isDirectory(path);
        auto const pathIsSymlink = _fs.isSymlink(path);

        // Interactive prompt
        if (interactive && !force)
        {
            std::string prompt;
            if (pathIsDirectory)
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

        if (pathIsDirectory && !pathIsSymlink)
        {
            if (recursive)
            {
                if (verbose)
                {
                    // Manual recursive traversal to print each entry (matches GNU coreutils rm -vr)
                    auto const listResult = _fs.listDirectoryRecursive(path);
                    if (!listResult.has_value())
                    {
                        error("rm: cannot remove '{}': {}", path, listResult.error());
                        success = false;
                    }
                    else
                    {
                        // Collect paths and reverse for leaf-to-root removal order
                        auto entries = std::vector<std::filesystem::path> {};
                        for (auto const& entry: listResult.value())
                            entries.push_back(entry.path);
                        std::ranges::reverse(entries);
                        auto allOk = true;
                        for (auto const& entry: entries)
                        {
                            auto const removeResult = _fs.remove(entry);
                            if (!removeResult.has_value() || !removeResult.value())
                            {
                                error("rm: cannot remove '{}': {}",
                                      entry.string(),
                                      removeResult.has_value() ? "Unknown error" : removeResult.error());
                                allOk = false;
                                break;
                            }
                            writeOutput(std::format("removed '{}'\n", entry.string()));
                        }
                        if (!allOk)
                        {
                            success = false;
                        }
                        else
                        {
                            auto const removeResult = _fs.remove(path);
                            if (!removeResult.has_value() || !removeResult.value())
                            {
                                error("rm: cannot remove '{}': {}",
                                      path,
                                      removeResult.has_value() ? "Unknown error" : removeResult.error());
                                success = false;
                            }
                            else
                            {
                                writeOutput(std::format("removed '{}'\n", path));
                            }
                        }
                    }
                }
                else
                {
                    auto const removeResult = _fs.removeAll(path);
                    if (!removeResult.has_value())
                    {
                        error("rm: cannot remove '{}': {}", path, removeResult.error());
                        success = false;
                    }
                }
            }
            else if (removeEmptyDirs)
            {
                auto const removeResult = _fs.remove(path);
                if (!removeResult.has_value() || !removeResult.value())
                {
                    error("rm: cannot remove '{}': {}",
                          path,
                          removeResult.has_value() ? "Directory not empty" : removeResult.error());
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
            auto const removeResult = _fs.remove(path);
            if (!removeResult.has_value() || !removeResult.value())
            {
                error("rm: cannot remove '{}': {}",
                      path,
                      removeResult.has_value() ? "Unknown error" : removeResult.error());
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

    for (auto const i: std::views::iota(1uz, args.size()))
    {
        std::string_view const arg = args.at(i);

        if (!endOfOptions && arg == "--")
        {
            endOfOptions = true;
            continue;
        }

        if (!endOfOptions && (arg == "--help" || arg == "-h"))
        {
            return renderMarkdownHelp(outputFd,
                                      "# mkdir\n"
                                      "\n"
                                      "Create the DIRECTORY(ies), if they do not already exist.\n"
                                      "\n"
                                      "## Usage\n"
                                      "\n"
                                      "`mkdir [OPTION]... DIRECTORY...`\n"
                                      "\n"
                                      "## Options\n"
                                      "\n"
                                      "| Option | Description |\n"
                                      "|--------|-------------|\n"
                                      "| `-p`, `--parents` | Make parent directories as needed |\n"
                                      "| `-v`, `--verbose` | Print a message for each created directory |\n"
                                      "| `--help` | Display this help |\n");
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
            for (auto const j: std::views::iota(1uz, arg.size()))
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
        if (parents)
        {
            auto const result = _fs.createDirectories(path);
            if (!result.has_value())
            {
                error("mkdir: cannot create directory '{}': {}", path, result.error());
                success = false;
                continue;
            }
            if (verbose && _fs.exists(path))
                writeOutput(std::format("mkdir: created directory '{}'\n", path));
        }
        else
        {
            if (_fs.exists(path))
            {
                error("mkdir: cannot create directory '{}': File exists", path);
                success = false;
                continue;
            }
            auto const result = _fs.createDirectory(path);
            if (!result.has_value())
            {
                error("mkdir: cannot create directory '{}': {}", path, result.error());
                success = false;
                continue;
            }
            if (verbose)
                writeOutput(std::format("mkdir: created directory '{}'\n", path));
        }
    }

    return success ? 0 : 1;
}

int Shell::executeInlineCp(CoreVM::CoreStringArray const& args, NativeHandle outputFd)
{
    auto writeOutput = [outputFd](std::string const& str) {
        [[maybe_unused]] auto written = platformWrite(outputFd, str.data(), str.size());
    };

    bool recursive = false;
    bool force = false;
    bool noClobber = false;
    bool verbose = false;
    bool endOfOptions = false;
    std::vector<std::string> paths;

    for (auto const i: std::views::iota(1uz, args.size()))
    {
        std::string_view const arg = args.at(i);

        if (!endOfOptions && arg == "--")
        {
            endOfOptions = true;
            continue;
        }

        if (!endOfOptions && (arg == "--help" || arg == "-h"))
        {
            return renderMarkdownHelp(outputFd,
                                      "# cp\n"
                                      "\n"
                                      "Copy SOURCE to DEST, or multiple SOURCE(s) to DIRECTORY.\n"
                                      "\n"
                                      "## Usage\n"
                                      "\n"
                                      "`cp [OPTION]... SOURCE... DEST`\n"
                                      "\n"
                                      "## Options\n"
                                      "\n"
                                      "| Option | Description |\n"
                                      "|--------|-------------|\n"
                                      "| `-r`, `-R`, `--recursive` | Copy directories recursively |\n"
                                      "| `-f`, `--force` | Force overwrite; remove destination if needed |\n"
                                      "| `-n`, `--no-clobber` | Do not overwrite existing files |\n"
                                      "| `-v`, `--verbose` | Explain what is being done |\n"
                                      "| `--help` | Display this help |\n");
        }

        if (!endOfOptions && arg == "--recursive")
        {
            recursive = true;
            continue;
        }
        if (!endOfOptions && arg == "--force")
        {
            force = true; // NOLINT(clang-analyzer-deadcode.DeadStores)
            noClobber = false;
            continue;
        }
        if (!endOfOptions && arg == "--no-clobber")
        {
            noClobber = true;
            force = false; // NOLINT(clang-analyzer-deadcode.DeadStores)
            continue;
        }
        if (!endOfOptions && arg == "--verbose")
        {
            verbose = true;
            continue;
        }

        // Parse combined short flags: -rv, -fn, etc.
        if (!endOfOptions && arg.size() > 1 && arg[0] == '-' && arg[1] != '-')
        {
            bool validFlags = true;
            for (auto const j: std::views::iota(1uz, arg.size()))
            {
                switch (arg[j])
                {
                    case 'r':
                    case 'R': recursive = true; break;
                    case 'f':
                        force = true; // NOLINT(clang-analyzer-deadcode.DeadStores)
                        noClobber = false;
                        break;
                    case 'n':
                        noClobber = true;
                        force = false; // NOLINT(clang-analyzer-deadcode.DeadStores)
                        break;
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
        error("cp: missing file operand");
        return 1;
    }

    if (paths.size() < 2)
    {
        error("cp: missing destination file operand after '{}'", paths.front());
        return 1;
    }

    auto const dest = std::filesystem::path(paths.back());
    auto const sources = std::span(paths.data(), paths.size() - 1);
    auto const destIsDir = _fs.isDirectory(dest);
    auto const overwrite = !noClobber;

    if (sources.size() > 1 && !destIsDir)
    {
        error("cp: target '{}' is not a directory", dest.string());
        return 1;
    }

    bool success = true;
    for (auto const& src: sources)
    {
        auto const srcPath = std::filesystem::path(src);

        if (!_fs.exists(srcPath))
        {
            error("cp: cannot stat '{}': No such file or directory", src);
            success = false;
            continue;
        }

        if (_fs.isDirectory(srcPath) && !recursive)
        {
            error("cp: -r not specified; omitting directory '{}'", src);
            success = false;
            continue;
        }

        auto const target = destIsDir ? dest / srcPath.filename() : dest;

        if (recursive && _fs.isDirectory(srcPath))
        {
            // Recursive directory copy: list all entries and copy individually
            auto const listResult = _fs.listDirectoryRecursive(srcPath);
            if (!listResult.has_value())
            {
                error("cp: cannot copy '{}': {}", src, listResult.error());
                success = false;
                continue;
            }

            // Create the top-level target directory
            if (auto const mkResult = _fs.createDirectories(target); !mkResult.has_value())
            {
                error("cp: cannot create directory '{}': {}", target.string(), mkResult.error());
                success = false;
                continue;
            }

            for (auto const& entry: listResult.value())
            {
                auto const relativePath = entry.path.lexically_relative(srcPath);
                auto const entryTarget = target / relativePath;

                if (entry.isDirectory)
                {
                    if (auto const mkResult = _fs.createDirectories(entryTarget); !mkResult.has_value())
                    {
                        error("cp: cannot create directory '{}': {}", entryTarget.string(), mkResult.error());
                        success = false;
                    }
                }
                else
                {
                    // Skip silently if no-clobber and target exists
                    if (noClobber && _fs.exists(entryTarget))
                        continue;

                    // Ensure parent directory exists
                    if (auto const mkResult = _fs.createDirectories(entryTarget.parent_path());
                        !mkResult.has_value())
                    {
                        error("cp: cannot create directory '{}': {}",
                              entryTarget.parent_path().string(),
                              mkResult.error());
                        success = false;
                        continue;
                    }
                    if (auto const cpResult = _fs.copyFile(entry.path, entryTarget, overwrite);
                        !cpResult.has_value())
                    {
                        error("cp: cannot copy '{}': {}", entry.path.string(), cpResult.error());
                        success = false;
                        continue;
                    }
                    if (verbose)
                        writeOutput(std::format("'{}' -> '{}'\n", entry.path.string(), entryTarget.string()));
                }
            }
        }
        else
        {
            // Single file copy -- skip silently if no-clobber and target exists
            if (noClobber && _fs.exists(target))
                continue;

            if (auto const cpResult = _fs.copyFile(srcPath, target, overwrite); !cpResult.has_value())
            {
                error("cp: cannot copy '{}' to '{}': {}", src, target.string(), cpResult.error());
                success = false;
                continue;
            }

            if (verbose)
                writeOutput(std::format("'{}' -> '{}'\n", src, target.string()));
        }
    }

    return success ? 0 : 1;
}

int Shell::executeInlineMv(CoreVM::CoreStringArray const& args, NativeHandle outputFd)
{
    auto writeOutput = [outputFd](std::string const& str) {
        [[maybe_unused]] auto written = platformWrite(outputFd, str.data(), str.size());
    };

    bool force = false;
    bool noClobber = false;
    bool verbose = false;
    bool interactive = false;
    bool endOfOptions = false;
    std::vector<std::string> paths;

    for (auto const i: std::views::iota(1uz, args.size()))
    {
        std::string_view const arg = args.at(i);

        if (!endOfOptions && arg == "--")
        {
            endOfOptions = true;
            continue;
        }

        if (!endOfOptions && (arg == "--help" || arg == "-h"))
        {
            return renderMarkdownHelp(outputFd,
                                      "# mv\n"
                                      "\n"
                                      "Move SOURCE to DEST, or multiple SOURCE(s) to DIRECTORY.\n"
                                      "\n"
                                      "## Usage\n"
                                      "\n"
                                      "`mv [OPTION]... SOURCE... DEST`\n"
                                      "\n"
                                      "## Options\n"
                                      "\n"
                                      "| Option | Description |\n"
                                      "|--------|-------------|\n"
                                      "| `-f`, `--force` | Do not prompt before overwriting |\n"
                                      "| `-n`, `--no-clobber` | Do not overwrite existing files |\n"
                                      "| `-v`, `--verbose` | Explain what is being done |\n"
                                      "| `-i`, `--interactive` | Prompt before overwrite |\n"
                                      "| `--help` | Display this help |\n");
        }

        if (!endOfOptions && arg == "--force")
        {
            force = true;
            noClobber = false;
            interactive = false;
            continue;
        }
        if (!endOfOptions && arg == "--no-clobber")
        {
            noClobber = true;
            force = false;
            interactive = false;
            continue;
        }
        if (!endOfOptions && arg == "--verbose")
        {
            verbose = true;
            continue;
        }
        if (!endOfOptions && arg == "--interactive")
        {
            interactive = true;
            force = false;
            noClobber = false;
            continue;
        }

        if (!endOfOptions && arg.size() > 1 && arg[0] == '-' && arg[1] != '-')
        {
            bool validFlags = true;
            for (auto const j: std::views::iota(1uz, arg.size()))
            {
                switch (arg[j])
                {
                    case 'f':
                        force = true;
                        noClobber = false;
                        interactive = false;
                        break;
                    case 'n':
                        noClobber = true;
                        force = false;
                        interactive = false;
                        break;
                    case 'v': verbose = true; break;
                    case 'i':
                        interactive = true;
                        force = false;
                        noClobber = false;
                        break;
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
        error("mv: missing file operand");
        return 1;
    }

    if (paths.size() == 1)
    {
        error("mv: missing destination file operand after '{}'", paths.front());
        return 1;
    }

    auto const dest = std::filesystem::path(paths.back());
    auto const sources = std::span(paths.data(), paths.size() - 1);
    auto const destIsDir = _fs.isDirectory(dest);

    if (sources.size() > 1 && !destIsDir)
    {
        error("mv: target '{}' is not a directory", dest.string());
        return 1;
    }

    auto success = true;
    for (auto const& src: sources)
    {
        auto const srcPath = std::filesystem::path(src);

        if (!_fs.exists(srcPath))
        {
            error("mv: cannot stat '{}': No such file or directory", src);
            success = false;
            continue;
        }

        auto const target = destIsDir ? dest / srcPath.filename() : dest;

        // Check if target already exists
        if (_fs.exists(target))
        {
            if (noClobber)
                continue;

            if (interactive && !force)
            {
                auto const prompt = std::format("mv: overwrite '{}'? ", target.string());
                [[maybe_unused]] auto w = platformWrite(standardError(), prompt.data(), prompt.size());
                if (!_tty.isTerminal())
                    continue;
                std::string response;
                std::getline(std::cin, response);
                if (response.empty() || (response[0] != 'y' && response[0] != 'Y'))
                    continue;
            }
        }

        // Attempt rename (fast path: same filesystem)
        auto const renameResult = _fs.rename(srcPath, target);
        if (!renameResult.has_value())
        {
            // Only fall back to copy+remove for cross-device errors
            auto const& renameError = renameResult.error();
            auto const isCrossDevice = renameError.find("cross-device") != std::string::npos
                                       || renameError.find("Cross-device") != std::string::npos
                                       || renameError.find("EXDEV") != std::string::npos;
            if (!isCrossDevice)
            {
                error("mv: cannot move '{}' to '{}': {}", src, target.string(), renameError);
                success = false;
                continue;
            }

            // Cross-device move: fallback to recursive copy + remove
            std::string copyError;
            auto copyFailed = false;
            if (_fs.isDirectory(srcPath))
            {
                // Recursive directory copy
                if (auto const mkResult = _fs.createDirectories(target); !mkResult.has_value())
                {
                    error("mv: cannot move '{}' to '{}': {}", src, target.string(), mkResult.error());
                    success = false;
                    continue;
                }
                auto const listResult = _fs.listDirectoryRecursive(srcPath);
                if (!listResult.has_value())
                {
                    error("mv: cannot move '{}' to '{}': {}", src, target.string(), listResult.error());
                    success = false;
                    continue;
                }
                for (auto const& entry: listResult.value())
                {
                    auto const relativePath = entry.path.lexically_relative(srcPath);
                    auto const entryTarget = target / relativePath;
                    if (entry.isDirectory)
                    {
                        if (auto const mkResult = _fs.createDirectories(entryTarget); !mkResult.has_value())
                        {
                            copyFailed = true;
                            copyError = mkResult.error();
                            break;
                        }
                    }
                    else
                    {
                        auto const mkResult = _fs.createDirectories(entryTarget.parent_path());
                        if (!mkResult.has_value())
                        {
                            copyFailed = true;
                            copyError = mkResult.error();
                            break;
                        }
                        if (auto const cpResult = _fs.copyFile(entry.path, entryTarget, true);
                            !cpResult.has_value())
                        {
                            copyFailed = true;
                            copyError = cpResult.error();
                            break;
                        }
                    }
                }
            }
            else
            {
                if (auto const cpResult = _fs.copyFile(srcPath, target, true); !cpResult.has_value())
                {
                    copyFailed = true;
                    copyError = cpResult.error();
                }
            }

            if (copyFailed)
            {
                error("mv: cannot move '{}' to '{}': {}", src, target.string(), copyError);
                success = false;
                continue;
            }

            auto const removeResult = _fs.removeAll(srcPath);
            if (!removeResult.has_value())
            {
                error("mv: moved '{}' to '{}' but failed to remove source: {}",
                      src,
                      target.string(),
                      removeResult.error());
                success = false;
                continue;
            }
        }

        if (verbose)
            writeOutput(std::format("'{}' -> '{}'\n", src, target.string()));
    }

    return success ? 0 : 1;
}

void Shell::finalizePipelineBuiltin(bool lastInChain,
                                    CoreVM::CoreStringArray const& args,
                                    std::string_view programName,
                                    CoreVM::Params& context)
{
    _currentPipelineBuilder.closePipeFdsInParent();

    // Track command for job table
    std::string cmdString(programName);
    for (auto const i: std::views::iota(1uz, args.size()))
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

int Shell::executeInlineFind(CoreVM::CoreStringArray const& args, NativeHandle outputFd)
{
    // Parse arguments (skip args[0] which is "find")
    std::vector<std::string> findArgs;
    for (auto const i: std::views::iota(1uz, args.size()))
        findArgs.push_back(args.at(i));

    // Check for --help before parsing
    for (auto const& arg: findArgs)
    {
        if (arg == "--help" || arg == "-h")
            return renderMarkdownHelp(
                outputFd,
                "# find\n"
                "\n"
                "Search for files in a directory hierarchy.\n"
                "\n"
                "## Usage\n"
                "\n"
                "`find [PATH...] [EXPRESSION]`\n"
                "\n"
                "If no PATH is given, the current directory is used.\n"
                "\n"
                "## Options\n"
                "\n"
                "| Option | Description |\n"
                "|--------|-------------|\n"
                "| `-maxdepth N` | Descend at most N levels |\n"
                "| `-mindepth N` | Do not apply tests at levels less than N |\n"
                "| `-print0` | Print entries separated by null instead of newline |\n"
                "| `--help` | Display this help |\n"
                "\n"
                "## Predicates\n"
                "\n"
                "| Predicate | Description |\n"
                "|-----------|-------------|\n"
                "| `-name PATTERN` | Match filename against glob pattern |\n"
                "| `-iname PATTERN` | Like `-name` but case-insensitive |\n"
                "| `-path PATTERN` | Match full path against glob pattern |\n"
                "| `-ipath PATTERN` | Like `-path` but case-insensitive |\n"
                "| `-type TYPE` | Match file type: `f` (file), `d` (directory), `l` (symlink) |\n"
                "| `-size [+\\|-]N[c\\|k\\|M\\|G]` | Match file size (c=bytes, k=KiB, M=MiB, G=GiB) |\n"
                "| `-mtime [+\\|-]N` | Match modification time in 24-hour periods |\n"
                "| `-newer FILE` | Match files newer than FILE |\n"
                "| `-empty` | Match empty files or directories |\n"
                "\n"
                "## Operators\n"
                "\n"
                "| Operator | Description |\n"
                "|----------|-------------|\n"
                "| `-a`, `-and` | Logical AND (implicit between predicates) |\n"
                "| `-o`, `-or` | Logical OR |\n"
                "| `-not`, `!` | Logical NOT |\n"
                "| `( expr )` | Group expressions |\n");
    }

    auto parsed = find::parseFindArgs(findArgs);
    if (!parsed.has_value())
    {
        error("find: {}", parsed.error());
        return 1;
    }

    auto& [options, expression] = parsed.value();

    auto const separator = options.print0 ? std::string_view("\0", 1) : std::string_view("\n");

    // Helper to derive std::filesystem::file_type from FileSystem::DirectoryEntry booleans
    auto entryFileType = [](FileSystem::DirectoryEntry const& de) -> std::filesystem::file_type {
        if (de.isSymlink)
            return std::filesystem::file_type::symlink;
        if (de.isDirectory)
            return std::filesystem::file_type::directory;
        if (de.isRegularFile)
            return std::filesystem::file_type::regular;
        return std::filesystem::file_type::unknown;
    };

    // Helper to determine file type for a path via _fs queries
    auto pathFileType = [this](std::filesystem::path const& p) -> std::filesystem::file_type {
        if (_fs.isSymlink(p))
            return std::filesystem::file_type::symlink;
        if (_fs.isDirectory(p))
            return std::filesystem::file_type::directory;
        if (_fs.isRegularFile(p))
            return std::filesystem::file_type::regular;
        return std::filesystem::file_type::unknown;
    };

    for (auto const& searchPath: options.searchPaths)
    {
        // Output the search path itself (depth 0)
        if (!options.minDepth.has_value() || options.minDepth.value() <= 0)
        {
            if (!_fs.exists(searchPath))
            {
                error("find: '{}': No such file or directory", searchPath.string());
                continue;
            }

            auto const ftype = pathFileType(searchPath);
            auto const isFile = _fs.isRegularFile(searchPath);
            auto const sizeResult =
                isFile ? _fs.fileSize(searchPath) : std::expected<std::uintmax_t, std::string>(0);
            auto const mtimeResult = _fs.lastWriteTime(searchPath);

            find::FindEntry entry {
                .path = searchPath,
                .filename = searchPath.filename().string(),
                .type = ftype,
                .size = sizeResult.value_or(0),
                .mtime = mtimeResult.value_or(std::filesystem::file_time_type {}),
                .depth = 0,
            };

            if (!expression || expression->evaluate(entry))
            {
                auto const output = searchPath.string() + std::string(separator);
                platformWrite(outputFd, output.data(), output.size());
            }
        }

        // Skip recursion if maxdepth is 0
        if (options.maxDepth.has_value() && options.maxDepth.value() == 0)
            continue;

        auto const listResult = _fs.listDirectoryRecursive(searchPath);
        if (!listResult.has_value())
            continue;

        for (auto const& dirEntry: listResult.value())
        {
            // Compute depth from relative path
            auto const relativePath = dirEntry.path.lexically_relative(searchPath);
            auto const depth = static_cast<int>(std::distance(relativePath.begin(), relativePath.end()));

            if (options.maxDepth.has_value() && depth > options.maxDepth.value())
                continue;

            if (options.minDepth.has_value() && depth < options.minDepth.value())
                continue;

            auto const ftype = entryFileType(dirEntry);
            auto const sizeResult = dirEntry.isRegularFile ? _fs.fileSize(dirEntry.path)
                                                           : std::expected<std::uintmax_t, std::string>(0);
            auto const mtimeResult = _fs.lastWriteTime(dirEntry.path);

            find::FindEntry entry {
                .path = dirEntry.path,
                .filename = dirEntry.path.filename().string(),
                .type = ftype,
                .size = sizeResult.value_or(0),
                .mtime = mtimeResult.value_or(std::filesystem::file_time_type {}),
                .depth = depth,
            };

            if (!expression || expression->evaluate(entry))
            {
                auto const output = dirEntry.path.string() + std::string(separator);
                platformWrite(outputFd, output.data(), output.size());
            }
        }
    }

    return 0;
}

int Shell::executeInlineGrep(CoreVM::CoreStringArray const& args, NativeHandle outputFd, NativeHandle stdinFd)
{
    // Parse arguments (skip args[0] which is "grep")
    std::vector<std::string> grepArgs;
    for (auto const i: std::views::iota(1uz, args.size()))
        grepArgs.push_back(args.at(i));

    auto parsed = grep::parseGrepArgs(grepArgs);
    if (!parsed.has_value())
    {
        error("{}", parsed.error());
        return 2;
    }

    auto& opts = parsed.value();

    if (opts.showHelp)
        return renderMarkdownHelp(outputFd,
                                  "# grep\n"
                                  "\n"
                                  "Search for patterns in files.\n"
                                  "\n"
                                  "## Usage\n"
                                  "\n"
                                  "`grep [OPTIONS] PATTERN [FILE...]`\n"
                                  "\n"
                                  "## Pattern Selection\n"
                                  "\n"
                                  "| Option | Description |\n"
                                  "|--------|-------------|\n"
                                  "| `-e PATTERN` | Use PATTERN for matching (repeatable) |\n"
                                  "| `-F` | Interpret PATTERN as fixed strings |\n"
                                  "| `-E` | Interpret PATTERN as extended regex (default) |\n"
                                  "| `-i` | Ignore case distinctions |\n"
                                  "| `-w` | Match whole words only |\n"
                                  "| `-x` | Match whole lines only |\n"
                                  "\n"
                                  "## Output Control\n"
                                  "\n"
                                  "| Option | Description |\n"
                                  "|--------|-------------|\n"
                                  "| `-c` | Print count of matching lines |\n"
                                  "| `-l` | Print only filenames with matches |\n"
                                  "| `-L` | Print only filenames without matches |\n"
                                  "| `-n` | Prefix output with line numbers |\n"
                                  "| `-H` | Print filename for each match |\n"
                                  "| `-h` | Suppress filename prefix |\n"
                                  "| `-o` | Print only matching parts |\n"
                                  "| `-v` | Invert match (select non-matching lines) |\n"
                                  "| `-q` | Quiet mode (no output, exit code only) |\n"
                                  "| `-s` | Suppress error messages |\n"
                                  "\n"
                                  "## Context\n"
                                  "\n"
                                  "| Option | Description |\n"
                                  "|--------|-------------|\n"
                                  "| `-A NUM` | Print NUM lines of trailing context |\n"
                                  "| `-B NUM` | Print NUM lines of leading context |\n"
                                  "| `-C NUM` | Print NUM lines of context (before + after) |\n"
                                  "\n"
                                  "## File Selection\n"
                                  "\n"
                                  "| Option | Description |\n"
                                  "|--------|-------------|\n"
                                  "| `-r` | Recurse into directories |\n"
                                  "| `-m NUM` | Stop after NUM matches per file |\n"
                                  "| `-I` | Skip binary files |\n"
                                  "| `--include=GLOB` | Search only matching files |\n"
                                  "| `--exclude=GLOB` | Skip matching files |\n"
                                  "| `--exclude-dir=DIR` | Skip matching directories |\n"
                                  "| `--color=MODE` | Colorize output (auto/always/never) |\n");

    auto regex = grep::buildRegex(opts);
    if (!regex.has_value())
    {
        error("{}", regex.error());
        return 2;
    }

    // Determine color mode
    bool useColor = false;
    if (opts.colorMode == grep::ColorMode::Always)
        useColor = true;
    else if (opts.colorMode == grep::ColorMode::Auto)
    {
        useColor = isTerminal(outputFd);
    }

    // Output + error writer lambdas
    auto const writer = [outputFd](std::string_view sv) {
        platformWrite(outputFd, sv.data(), sv.size());
    };

    bool hasError = false;
    auto const errWriter = [this](std::string_view sv) {
        error("{}", sv.substr(0, sv.size() - (sv.ends_with('\n') ? 1 : 0)));
    };

    // Collect files
    auto files = grep::collectFiles(opts, errWriter, hasError);

    // Determine file count for filename display
    auto const readingStdin = opts.files.empty() && !opts.recursive;
    auto const fileCount = readingStdin ? 0uz : files.size();
    auto const showFilename = opts.showFilename(fileCount);

    size_t totalMatches = 0;

    if (readingStdin)
    {
        // Read from stdin
        std::string stdinData;
        std::array<char, 4096> buffer {};
        while (true)
        {
            auto const bytesRead = platformRead(stdinFd, buffer.data(), buffer.size());
            if (bytesRead <= 0)
                break;
            stdinData.append(buffer.data(), static_cast<size_t>(bytesRead));
        }

        // Split into lines
        std::vector<std::string> lines;
        std::string currentLine;
        for (auto const ch: stdinData)
        {
            if (ch == '\n')
            {
                lines.push_back(std::move(currentLine));
                currentLine.clear();
            }
            else
            {
                currentLine += ch;
            }
        }
        if (!currentLine.empty())
            lines.push_back(std::move(currentLine));

        totalMatches =
            grep::searchLines(lines, *regex, opts, "(standard input)", showFilename, useColor, writer);
    }
    else
    {
        // Search files
        for (auto const& filePath: files)
        {
            // Binary detection
            if (opts.skipBinary && grep::isBinaryFile(filePath))
                continue;

            // Read file
            auto fileStream = _fs.openRead(filePath);
            if (!fileStream || !fileStream->good())
            {
                if (!opts.suppressErrors)
                    error("grep: {}: Permission denied", filePath.string());
                hasError = true;
                continue;
            }

            std::vector<std::string> lines;
            std::string line;
            while (std::getline(*fileStream, line))
            {
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                lines.push_back(std::move(line));
            }

            auto const matches =
                grep::searchLines(lines, *regex, opts, filePath.string(), showFilename, useColor, writer);
            totalMatches += matches;

            // For -q, bail out early on first match
            if (opts.quiet && totalMatches > 0)
                return 0;

            // For -l, bail out of this file after first match (already handled in searchLines)
        }
    }

    if (hasError && totalMatches == 0)
        return 2;

    return totalMatches > 0 ? 0 : 1;
}

int Shell::executeInlineTimeout(CoreVM::CoreStringArray const& args, NativeHandle outputFd)
{
    // Parse arguments (skip args[0] which is "timeout")
    auto timeoutArgs = std::vector<std::string>();
    for (auto const i: std::views::iota(1uz, args.size()))
        timeoutArgs.push_back(args.at(i));

    auto parsed = timeout::parseTimeoutArgs(timeoutArgs);
    if (!parsed.has_value())
    {
        error("{}", parsed.error());
        return 125;
    }

    auto& opts = parsed.value();

    if (opts.showHelp)
        return renderMarkdownHelp(
            outputFd,
            "# timeout\n"
            "\n"
            "Run a command with a time limit.\n"
            "\n"
            "## Usage\n"
            "\n"
            "`timeout [OPTIONS] DURATION COMMAND [ARG...]`\n"
            "\n"
            "## Options\n"
            "\n"
            "| Option | Description |\n"
            "|--------|-------------|\n"
            "| `-s SIGNAL`, `--signal=SIGNAL` | Signal to send on timeout (default: TERM) |\n"
            "| `-k DURATION`, `--kill-after=DURATION` | Send SIGKILL after grace period |\n"
            "| `--preserve-status` | Return the command's exit status on timeout |\n"
            "| `--foreground` | Don't create a separate process group |\n"
            "| `-v`, `--verbose` | Diagnose to stderr when signal is sent |\n"
            "| `-h`, `--help` | Show this help |\n"
            "\n"
            "## Duration Format\n"
            "\n"
            "A floating-point number with optional suffix: `s` (seconds, default), `m` (minutes), "
            "`h` (hours), `d` (days).\n"
            "\n"
            "## Exit Codes\n"
            "\n"
            "| Code | Meaning |\n"
            "|------|--------|\n"
            "| 124 | Command timed out |\n"
            "| 125 | timeout command itself failed |\n"
            "| 126 | Command found but not executable |\n"
            "| 127 | Command not found |\n"
            "| 128+N | Command was killed by signal N |\n");

    // Resolve the sub-command
    auto const programPath = resolveProgram(opts.command[0]);
    if (!programPath.has_value())
    {
        if (programPath.error() == ShellError::ProgramNotFound)
        {
            error("timeout: {}: command not found", opts.command[0]);
            return 127;
        }
        error("timeout: {}: not executable", opts.command[0]);
        return 126;
    }

    // Build spawn config
    auto config = SpawnConfig {};
    config.program = programPath.value();
    for (auto const i: std::views::iota(1uz, opts.command.size()))
        config.arguments.push_back(opts.command[i]);
    config.stdoutFd = outputFd;
    if (!opts.foreground)
        config.processGroup = 0; // New process group

    // Spawn the sub-command
    auto spawnResult = _processManager.spawn(config);
    if (!spawnResult.has_value())
    {
        error("timeout: failed to spawn {}: {}", opts.command[0], static_cast<int>(spawnResult.error()));
        return 125;
    }
    auto const pid = spawnResult.value();

    // If duration is 0, just do a blocking wait (no timeout)
    if (opts.durationSeconds == 0.0)
    {
        auto waitResult = _processManager.wait(pid);
        if (!waitResult.has_value())
            return 125;
        if (waitResult->signaled)
            return 128 + waitResult->signal;
        return waitResult->exitCode;
    }

    // Poll loop with timeout
    auto const deadline =
        std::chrono::steady_clock::now() + std::chrono::duration<double>(opts.durationSeconds);
    auto timedOut = false;

    while (true)
    {
        auto waitResult = _processManager.wait(pid, WaitFlag::NoHang);
        if (!waitResult.has_value())
            return 125;

        if (waitResult->exitCode != -1 || waitResult->signaled || waitResult->stopped)
        {
            // Child has exited
            if (waitResult->signaled)
                return 128 + waitResult->signal;
            return waitResult->exitCode;
        }

        if (std::chrono::steady_clock::now() >= deadline)
        {
            timedOut = true;
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Timeout reached — send configured signal
    if (opts.verbose)
        _tty.writeToStderr(
            std::format("timeout: sending signal {} to command '{}'\n", opts.signal, opts.command[0]));
    if (auto const sendResult = _processManager.sendSignal(pid, opts.signal); !sendResult.has_value())
        return 125;

    // If kill-after is set, wait the grace period then send SIGKILL
    if (opts.killAfterSeconds > 0.0)
    {
        auto const killDeadline =
            std::chrono::steady_clock::now() + std::chrono::duration<double>(opts.killAfterSeconds);

        while (std::chrono::steady_clock::now() < killDeadline)
        {
            auto waitResult = _processManager.wait(pid, WaitFlag::NoHang);
            if (!waitResult.has_value())
                return 125;

            if (waitResult->exitCode != -1 || waitResult->signaled || waitResult->stopped)
            {
                if (opts.preserveStatus)
                {
                    if (waitResult->signaled)
                        return 128 + waitResult->signal;
                    return waitResult->exitCode;
                }
                return 124;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        // Grace period expired — send SIGKILL
        if (opts.verbose)
            _tty.writeToStderr(std::format("timeout: sending SIGKILL to command '{}'\n", opts.command[0]));

#if !defined(_WIN32)
        if (auto const sendResult = _processManager.sendSignal(pid, 9); !sendResult.has_value())
            return 125;
#else
        if (auto const sendResult = _processManager.sendSignal(pid, opts.signal); !sendResult.has_value())
            return 125;
#endif
    }

    // Reap the child
    auto finalResult = _processManager.wait(pid);
    if (!finalResult.has_value())
        return 125;

    if (opts.preserveStatus)
    {
        if (finalResult->signaled)
            return 128 + finalResult->signal;
        return finalResult->exitCode;
    }

    return timedOut ? 124 : finalResult->exitCode;
}

int Shell::executeInlineKill(CoreVM::CoreStringArray const& args, NativeHandle outputFd)
{
    // Parse arguments (skip args[0] which is "kill")
    auto killArgs = std::vector<std::string>();
    for (auto const i: std::views::iota(1uz, args.size()))
        killArgs.push_back(args.at(i));

    auto parsed = kill_cmd::parseKillArgs(killArgs);
    if (!parsed.has_value())
    {
        error("{}", parsed.error());
        return 1;
    }

    auto const& opts = parsed.value();

    if (opts.showHelp)
        return renderMarkdownHelp(outputFd,
                                  "# kill\n"
                                  "\n"
                                  "Send signals to processes or jobs.\n"
                                  "\n"
                                  "## Usage\n"
                                  "\n"
                                  "`kill [-SIGNAL | -s SIGNAL] PID|%JOB ...`\n"
                                  "`kill -l`\n"
                                  "\n"
                                  "## Options\n"
                                  "\n"
                                  "| Option | Description |\n"
                                  "|---|---|\n"
                                  "| `-SIGNAL` | Signal to send by name or number (default: `TERM`) |\n"
                                  "| `-s SIGNAL` | Signal to send (POSIX style) |\n"
                                  "| `-l` | List available signal names |\n"
                                  "| `-h`, `--help` | Show this help message |\n"
                                  "\n"
                                  "## Signals\n"
                                  "\n"
                                  "| # | Name | # | Name | # | Name |\n"
                                  "|---|---|---|---|---|---|\n"
                                  "| 1 | `HUP` | 2 | `INT` | 3 | `QUIT` |\n"
                                  "| 4 | `ILL` | 5 | `TRAP` | 6 | `ABRT` |\n"
                                  "| 7 | `BUS` | 8 | `FPE` | 9 | `KILL` |\n"
                                  "| 10 | `USR1` | 11 | `SEGV` | 12 | `USR2` |\n"
                                  "| 13 | `PIPE` | 14 | `ALRM` | 15 | `TERM` |\n"
                                  "\n"
                                  "## Examples\n"
                                  "\n"
                                  "| Example | Description |\n"
                                  "|---|---|\n"
                                  "| `kill 1234` | Send `SIGTERM` to process 1234 |\n"
                                  "| `kill -9 1234` | Send `SIGKILL` to process 1234 |\n"
                                  "| `kill -TERM 1234` | Send `SIGTERM` by name |\n"
                                  "| `kill %1` | Send `SIGTERM` to job 1 |\n"
                                  "| `kill -s USR1 5678` | Send `SIGUSR1` (POSIX style) |\n"
                                  "| `kill -l` | List all signal names |\n");

    if (opts.listSignals)
    {
        // clang-format off
        static constexpr std::pair<int, std::string_view> signals[] = {
            {  1, "HUP" }, {  2, "INT"  }, {  3, "QUIT" }, {  4, "ILL"  }, {  5, "TRAP" },
            {  6, "ABRT"}, {  7, "BUS"  }, {  8, "FPE"  }, {  9, "KILL" }, { 10, "USR1" },
            { 11, "SEGV"}, { 12, "USR2" }, { 13, "PIPE" }, { 14, "ALRM" }, { 15, "TERM" },
        };
        // clang-format on
        std::string output;
        for (auto const& [num, name]: signals)
            output += std::format("{:2}) {:<8}", num, name);

        output += '\n';
        [[maybe_unused]] auto const written = platformWrite(outputFd, output.data(), output.size());
        return 0;
    }

    auto exitCode = 0;
    for (auto const& target: opts.targets)
    {
        if (target.starts_with("%"))
        {
            // Job ID
            auto const jobIdStr = std::string_view(target).substr(1);
            int jobId = 0;
            auto const [ptr, ec] = std::from_chars(jobIdStr.data(), jobIdStr.data() + jobIdStr.size(), jobId);
            if (ec != std::errc {} || ptr != jobIdStr.data() + jobIdStr.size() || jobId <= 0)
            {
                error("kill: {}: invalid job specification", target);
                exitCode = 1;
                continue;
            }

            auto* job = jobTable.getJob(jobId);
            if (!job)
            {
                error("kill: {}: no such job", target);
                exitCode = 1;
                continue;
            }

            auto const result = _processManager.sendSignal(-static_cast<int>(job->pgid), opts.signal);
            if (!result.has_value())
            {
                error("kill: {}: {}", target, toString(result.error()));
                exitCode = 1;
            }
        }
        else
        {
            // PID
            int pid = 0;
            auto const [ptr, ec] = std::from_chars(target.data(), target.data() + target.size(), pid);
            if (ec != std::errc {} || ptr != target.data() + target.size())
            {
                error("kill: {}: invalid process id", target);
                exitCode = 1;
                continue;
            }

            auto const result = _processManager.sendSignal(static_cast<ProcessId>(pid), opts.signal);
            if (!result.has_value())
            {
                error("kill: ({}) - No such process", pid);
                exitCode = 1;
            }
        }
    }

    return exitCode;
}

// ---------------------------------------------------------------------------
// whoami
// ---------------------------------------------------------------------------

int Shell::executeInlineWhoami(CoreVM::CoreStringArray const& args, NativeHandle outputFd)
{
    for (auto const i: std::views::iota(1uz, args.size()))
    {
        std::string_view arg = args.at(i);
        if (arg == "-h" || arg == "--help")
            return renderMarkdownHelp(outputFd,
                                      "# whoami\n"
                                      "\n"
                                      "Print the current username.\n"
                                      "\n"
                                      "## Usage\n"
                                      "\n"
                                      "`whoami`\n"
                                      "\n"
                                      "## Options\n"
                                      "\n"
                                      "| Option | Description |\n"
                                      "|--------|-------------|\n"
                                      "| `-h`, `--help` | Display this help |\n");
    }

#if defined(_WIN32)
    char username[256];
    DWORD size = sizeof(username);
    if (GetUserNameA(username, &size))
    {
        auto output = std::format("{}\n", username);
        [[maybe_unused]] auto written = platformWrite(outputFd, output.data(), output.size());
        return 0;
    }
    error("whoami: cannot determine username");
    return 1;
#else
    if (auto const* pw = getpwuid(geteuid()))
    {
        auto output = std::format("{}\n", pw->pw_name);
        [[maybe_unused]] auto written = platformWrite(outputFd, output.data(), output.size());
        return 0;
    }
    error("whoami: cannot determine username");
    return 1;
#endif
}

// ---------------------------------------------------------------------------
// nproc
// ---------------------------------------------------------------------------

int Shell::executeInlineNproc(CoreVM::CoreStringArray const& args, NativeHandle outputFd)
{
    unsigned ignore = 0;

    for (auto const i: std::views::iota(1uz, args.size()))
    {
        std::string_view arg = args.at(i);
        if (arg == "-h" || arg == "--help")
            return renderMarkdownHelp(outputFd,
                                      "# nproc\n"
                                      "\n"
                                      "Print the number of available processing units.\n"
                                      "\n"
                                      "## Usage\n"
                                      "\n"
                                      "`nproc [OPTIONS]`\n"
                                      "\n"
                                      "## Options\n"
                                      "\n"
                                      "| Option | Description |\n"
                                      "|--------|-------------|\n"
                                      "| `--all` | Print the number of installed processors |\n"
                                      "| `--ignore=N` | Exclude N processing units |\n"
                                      "| `-h`, `--help` | Display this help |\n");
        if (arg == "--all")
            continue; // --all is the default behavior
        if (arg.starts_with("--ignore="))
        {
            auto const valStr = arg.substr(9);
            auto const [ptr, ec] = std::from_chars(valStr.data(), valStr.data() + valStr.size(), ignore);
            if (ec != std::errc())
            {
                error("nproc: invalid number: '{}'", valStr);
                return 1;
            }
            continue;
        }
        error("nproc: unrecognized option: '{}'", arg);
        return 1;
    }

    auto const total = std::thread::hardware_concurrency();
    auto const available = (total > ignore) ? (total - ignore) : 1u;
    auto output = std::format("{}\n", available);
    [[maybe_unused]] auto written = platformWrite(outputFd, output.data(), output.size());
    return 0;
}

// ---------------------------------------------------------------------------
// hostname
// ---------------------------------------------------------------------------

int Shell::executeInlineHostname(CoreVM::CoreStringArray const& args, NativeHandle outputFd)
{
    for (auto const i: std::views::iota(1uz, args.size()))
    {
        std::string_view arg = args.at(i);
        if (arg == "-h" || arg == "--help")
            return renderMarkdownHelp(outputFd,
                                      "# hostname\n"
                                      "\n"
                                      "Print the system hostname.\n"
                                      "\n"
                                      "## Usage\n"
                                      "\n"
                                      "`hostname`\n"
                                      "\n"
                                      "## Options\n"
                                      "\n"
                                      "| Option | Description |\n"
                                      "|--------|-------------|\n"
                                      "| `-h`, `--help` | Display this help |\n");
    }

    std::array<char, 256> hostbuf {};
#if defined(_WIN32)
    DWORD size = static_cast<DWORD>(hostbuf.size());
    if (GetComputerNameA(hostbuf.data(), &size))
    {
        auto output = std::format("{}\n", hostbuf.data());
        [[maybe_unused]] auto written = platformWrite(outputFd, output.data(), output.size());
        return 0;
    }
#else
    if (gethostname(hostbuf.data(), hostbuf.size()) == 0)
    {
        auto output = std::format("{}\n", hostbuf.data());
        [[maybe_unused]] auto written = platformWrite(outputFd, output.data(), output.size());
        return 0;
    }
#endif
    error("hostname: cannot determine hostname");
    return 1;
}

// ---------------------------------------------------------------------------
// date
// ---------------------------------------------------------------------------

int Shell::executeInlineDate(CoreVM::CoreStringArray const& args, NativeHandle outputFd)
{
    bool useUtc = false;
    bool showEpoch = false;
    bool showIso = false;
    std::string formatStr;
    std::string dateStr;

    for (size_t i = 1; i < args.size(); ++i)
    {
        std::string_view arg = args.at(i);
        if (arg == "-h" || arg == "--help")
            return renderMarkdownHelp(outputFd,
                                      "# date\n"
                                      "\n"
                                      "Print or format the current date and time.\n"
                                      "\n"
                                      "## Usage\n"
                                      "\n"
                                      "`date [OPTIONS]`\n"
                                      "\n"
                                      "## Options\n"
                                      "\n"
                                      "| Option | Description |\n"
                                      "|--------|-------------|\n"
                                      "| `-u`, `--utc` | Use UTC instead of local time |\n"
                                      "| `--epoch` | Print seconds since Unix epoch |\n"
                                      "| `--iso` | Print in ISO 8601 format |\n"
                                      "| `-f`, `--format FMT` | Use custom format (strftime) |\n"
                                      "| `-d`, `--date STRING` | Display given date instead of now |\n"
                                      "| `-h`, `--help` | Display this help |\n"
                                      "\n"
                                      "## Format Specifiers\n"
                                      "\n"
                                      "| Specifier | Description |\n"
                                      "|-----------|-------------|\n"
                                      "| `%Y` | Year (4 digits) |\n"
                                      "| `%m` | Month (01-12) |\n"
                                      "| `%d` | Day of month (01-31) |\n"
                                      "| `%H` | Hour (00-23) |\n"
                                      "| `%M` | Minute (00-59) |\n"
                                      "| `%S` | Second (00-59) |\n"
                                      "| `%A` | Full weekday name |\n"
                                      "| `%B` | Full month name |\n"
                                      "| `%Z` | Timezone abbreviation |\n");
        if (arg == "-u" || arg == "--utc")
        {
            useUtc = true;
            continue;
        }
        if (arg == "--epoch")
        {
            showEpoch = true;
            continue;
        }
        if (arg == "--iso")
        {
            showIso = true;
            continue;
        }
        if (arg == "-f" || arg == "--format")
        {
            if (i + 1 >= args.size())
            {
                error("date: --format requires an argument");
                return 1;
            }
            formatStr = args.at(++i);
            continue;
        }
        if (arg == "-d" || arg == "--date")
        {
            if (i + 1 >= args.size())
            {
                error("date: --date requires an argument");
                return 1;
            }
            dateStr = args.at(++i);
            continue;
        }
        // Support +FORMAT (like GNU date)
        if (arg.starts_with("+"))
        {
            formatStr = std::string(arg.substr(1));
            continue;
        }
        error("date: unrecognized option '{}'", arg);
        return 1;
    }

    // Get time point
    auto const now = std::chrono::system_clock::now();
    auto const timeT = std::chrono::system_clock::to_time_t(now);

    if (!dateStr.empty())
    {
        // Parse @EPOCH format
        if (dateStr.starts_with("@"))
        {
            auto const epochStr = dateStr.substr(1);
            long long epochVal = 0;
            auto const [ptr, ec] =
                std::from_chars(epochStr.data(), epochStr.data() + epochStr.size(), epochVal);
            if (ec != std::errc {} || ptr != epochStr.data() + epochStr.size())
            {
                error("date: invalid date '{}'", dateStr);
                return 1;
            }
            auto const epochTime = static_cast<time_t>(epochVal);
            std::tm timeBuf {};
#if defined(_WIN32)
            if (useUtc)
                gmtime_s(&timeBuf, &epochTime);
            else
                localtime_s(&timeBuf, &epochTime);
#else
            if (useUtc)
                gmtime_r(&epochTime, &timeBuf);
            else
                localtime_r(&epochTime, &timeBuf);
#endif

            std::array<char, 256> buf {};
            auto const fmt =
                formatStr.empty() ? std::string_view("%a %b %e %H:%M:%S %Z %Y") : std::string_view(formatStr);
            auto const len = strftime(buf.data(), buf.size(), std::string(fmt).c_str(), &timeBuf);
            auto output = std::format("{}\n", std::string_view(buf.data(), len));
            [[maybe_unused]] auto written = platformWrite(outputFd, output.data(), output.size());
            return 0;
        }
        error("date: unsupported date string '{}' (use @EPOCH)", dateStr);
        return 1;
    }

    if (showEpoch)
    {
        auto const epoch = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
        auto output = std::format("{}\n", epoch);
        [[maybe_unused]] auto written = platformWrite(outputFd, output.data(), output.size());
        return 0;
    }

    std::tm timeBuf {};
#if defined(_WIN32)
    if (useUtc)
        gmtime_s(&timeBuf, &timeT);
    else
        localtime_s(&timeBuf, &timeT);
#else
    if (useUtc)
        gmtime_r(&timeT, &timeBuf);
    else
        localtime_r(&timeT, &timeBuf);
#endif

    if (showIso)
    {
        std::array<char, 64> buf {};
        auto const len = strftime(buf.data(), buf.size(), "%Y-%m-%dT%H:%M:%S%z", &timeBuf);
        auto output = std::format("{}\n", std::string_view(buf.data(), len));
        [[maybe_unused]] auto written = platformWrite(outputFd, output.data(), output.size());
        return 0;
    }

    std::array<char, 256> buf {};
    auto const fmt =
        formatStr.empty() ? std::string_view("%a %b %e %H:%M:%S %Z %Y") : std::string_view(formatStr);
    auto const len = strftime(buf.data(), buf.size(), std::string(fmt).c_str(), &timeBuf);
    auto output = std::format("{}\n", std::string_view(buf.data(), len));
    [[maybe_unused]] auto written = platformWrite(outputFd, output.data(), output.size());
    return 0;
}

// ---------------------------------------------------------------------------
// uname
// ---------------------------------------------------------------------------

int Shell::executeInlineUname(CoreVM::CoreStringArray const& args, NativeHandle outputFd)
{
    bool showAll = false;
    bool showSysname = false;
    bool showNodename = false;
    bool showRelease = false;
    bool showMachine = false;

    for (size_t i = 1; i < args.size(); ++i)
    {
        std::string_view arg = args.at(i);
        if (arg == "--help")
            return renderMarkdownHelp(outputFd,
                                      "# uname\n"
                                      "\n"
                                      "Print system information.\n"
                                      "\n"
                                      "## Usage\n"
                                      "\n"
                                      "`uname [OPTIONS]`\n"
                                      "\n"
                                      "## Options\n"
                                      "\n"
                                      "| Option | Description |\n"
                                      "|--------|-------------|\n"
                                      "| `-s` | Print kernel name |\n"
                                      "| `-n` | Print network node hostname |\n"
                                      "| `-r` | Print kernel release |\n"
                                      "| `-m` | Print machine hardware name |\n"
                                      "| `-a` | Print all information |\n"
                                      "| `--help` | Display this help |\n");
        if (arg == "-a")
        {
            showAll = true;
            continue;
        }

        if (arg.starts_with("-") && arg.size() > 1 && arg[1] != '-')
        {
            for (auto const j: std::views::iota(1uz, arg.size()))
            {
                switch (arg[j])
                {
                    case 's': showSysname = true; break;
                    case 'n': showNodename = true; break;
                    case 'r': showRelease = true; break;
                    case 'm': showMachine = true; break;
                    case 'a': showAll = true; break;
                    default: error("uname: invalid option -- '{}'", arg[j]); return 1;
                }
            }
            continue;
        }

        error("uname: extra operand '{}'", arg);
        return 1;
    }

    // Default: show sysname only
    if (!showAll && !showSysname && !showNodename && !showRelease && !showMachine)
        showSysname = true;

#if defined(_WIN32)
    auto const sysname = std::string_view("Windows");
    std::array<char, 256> nodebuf {};
    DWORD nodeSize = static_cast<DWORD>(nodebuf.size());
    GetComputerNameA(nodebuf.data(), &nodeSize);
    auto const nodename = std::string_view(nodebuf.data());
    auto const release = std::string_view("10.0");
    #if defined(_M_X64) || defined(_M_AMD64)
    auto const machine = std::string_view("x86_64");
    #elif defined(_M_ARM64)
    auto const machine = std::string_view("aarch64");
    #else
    auto const machine = std::string_view("unknown");
    #endif
#else
    struct utsname unameData {};
    if (uname(&unameData) != 0)
    {
        error("uname: cannot get system information");
        return 1;
    }
    auto const sysname = std::string_view(unameData.sysname);
    auto const nodename = std::string_view(unameData.nodename);
    auto const release = std::string_view(unameData.release);
    auto const machine = std::string_view(unameData.machine);
#endif

    std::string result;
    auto const append = [&](std::string_view val) {
        if (!result.empty())
            result += ' ';
        result += val;
    };

    if (showAll || showSysname)
        append(sysname);
    if (showAll || showNodename)
        append(nodename);
    if (showAll || showRelease)
        append(release);
    if (showAll || showMachine)
        append(machine);

    result += '\n';
    [[maybe_unused]] auto written = platformWrite(outputFd, result.data(), result.size());
    return 0;
}

// ---------------------------------------------------------------------------
// basename
// ---------------------------------------------------------------------------

int Shell::executeInlineBasename(CoreVM::CoreStringArray const& args, NativeHandle outputFd)
{
    if (args.size() < 2)
    {
        error("basename: missing operand");
        return 1;
    }

    for (auto const i: std::views::iota(1uz, args.size()))
    {
        if (args.at(i) == "--help" || args.at(i) == "-h")
            return renderMarkdownHelp(outputFd,
                                      "# basename\n"
                                      "\n"
                                      "Strip directory and optional suffix from path.\n"
                                      "\n"
                                      "## Usage\n"
                                      "\n"
                                      "`basename PATH [SUFFIX]`\n"
                                      "\n"
                                      "## Options\n"
                                      "\n"
                                      "| Option | Description |\n"
                                      "|--------|-------------|\n"
                                      "| `--help` | Display this help |\n");
    }

    auto name = std::filesystem::path(args.at(1)).filename().string();
    if (name.empty())
        name = "/";

    // Remove suffix if given
    if (args.size() >= 3)
    {
        auto const suffix = std::string_view(args.at(2));
        if (!suffix.empty() && name.size() > suffix.size() && name.ends_with(suffix))
            name = name.substr(0, name.size() - suffix.size());
    }

    auto output = std::format("{}\n", name);
    [[maybe_unused]] auto written = platformWrite(outputFd, output.data(), output.size());
    return 0;
}

// ---------------------------------------------------------------------------
// dirname
// ---------------------------------------------------------------------------

int Shell::executeInlineDirname(CoreVM::CoreStringArray const& args, NativeHandle outputFd)
{
    if (args.size() < 2)
    {
        error("dirname: missing operand");
        return 1;
    }

    for (auto const i: std::views::iota(1uz, args.size()))
    {
        if (args.at(i) == "--help" || args.at(i) == "-h")
            return renderMarkdownHelp(outputFd,
                                      "# dirname\n"
                                      "\n"
                                      "Strip last component from path.\n"
                                      "\n"
                                      "## Usage\n"
                                      "\n"
                                      "`dirname PATH`\n"
                                      "\n"
                                      "## Options\n"
                                      "\n"
                                      "| Option | Description |\n"
                                      "|--------|-------------|\n"
                                      "| `--help` | Display this help |\n");
    }

    auto parent = std::filesystem::path(args.at(1)).parent_path().string();
    if (parent.empty())
        parent = ".";

    auto output = std::format("{}\n", parent);
    [[maybe_unused]] auto written = platformWrite(outputFd, output.data(), output.size());
    return 0;
}

// ---------------------------------------------------------------------------
// realpath
// ---------------------------------------------------------------------------

int Shell::executeInlineRealpath(CoreVM::CoreStringArray const& args, NativeHandle outputFd)
{
    if (args.size() < 2)
    {
        error("realpath: missing operand");
        return 1;
    }

    for (auto const i: std::views::iota(1uz, args.size()))
    {
        if (args.at(i) == "-h" || args.at(i) == "--help")
            return renderMarkdownHelp(outputFd,
                                      "# realpath\n"
                                      "\n"
                                      "Resolve path to absolute canonical form.\n"
                                      "\n"
                                      "## Usage\n"
                                      "\n"
                                      "`realpath PATH...`\n"
                                      "\n"
                                      "## Options\n"
                                      "\n"
                                      "| Option | Description |\n"
                                      "|--------|-------------|\n"
                                      "| `-h`, `--help` | Display this help |\n");
    }

    auto exitCode = 0;
    for (auto const i: std::views::iota(1uz, args.size()))
    {
        std::error_code ec;
        auto const canonical = std::filesystem::canonical(args.at(i), ec);
        if (ec)
        {
            error("realpath: {}: {}", args.at(i), ec.message());
            exitCode = 1;
            continue;
        }
        auto output = std::format("{}\n", canonical.string());
        [[maybe_unused]] auto written = platformWrite(outputFd, output.data(), output.size());
    }
    return exitCode;
}

// ---------------------------------------------------------------------------
// touch
// ---------------------------------------------------------------------------

int Shell::executeInlineTouch(CoreVM::CoreStringArray const& args, NativeHandle outputFd)
{
    bool noCreate = false;
    std::vector<std::string> files;

    for (size_t i = 1; i < args.size(); ++i)
    {
        std::string_view arg = args.at(i);
        if (arg == "-h" || arg == "--help")
            return renderMarkdownHelp(outputFd,
                                      "# touch\n"
                                      "\n"
                                      "Create file or update timestamps.\n"
                                      "\n"
                                      "## Usage\n"
                                      "\n"
                                      "`touch [OPTIONS] FILE...`\n"
                                      "\n"
                                      "## Options\n"
                                      "\n"
                                      "| Option | Description |\n"
                                      "|--------|-------------|\n"
                                      "| `-c`, `--no-create` | Do not create files |\n"
                                      "| `-h`, `--help` | Display this help |\n");
        if (arg == "-c" || arg == "--no-create")
        {
            noCreate = true;
            continue;
        }
        if (arg == "--")
        {
            for (auto const j: std::views::iota(i + 1, args.size()))
                files.push_back(args.at(j));
            break;
        }
        files.emplace_back(arg);
    }

    if (files.empty())
    {
        error("touch: missing file operand");
        return 1;
    }

    auto exitCode = 0;
    for (auto const& file: files)
    {
        auto const path = std::filesystem::path(file);
        std::error_code ec;
        if (std::filesystem::exists(path, ec))
        {
            // Update timestamp
            std::filesystem::last_write_time(path, std::filesystem::file_time_type::clock::now(), ec);
            if (ec)
            {
                error("touch: cannot touch '{}': {}", file, ec.message());
                exitCode = 1;
            }
        }
        else if (!noCreate)
        {
            // Create file
            std::ofstream ofs(path);
            if (!ofs)
            {
                error("touch: cannot touch '{}': Permission denied", file);
                exitCode = 1;
            }
        }
    }
    return exitCode;
}

// ---------------------------------------------------------------------------
// ln
// ---------------------------------------------------------------------------

int Shell::executeInlineLn(CoreVM::CoreStringArray const& args, NativeHandle outputFd)
{
    bool symbolic = false;
    bool force = false;
    bool verbose = false;
    std::vector<std::string> operands;

    for (size_t i = 1; i < args.size(); ++i)
    {
        std::string_view arg = args.at(i);
        if (arg == "--help")
            return renderMarkdownHelp(outputFd,
                                      "# ln\n"
                                      "\n"
                                      "Create hard or symbolic links.\n"
                                      "\n"
                                      "## Usage\n"
                                      "\n"
                                      "`ln [OPTIONS] TARGET LINK_NAME`\n"
                                      "\n"
                                      "## Options\n"
                                      "\n"
                                      "| Option | Description |\n"
                                      "|--------|-------------|\n"
                                      "| `-s` | Create symbolic link |\n"
                                      "| `-f` | Remove existing destination files |\n"
                                      "| `-v` | Explain what is being done |\n"
                                      "| `--help` | Display this help |\n");
        if (arg == "--")
        {
            for (auto const j: std::views::iota(i + 1, args.size()))
                operands.push_back(args.at(j));
            break;
        }
        if (arg.starts_with("-") && arg.size() > 1 && arg[1] != '-')
        {
            for (auto const j: std::views::iota(1uz, arg.size()))
            {
                switch (arg[j])
                {
                    case 's': symbolic = true; break;
                    case 'f': force = true; break;
                    case 'v': verbose = true; break;
                    default: error("ln: invalid option -- '{}'", arg[j]); return 1;
                }
            }
            continue;
        }
        operands.emplace_back(arg);
    }

    if (operands.size() < 2)
    {
        error("ln: missing operand");
        return 1;
    }

    auto const& target = operands[0];
    auto const& linkName = operands[1];

    std::error_code ec;
    if (force && std::filesystem::exists(linkName, ec))
        std::filesystem::remove(linkName, ec);

    if (symbolic)
        std::filesystem::create_symlink(target, linkName, ec);
    else
        std::filesystem::create_hard_link(target, linkName, ec);

    if (ec)
    {
        error("ln: failed to create link '{}' -> '{}': {}", linkName, target, ec.message());
        return 1;
    }

    if (verbose)
    {
        auto msg = std::format("'{}' -> '{}'\n", linkName, target);
        [[maybe_unused]] auto written = platformWrite(outputFd, msg.data(), msg.size());
    }

    return 0;
}

// ---------------------------------------------------------------------------
// mktemp
// ---------------------------------------------------------------------------

int Shell::executeInlineMktemp(CoreVM::CoreStringArray const& args, NativeHandle outputFd)
{
    bool createDir = false;
    std::string basedir;

    for (size_t i = 1; i < args.size(); ++i)
    {
        std::string_view arg = args.at(i);
        if (arg == "--help")
            return renderMarkdownHelp(outputFd,
                                      "# mktemp\n"
                                      "\n"
                                      "Create a temporary file or directory.\n"
                                      "\n"
                                      "## Usage\n"
                                      "\n"
                                      "`mktemp [OPTIONS]`\n"
                                      "\n"
                                      "## Options\n"
                                      "\n"
                                      "| Option | Description |\n"
                                      "|--------|-------------|\n"
                                      "| `-d` | Create a directory instead of a file |\n"
                                      "| `-p DIR` | Use DIR as the base directory |\n"
                                      "| `--help` | Display this help |\n");
        if (arg == "-d")
        {
            createDir = true;
            continue;
        }
        if (arg == "-p")
        {
            if (i + 1 >= args.size())
            {
                error("mktemp: -p requires an argument");
                return 1;
            }
            basedir = args.at(++i);
            continue;
        }
        error("mktemp: unrecognized option '{}'", arg);
        return 1;
    }

    auto const tmpdir =
        basedir.empty() ? std::filesystem::temp_directory_path() : std::filesystem::path(basedir);

    // Generate random suffix
    static constexpr std::string_view chars =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    std::string suffix = "tmp.";
    std::mt19937 rng(static_cast<unsigned>(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::uniform_int_distribution<size_t> dist(0, chars.size() - 1);
    for (auto const _: std::views::iota(0, 10))
    {
        (void) _;
        suffix += chars[dist(rng)];
    }

    auto const path = tmpdir / suffix;

    std::error_code ec;
    if (createDir)
    {
        std::filesystem::create_directories(path, ec);
        if (ec)
        {
            error("mktemp: failed to create directory '{}': {}", path.string(), ec.message());
            return 1;
        }
    }
    else
    {
        std::ofstream ofs(path);
        if (!ofs)
        {
            error("mktemp: failed to create file '{}'", path.string());
            return 1;
        }
    }

    auto output = std::format("{}\n", path.string());
    [[maybe_unused]] auto written = platformWrite(outputFd, output.data(), output.size());
    return 0;
}

// ---------------------------------------------------------------------------
// Helper: read all lines from stdin or files
// ---------------------------------------------------------------------------

namespace
{

    std::vector<std::string> readLinesFromInput(NativeHandle stdinFd,
                                                std::span<std::string const> files,
                                                auto const& errorFn)
    {
        std::vector<std::string> lines;

        auto const readFromStream = [&](std::istream& stream) {
            std::string line;
            while (std::getline(stream, line))
            {
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                lines.push_back(std::move(line));
            }
        };

        if (files.empty())
        {
            // Read from stdin
            std::string stdinData;
            std::array<char, 4096> buffer {};
            while (true)
            {
                auto const bytesRead = platformRead(stdinFd, buffer.data(), buffer.size());
                if (bytesRead <= 0)
                    break;
                stdinData.append(buffer.data(), static_cast<size_t>(bytesRead));
            }
            std::istringstream iss(stdinData);
            readFromStream(iss);
        }
        else
        {
            for (auto const& file: files)
            {
                if (file == "-")
                {
                    std::string stdinData;
                    std::array<char, 4096> buffer {};
                    while (true)
                    {
                        auto const bytesRead = platformRead(stdinFd, buffer.data(), buffer.size());
                        if (bytesRead <= 0)
                            break;
                        stdinData.append(buffer.data(), static_cast<size_t>(bytesRead));
                    }
                    std::istringstream iss(stdinData);
                    readFromStream(iss);
                }
                else
                {
                    std::ifstream ifs(file);
                    if (!ifs)
                    {
                        errorFn(std::format("{}: No such file or directory", file));
                        continue;
                    }
                    readFromStream(ifs);
                }
            }
        }
        return lines;
    }

} // namespace

// ---------------------------------------------------------------------------
// head
// ---------------------------------------------------------------------------

int Shell::executeInlineHead(CoreVM::CoreStringArray const& args, NativeHandle outputFd, NativeHandle stdinFd)
{
    int numLines = 10;
    std::vector<std::string> files;

    for (size_t i = 1; i < args.size(); ++i)
    {
        std::string_view arg = args.at(i);
        if (arg == "-h" || arg == "--help")
            return renderMarkdownHelp(outputFd,
                                      "# head\n"
                                      "\n"
                                      "Output the first lines of files.\n"
                                      "\n"
                                      "## Usage\n"
                                      "\n"
                                      "`head [OPTIONS] [FILE...]`\n"
                                      "\n"
                                      "## Options\n"
                                      "\n"
                                      "| Option | Description |\n"
                                      "|--------|-------------|\n"
                                      "| `-n NUM` | Output the first NUM lines (default: 10) |\n"
                                      "| `-h`, `--help` | Display this help |\n");
        if (arg == "-n")
        {
            if (i + 1 >= args.size())
            {
                error("head: option requires an argument -- 'n'");
                return 1;
            }
            auto const val = std::string_view(args.at(++i));
            auto const [ptr, ec] = std::from_chars(val.data(), val.data() + val.size(), numLines);
            if (ec != std::errc {} || ptr != val.data() + val.size())
            {
                error("head: invalid number of lines: '{}'", val);
                return 1;
            }
            continue;
        }
        files.emplace_back(arg);
    }

    auto const errorFn = [this](std::string const& msg) {
        error("head: {}", msg);
    };
    auto const lines = readLinesFromInput(stdinFd, files, errorFn);

    auto const count = std::min(static_cast<size_t>(numLines), lines.size());
    for (auto const i: std::views::iota(0uz, count))
    {
        auto output = std::format("{}\n", lines[i]);
        [[maybe_unused]] auto written = platformWrite(outputFd, output.data(), output.size());
    }
    return 0;
}

// ---------------------------------------------------------------------------
// tail
// ---------------------------------------------------------------------------

int Shell::executeInlineTail(CoreVM::CoreStringArray const& args, NativeHandle outputFd, NativeHandle stdinFd)
{
    int numLines = 10;
    bool follow = false;
    std::vector<std::string> files;

    for (size_t i = 1; i < args.size(); ++i)
    {
        std::string_view arg = args.at(i);
        if (arg == "-h" || arg == "--help")
            return renderMarkdownHelp(outputFd,
                                      "# tail\n"
                                      "\n"
                                      "Output the last lines of files.\n"
                                      "\n"
                                      "## Usage\n"
                                      "\n"
                                      "`tail [OPTIONS] [FILE...]`\n"
                                      "\n"
                                      "## Options\n"
                                      "\n"
                                      "| Option | Description |\n"
                                      "|--------|-------------|\n"
                                      "| `-n NUM` | Output the last NUM lines (default: 10) |\n"
                                      "| `-f` | Follow: output appended data as file grows |\n"
                                      "| `-h`, `--help` | Display this help |\n");
        if (arg == "-n")
        {
            if (i + 1 >= args.size())
            {
                error("tail: option requires an argument -- 'n'");
                return 1;
            }
            auto const val = std::string_view(args.at(++i));
            auto const [ptr, ec] = std::from_chars(val.data(), val.data() + val.size(), numLines);
            if (ec != std::errc {} || ptr != val.data() + val.size())
            {
                error("tail: invalid number of lines: '{}'", val);
                return 1;
            }
            continue;
        }
        if (arg == "-f")
        {
            follow = true;
            continue;
        }
        files.emplace_back(arg);
    }

    auto const writeLine = [outputFd](std::string_view line) {
        [[maybe_unused]] auto w1 = platformWrite(outputFd, line.data(), line.size());
        [[maybe_unused]] auto w2 = platformWrite(outputFd, "\n", 1);
    };

    if (follow && !files.empty())
    {
        // Follow mode: open file once, stream last N lines via bounded deque, then follow
        auto const& filePath = files.back();
        std::ifstream ifs(filePath);
        if (!ifs)
        {
            error("tail: cannot open '{}': No such file or directory", filePath);
            return 1;
        }

        // Read and output last N lines using a sliding window
        std::deque<std::string> lastLines;
        std::string line;
        while (std::getline(ifs, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            lastLines.push_back(std::move(line));
            if (lastLines.size() > static_cast<size_t>(numLines))
                lastLines.pop_front();
        }
        for (auto const& l: lastLines)
            writeLine(l);

        // getline() set EOF; clear it so subsequent reads can pick up appended data
        ifs.clear();

        SignalHandler::clearPendingSigint();
        while (true)
        {
            SignalHandler::processSignalFd();
            if (SignalHandler::hasPendingSigint())
            {
                SignalHandler::clearPendingSigint();
                return 130;
            }

            while (std::getline(ifs, line))
            {
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                writeLine(line);
            }
            ifs.clear();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    if (follow)
    {
        // Follow stdin — print initial last N lines, then poll for new data
        auto const errorFn = [this](std::string const& msg) {
            error("tail: {}", msg);
        };
        auto const lines = readLinesFromInput(stdinFd, files, errorFn);
        auto const start =
            lines.size() > static_cast<size_t>(numLines) ? lines.size() - static_cast<size_t>(numLines) : 0uz;
        for (auto const i: std::views::iota(start, lines.size()))
            writeLine(lines[i]);

        SignalHandler::clearPendingSigint();
        std::array<char, 4096> readBuf {};
        while (true)
        {
            SignalHandler::processSignalFd();
            if (SignalHandler::hasPendingSigint())
            {
                SignalHandler::clearPendingSigint();
                return 130;
            }
#if !defined(_WIN32)
            pollfd pfd { .fd = stdinFd, .events = POLLIN, .revents = 0 };
            auto const pollResult = poll(&pfd, 1, 100);
            if (pollResult < 0)
                break;
            if (pollResult > 0)
            {
                auto const bytesRead = platformRead(stdinFd, readBuf.data(), readBuf.size());
                if (bytesRead <= 0)
                    break;
                [[maybe_unused]] auto written =
                    platformWrite(outputFd, readBuf.data(), static_cast<size_t>(bytesRead));
            }
#else
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            auto const bytesRead = platformRead(stdinFd, readBuf.data(), readBuf.size());
            if (bytesRead <= 0)
                break;
            [[maybe_unused]] auto written =
                platformWrite(outputFd, readBuf.data(), static_cast<size_t>(bytesRead));
#endif
        }
        return 0;
    }

    // Non-follow mode: read all lines, output last N
    auto const errorFn = [this](std::string const& msg) {
        error("tail: {}", msg);
    };
    auto const lines = readLinesFromInput(stdinFd, files, errorFn);

    auto const start =
        lines.size() > static_cast<size_t>(numLines) ? lines.size() - static_cast<size_t>(numLines) : 0uz;
    for (auto const i: std::views::iota(start, lines.size()))
        writeLine(lines[i]);

    return 0;
}

// ---------------------------------------------------------------------------
// history
// ---------------------------------------------------------------------------

int Shell::executeInlineHistory(CoreVM::CoreStringArray const& args, NativeHandle outputFd)
{
    // Detect whether output goes to a TTY for syntax highlighting.
    bool const outputIsTty = isTerminal(outputFd);
    auto const* noColor = std::getenv("NO_COLOR");
    bool const useColor = outputIsTty && (noColor == nullptr || noColor[0] == '\0');

    auto const printNumberedEntries = [&](size_t maxCount) {
        auto const& entries = history.entries();
        auto const start = entries.size() > maxCount ? entries.size() - maxCount : 0uz;
        auto const& theme = tui::currentTheme();
        std::string buf;
        for (auto const i: std::views::iota(start, entries.size()))
        {
            buf.clear();
            if (useColor)
            {
                auto [highlights, _] = tui::highlightLine(entries[i], tui::LanguageId::Endo);
                auto const coloredEntry = tui::renderHighlightedLineToString(entries[i], highlights, theme);
                std::format_to(std::back_inserter(buf), "  \033[32m{:>5}\033[m  {}\n", i + 1, coloredEntry);
            }
            else
            {
                std::format_to(std::back_inserter(buf), "  {:>5}  {}\n", i + 1, entries[i]);
            }
            [[maybe_unused]] auto written = platformWrite(outputFd, buf.data(), buf.size());
        }
    };

    if (args.size() >= 2)
    {
        std::string_view const subcmd = args.at(1);
        if (subcmd == "-h" || subcmd == "--help")
            return renderMarkdownHelp(outputFd,
                                      "# history\n"
                                      "\n"
                                      "Display or manage command history.\n"
                                      "\n"
                                      "## Usage\n"
                                      "\n"
                                      "`history [N | search PATTERN | clear]`\n"
                                      "\n"
                                      "## Subcommands\n"
                                      "\n"
                                      "| Subcommand | Description |\n"
                                      "|------------|-------------|\n"
                                      "| *(none)* | List all history entries, numbered |\n"
                                      "| `N` | List the last N entries |\n"
                                      "| `search PATTERN` | Search entries by prefix |\n"
                                      "| `clear` | Clear all history |\n"
                                      "| `-h`, `--help` | Display this help |\n");

        if (subcmd == "clear")
        {
            history.clear();
            return 0;
        }

        if (subcmd == "search")
        {
            if (args.size() < 3)
            {
                error("history: search requires a pattern");
                return 1;
            }
            auto const results = history.search(args.at(2), 50);
            auto const& theme = tui::currentTheme();
            std::string buf;
            for (auto const& entry: results)
            {
                buf.clear();
                if (useColor)
                {
                    auto [highlights, _] = tui::highlightLine(entry, tui::LanguageId::Endo);
                    std::format_to(std::back_inserter(buf),
                                   "{}\n",
                                   tui::renderHighlightedLineToString(entry, highlights, theme));
                }
                else
                {
                    std::format_to(std::back_inserter(buf), "{}\n", entry);
                }
                [[maybe_unused]] auto written = platformWrite(outputFd, buf.data(), buf.size());
            }
            return 0;
        }

        // Try parsing as count N
        int count = 0;
        auto const [ptr, ec] = std::from_chars(subcmd.data(), subcmd.data() + subcmd.size(), count);
        if (ec == std::errc {} && ptr == subcmd.data() + subcmd.size())
        {
            if (count <= 0)
            {
                error("history: count must be a positive number");
                return 1;
            }
            printNumberedEntries(static_cast<size_t>(count));
            return 0;
        }

        error("history: unknown subcommand '{}'", subcmd);
        return 1;
    }

    printNumberedEntries(history.size());
    return 0;
}

// ---------------------------------------------------------------------------
// source / .
// ---------------------------------------------------------------------------

int Shell::executeInlineSource(CoreVM::CoreStringArray const& args, NativeHandle outputFd)
{
    if (args.size() < 2)
    {
        error("{}: filename argument required", args.at(0));
        return 1;
    }

    std::string_view const firstArg = args.at(1);
    if (firstArg == "-h" || firstArg == "--help")
        return renderMarkdownHelp(outputFd,
                                  "# source\n"
                                  "\n"
                                  "Execute a script in the current shell context.\n"
                                  "\n"
                                  "## Usage\n"
                                  "\n"
                                  "`source FILE [ARGS...]`\n"
                                  "``. FILE [ARGS...]``\n"
                                  "\n"
                                  "Reads and executes commands from FILE in the current shell\n"
                                  "environment. Variables, functions, and other state changes\n"
                                  "persist after the script completes.\n"
                                  "\n"
                                  "## Options\n"
                                  "\n"
                                  "| Option | Description |\n"
                                  "|--------|-------------|\n"
                                  "| `-h`, `--help` | Display this help |\n");

    auto const filePath = std::string(firstArg);
    return executeEndoScript(std::filesystem::path(filePath));
}

// ---------------------------------------------------------------------------
// wc
// ---------------------------------------------------------------------------

int Shell::executeInlineWc(CoreVM::CoreStringArray const& args, NativeHandle outputFd, NativeHandle stdinFd)
{
    bool countLines = false;
    bool countWords = false;
    bool countChars = false;
    std::vector<std::string> files;

    for (size_t i = 1; i < args.size(); ++i)
    {
        std::string_view arg = args.at(i);
        if (arg == "--help")
            return renderMarkdownHelp(outputFd,
                                      "# wc\n"
                                      "\n"
                                      "Count lines, words, and characters.\n"
                                      "\n"
                                      "## Usage\n"
                                      "\n"
                                      "`wc [OPTIONS] [FILE...]`\n"
                                      "\n"
                                      "## Options\n"
                                      "\n"
                                      "| Option | Description |\n"
                                      "|--------|-------------|\n"
                                      "| `-l` | Print line count |\n"
                                      "| `-w` | Print word count |\n"
                                      "| `-c` | Print character count |\n"
                                      "| `--help` | Display this help |\n"
                                      "\n"
                                      "With no options, prints lines, words, and characters.\n");
        if (arg.starts_with("-") && arg.size() > 1 && arg[1] != '-')
        {
            for (auto const j: std::views::iota(1uz, arg.size()))
            {
                switch (arg[j])
                {
                    case 'l': countLines = true; break;
                    case 'w': countWords = true; break;
                    case 'c': countChars = true; break;
                    default: error("wc: invalid option -- '{}'", arg[j]); return 1;
                }
            }
            continue;
        }
        files.emplace_back(arg);
    }

    // Default: show all
    if (!countLines && !countWords && !countChars)
    {
        countLines = true;
        countWords = true;
        countChars = true;
    }

    auto const errorFn = [this](std::string const& msg) {
        error("wc: {}", msg);
    };
    auto const lines = readLinesFromInput(stdinFd, files, errorFn);

    size_t totalLines = lines.size();
    size_t totalWords = 0;
    size_t totalChars = 0;

    for (auto const& line: lines)
    {
        totalChars += line.size() + 1; // +1 for newline
        bool inWord = false;
        for (auto const ch: line)
        {
            if (std::isspace(static_cast<unsigned char>(ch)))
                inWord = false;
            else if (!inWord)
            {
                ++totalWords;
                inWord = true;
            }
        }
    }

    std::string output;
    if (countLines)
        output += std::format("{}", totalLines);
    if (countWords)
    {
        if (!output.empty())
            output += ' ';
        output += std::format("{}", totalWords);
    }
    if (countChars)
    {
        if (!output.empty())
            output += ' ';
        output += std::format("{}", totalChars);
    }
    output += '\n';
    [[maybe_unused]] auto written = platformWrite(outputFd, output.data(), output.size());
    return 0;
}

// ---------------------------------------------------------------------------
// sort
// ---------------------------------------------------------------------------

int Shell::executeInlineSort(CoreVM::CoreStringArray const& args, NativeHandle outputFd, NativeHandle stdinFd)
{
    bool reverse = false;
    bool numeric = false;
    bool unique = false;
    int keyField = 0; // 0 = whole line
    std::vector<std::string> files;

    for (size_t i = 1; i < args.size(); ++i)
    {
        std::string_view arg = args.at(i);
        if (arg == "--help")
            return renderMarkdownHelp(outputFd,
                                      "# sort\n"
                                      "\n"
                                      "Sort lines of text.\n"
                                      "\n"
                                      "## Usage\n"
                                      "\n"
                                      "`sort [OPTIONS] [FILE...]`\n"
                                      "\n"
                                      "## Options\n"
                                      "\n"
                                      "| Option | Description |\n"
                                      "|--------|-------------|\n"
                                      "| `-r` | Reverse sort order |\n"
                                      "| `-n` | Compare according to string numerical value |\n"
                                      "| `-u` | Output only unique lines |\n"
                                      "| `-k FIELD` | Sort by key field number |\n"
                                      "| `--help` | Display this help |\n");
        if (arg.starts_with("-") && arg.size() > 1 && arg[1] != '-')
        {
            for (auto const j: std::views::iota(1uz, arg.size()))
            {
                switch (arg[j])
                {
                    case 'r': reverse = true; break;
                    case 'n': numeric = true; break;
                    case 'u': unique = true; break;
                    case 'k':
                        if (i + 1 >= args.size())
                        {
                            error("sort: option requires an argument -- 'k'");
                            return 1;
                        }
                        {
                            auto const val = std::string_view(args.at(++i));
                            auto const [ptr, ec] =
                                std::from_chars(val.data(), val.data() + val.size(), keyField);
                            if (ec != std::errc {})
                            {
                                error("sort: invalid key: '{}'", val);
                                return 1;
                            }
                        }
                        goto next_arg; // NOLINT
                    default: error("sort: invalid option -- '{}'", arg[j]); return 1;
                }
            }
        next_arg:
            continue;
        }
        files.emplace_back(arg);
    }

    auto const errorFn = [this](std::string const& msg) {
        error("sort: {}", msg);
    };
    auto lines = readLinesFromInput(stdinFd, files, errorFn);

    // Extract key field helper
    auto const getKey = [keyField](std::string_view line) -> std::string_view {
        if (keyField <= 0)
            return line;
        size_t pos = 0;
        int field = 1;
        // Skip to the right field (whitespace-delimited)
        while (field < keyField && pos < line.size())
        {
            while (pos < line.size() && !std::isspace(static_cast<unsigned char>(line[pos])))
                ++pos;
            while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos])))
                ++pos;
            ++field;
        }
        auto const start = pos;
        while (pos < line.size() && !std::isspace(static_cast<unsigned char>(line[pos])))
            ++pos;
        return line.substr(start, pos - start);
    };

    if (numeric)
    {
        std::ranges::sort(lines, [&](auto const& a, auto const& b) {
            auto const ka = getKey(a);
            auto const kb = getKey(b);
            double va = 0;
            double vb = 0;
            std::from_chars(ka.data(), ka.data() + ka.size(), va);
            std::from_chars(kb.data(), kb.data() + kb.size(), vb);
            return reverse ? va > vb : va < vb;
        });
    }
    else
    {
        std::ranges::sort(lines, [&](auto const& a, auto const& b) {
            auto const ka = getKey(a);
            auto const kb = getKey(b); // NOLINT(clang-analyzer-cplusplus.Move)
            return reverse ? ka > kb : ka < kb;
        });
    }

    if (unique)
    {
        auto const [first, last] = std::ranges::unique(lines);
        lines.erase(first, last);
    }

    for (auto const& line: lines)
    {
        auto output = std::format("{}\n", line);
        [[maybe_unused]] auto written = platformWrite(outputFd, output.data(), output.size());
    }
    return 0;
}

// ---------------------------------------------------------------------------
// uniq
// ---------------------------------------------------------------------------

int Shell::executeInlineUniq(CoreVM::CoreStringArray const& args, NativeHandle outputFd, NativeHandle stdinFd)
{
    bool showCount = false;
    bool duplicatesOnly = false;
    bool ignoreCase = false;
    std::vector<std::string> files;

    for (size_t i = 1; i < args.size(); ++i)
    {
        std::string_view arg = args.at(i);
        if (arg == "--help")
            return renderMarkdownHelp(outputFd,
                                      "# uniq\n"
                                      "\n"
                                      "Filter adjacent duplicate lines.\n"
                                      "\n"
                                      "## Usage\n"
                                      "\n"
                                      "`uniq [OPTIONS] [FILE]`\n"
                                      "\n"
                                      "## Options\n"
                                      "\n"
                                      "| Option | Description |\n"
                                      "|--------|-------------|\n"
                                      "| `-c` | Prefix lines with occurrence count |\n"
                                      "| `-d` | Only print duplicate lines |\n"
                                      "| `-i` | Ignore case when comparing |\n"
                                      "| `--help` | Display this help |\n");
        if (arg.starts_with("-") && arg.size() > 1 && arg[1] != '-')
        {
            for (auto const j: std::views::iota(1uz, arg.size()))
            {
                switch (arg[j])
                {
                    case 'c': showCount = true; break;
                    case 'd': duplicatesOnly = true; break;
                    case 'i': ignoreCase = true; break;
                    default: error("uniq: invalid option -- '{}'", arg[j]); return 1;
                }
            }
            continue;
        }
        files.emplace_back(arg);
    }

    auto const errorFn = [this](std::string const& msg) {
        error("uniq: {}", msg);
    };
    auto const lines = readLinesFromInput(stdinFd, files, errorFn);

    auto const compareEqual = [ignoreCase](std::string_view a, std::string_view b) {
        if (!ignoreCase)
            return a == b;
        if (a.size() != b.size())
            return false;
        for (size_t i = 0; i < a.size(); ++i)
        {
            if (std::tolower(static_cast<unsigned char>(a[i]))
                != std::tolower(static_cast<unsigned char>(b[i])))
                return false;
        }
        return true;
    };

    size_t i = 0;
    while (i < lines.size())
    {
        size_t count = 1;
        while (i + count < lines.size() && compareEqual(lines[i], lines[i + count]))
            ++count;

        if (!duplicatesOnly || count > 1)
        {
            std::string output;
            if (showCount)
                output = std::format("{:>7} {}\n", count, lines[i]);
            else
                output = std::format("{}\n", lines[i]);
            [[maybe_unused]] auto written = platformWrite(outputFd, output.data(), output.size());
        }
        i += count;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// cut
// ---------------------------------------------------------------------------

int Shell::executeInlineCut(CoreVM::CoreStringArray const& args, NativeHandle outputFd, NativeHandle stdinFd)
{
    char delimiter = '\t';
    std::string fieldSpec;
    std::string charSpec;
    std::vector<std::string> files;

    for (size_t i = 1; i < args.size(); ++i)
    {
        std::string_view arg = args.at(i);
        if (arg == "--help")
            return renderMarkdownHelp(outputFd,
                                      "# cut\n"
                                      "\n"
                                      "Extract fields or characters from lines.\n"
                                      "\n"
                                      "## Usage\n"
                                      "\n"
                                      "`cut [OPTIONS] [FILE...]`\n"
                                      "\n"
                                      "## Options\n"
                                      "\n"
                                      "| Option | Description |\n"
                                      "|--------|-------------|\n"
                                      "| `-d DELIM` | Use DELIM as field delimiter (default: tab) |\n"
                                      "| `-f FIELDS` | Select fields (e.g. `1`, `1,3`, `1-3`) |\n"
                                      "| `-c CHARS` | Select characters (e.g. `1-5`, `3`) |\n"
                                      "| `--help` | Display this help |\n");
        if (arg == "-d")
        {
            if (i + 1 >= args.size())
            {
                error("cut: option requires an argument -- 'd'");
                return 1;
            }
            auto const delVal = std::string_view(args.at(++i));
            delimiter = delVal.empty() ? '\t' : delVal[0];
            continue;
        }
        if (arg == "-f")
        {
            if (i + 1 >= args.size())
            {
                error("cut: option requires an argument -- 'f'");
                return 1;
            }
            fieldSpec = args.at(++i);
            continue;
        }
        if (arg == "-c")
        {
            if (i + 1 >= args.size())
            {
                error("cut: option requires an argument -- 'c'");
                return 1;
            }
            charSpec = args.at(++i);
            continue;
        }
        files.emplace_back(arg);
    }

    if (fieldSpec.empty() && charSpec.empty())
    {
        error("cut: you must specify a list of bytes, characters, or fields");
        return 1;
    }

    // Parse range spec (e.g., "1", "1,3", "1-3")
    auto const parseRangeSpec = [](std::string_view spec) -> std::vector<std::pair<int, int>> {
        std::vector<std::pair<int, int>> ranges;
        size_t pos = 0;
        while (pos < spec.size())
        {
            auto const comma = spec.find(',', pos);
            auto const part = spec.substr(pos, comma == std::string_view::npos ? comma : comma - pos);
            auto const dash = part.find('-');
            if (dash == std::string_view::npos)
            {
                int val = 0;
                std::from_chars(part.data(), part.data() + part.size(), val);
                ranges.emplace_back(val, val);
            }
            else
            {
                int start = 1;
                int end = 999999;
                if (dash > 0)
                {
                    auto const startPart = part.substr(0, dash);
                    std::from_chars(startPart.data(), startPart.data() + startPart.size(), start);
                }
                if (dash + 1 < part.size())
                {
                    auto const endPart = part.substr(dash + 1);
                    std::from_chars(endPart.data(), endPart.data() + endPart.size(), end);
                }
                ranges.emplace_back(start, end);
            }
            pos = comma == std::string_view::npos ? spec.size() : comma + 1;
        }
        return ranges;
    };

    auto const errorFn = [this](std::string const& msg) {
        error("cut: {}", msg);
    };
    auto const lines = readLinesFromInput(stdinFd, files, errorFn);

    if (!fieldSpec.empty())
    {
        auto const ranges = parseRangeSpec(fieldSpec);
        for (auto const& line: lines)
        {
            // Split by delimiter
            std::vector<std::string_view> fields;
            size_t start = 0;
            for (size_t p = 0; p <= line.size(); ++p)
            {
                if (p == line.size() || line[p] == delimiter)
                {
                    fields.push_back(std::string_view(line).substr(start, p - start));
                    start = p + 1;
                }
            }

            std::string output;
            bool first = true;
            for (auto const& [lo, hi]: ranges)
            {
                for (int f = lo; f <= hi && std::cmp_less_equal(f, fields.size()); ++f)
                {
                    if (!first)
                        output += delimiter;
                    first = false;
                    output += fields[static_cast<size_t>(f - 1)];
                }
            }
            output += '\n';
            [[maybe_unused]] auto written = platformWrite(outputFd, output.data(), output.size());
        }
    }
    else
    {
        auto const ranges = parseRangeSpec(charSpec);
        for (auto const& line: lines)
        {
            std::string output;
            for (auto const& [lo, hi]: ranges)
            {
                for (int c = lo; c <= hi && std::cmp_less_equal(c, line.size()); ++c)
                    output += line[static_cast<size_t>(c - 1)];
            }
            output += '\n';
            [[maybe_unused]] auto written = platformWrite(outputFd, output.data(), output.size());
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// tr
// ---------------------------------------------------------------------------

int Shell::executeInlineTr(CoreVM::CoreStringArray const& args, NativeHandle outputFd, NativeHandle stdinFd)
{
    bool deleteMode = false;
    bool squeezeMode = false;
    std::string set1;
    std::string set2;

    size_t i = 1;
    for (; i < args.size(); ++i)
    {
        std::string_view arg = args.at(i);
        if (arg == "--help")
            return renderMarkdownHelp(outputFd,
                                      "# tr\n"
                                      "\n"
                                      "Translate or delete characters.\n"
                                      "\n"
                                      "## Usage\n"
                                      "\n"
                                      "`tr [OPTIONS] SET1 [SET2]`\n"
                                      "\n"
                                      "Reads from standard input and writes to standard output.\n"
                                      "\n"
                                      "## Options\n"
                                      "\n"
                                      "| Option | Description |\n"
                                      "|--------|-------------|\n"
                                      "| `-d` | Delete characters in SET1 |\n"
                                      "| `-s` | Squeeze repeated output characters in SET2 |\n"
                                      "| `--help` | Display this help |\n"
                                      "\n"
                                      "## Character Classes\n"
                                      "\n"
                                      "`a-z`, `A-Z`, `0-9` are expanded as character ranges.\n");
        if (arg == "-d")
        {
            deleteMode = true;
            continue;
        }
        if (arg == "-s")
        {
            squeezeMode = true;
            continue;
        }
        if (arg.starts_with("-") && arg.size() > 1 && arg[1] != '-')
        {
            for (auto const j: std::views::iota(1uz, arg.size()))
            {
                switch (arg[j])
                {
                    case 'd': deleteMode = true; break;
                    case 's': squeezeMode = true; break;
                    default: error("tr: invalid option -- '{}'", arg[j]); return 1;
                }
            }
            continue;
        }
        break;
    }

    // Remaining args are SET1 and SET2
    if (i < args.size())
        set1 = args.at(i++);
    if (i < args.size())
        set2 = args.at(i++);

    if (set1.empty())
    {
        error("tr: missing operand");
        return 1;
    }

    // Expand character ranges like a-z, A-Z, 0-9
    auto const expandRange = [](std::string_view s) -> std::string {
        std::string result;
        for (size_t p = 0; p < s.size(); ++p)
        {
            if (p + 2 < s.size() && s[p + 1] == '-')
            {
                auto const from = s[p];
                auto const to = s[p + 2];
                if (from <= to)
                {
                    for (char c = from; c <= to; ++c)
                        result += c;
                }
                else
                {
                    for (char c = from; c >= to; --c)
                        result += c;
                }
                p += 2;
            }
            else
            {
                result += s[p];
            }
        }
        return result;
    };

    auto const expandedSet1 = expandRange(set1);
    auto const expandedSet2 = expandRange(set2);

    // Read stdin
    std::string inputData;
    std::array<char, 4096> buffer {};
    while (true)
    {
        auto const bytesRead = platformRead(stdinFd, buffer.data(), buffer.size());
        if (bytesRead <= 0)
            break;
        inputData.append(buffer.data(), static_cast<size_t>(bytesRead));
    }

    std::string output;
    output.reserve(inputData.size());

    if (deleteMode)
    {
        for (auto const ch: inputData)
        {
            if (expandedSet1.find(ch) == std::string::npos)
                output += ch;
        }
    }
    else if (!expandedSet2.empty())
    {
        // Translate
        char lastOutput = '\0';
        for (auto const ch: inputData)
        {
            auto const pos = expandedSet1.find(ch);
            auto const mapped =
                pos != std::string::npos ? expandedSet2[std::min(pos, expandedSet2.size() - 1)] : ch;
            if (squeezeMode && mapped == lastOutput && pos != std::string::npos)
                continue;
            output += mapped;
            lastOutput = mapped;
        }
    }
    else
    {
        output = inputData;
    }

    [[maybe_unused]] auto written = platformWrite(outputFd, output.data(), output.size());
    return 0;
}

// ---------------------------------------------------------------------------
// tee
// ---------------------------------------------------------------------------

int Shell::executeInlineTee(CoreVM::CoreStringArray const& args, NativeHandle outputFd, NativeHandle stdinFd)
{
    bool appendMode = false;
    std::vector<std::string> files;

    for (size_t i = 1; i < args.size(); ++i)
    {
        std::string_view arg = args.at(i);
        if (arg == "-h" || arg == "--help")
            return renderMarkdownHelp(outputFd,
                                      "# tee\n"
                                      "\n"
                                      "Read stdin, write to stdout and files.\n"
                                      "\n"
                                      "## Usage\n"
                                      "\n"
                                      "`tee [OPTIONS] [FILE...]`\n"
                                      "\n"
                                      "## Options\n"
                                      "\n"
                                      "| Option | Description |\n"
                                      "|--------|-------------|\n"
                                      "| `-a` | Append to files instead of overwriting |\n"
                                      "| `-h`, `--help` | Display this help |\n");
        if (arg == "-a" || arg == "--append")
        {
            appendMode = true;
            continue;
        }
        files.emplace_back(arg);
    }

    // Open output files
    std::vector<std::ofstream> outStreams;
    for (auto const& file: files)
    {
        auto mode = std::ios::out;
        if (appendMode)
            mode |= std::ios::app;
        outStreams.emplace_back(file, mode);
        if (!outStreams.back())
        {
            error("tee: {}: Permission denied", file);
            return 1;
        }
    }

    // Read from stdin, write to stdout + files
    std::array<char, 4096> buffer {};
    while (true)
    {
        auto const bytesRead = platformRead(stdinFd, buffer.data(), buffer.size());
        if (bytesRead <= 0)
            break;

        auto const data = std::string_view(buffer.data(), static_cast<size_t>(bytesRead));
        [[maybe_unused]] auto written = platformWrite(outputFd, data.data(), data.size());

        for (auto& ofs: outStreams)
            ofs.write(data.data(), static_cast<std::streamsize>(data.size()));
    }

    return 0;
}

} // namespace endo
