// SPDX-License-Identifier: Apache-2.0
#include <shell/Shell.hpp>
#include <shell/commands/FindExpression.hpp>
#include <shell/commands/GrepCommand.hpp>
#include <shell/commands/TimeoutCommand.hpp>

#include <tui/GenericSyntaxHighlighter.hpp>
#include <tui/ImageLoader.hpp>
#include <tui/MarkdownRenderer.hpp>
#include <tui/Sixel.hpp>
#include <tui/TerminalOutput.hpp>
#include <tui/Theme.hpp>

#include <charconv>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <ranges>
#include <span>
#include <thread>

#include <fcntl.h>

#include <platform/Process.hpp>
#include <platform/Types.hpp>

#if !defined(_WIN32)
    #include <unistd.h>
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
#if !defined(_WIN32)
    if (isatty(outputFd) != 0 && outputFd == standardOutput())
    {
        tui::TerminalOutput termOutput;
        tui::MarkdownRenderer renderer(termOutput);
        renderer.render(markdownContent);
        termOutput.flush();
        return 0;
    }
#endif
    [[maybe_unused]] auto written = platformWrite(outputFd, markdownContent.data(), markdownContent.size());
    written = platformWrite(outputFd, "\n", 1);
    return 0;
}

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
#if defined(_WIN32)
    DWORD consoleMode = 0;
    bool const outputIsTty = GetConsoleMode(outputFd, &consoleMode) != 0;
#else
    bool const outputIsTty = isatty(outputFd) != 0;
#endif

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
        intptr_t bytesRead;
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
            force = true;
            noClobber = false;
            continue;
        }
        if (!endOfOptions && arg == "--no-clobber")
        {
            noClobber = true;
            force = false;
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
                        force = true;
                        noClobber = false;
                        break;
                    case 'n':
                        noClobber = true;
                        force = false;
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

    namespace fs = std::filesystem;

    auto const dest = fs::path(paths.back());
    auto const sources = std::span(paths.data(), paths.size() - 1);
    auto const destIsDir = fs::is_directory(dest);

    if (sources.size() > 1 && !destIsDir)
    {
        error("cp: target '{}' is not a directory", dest.string());
        return 1;
    }

    auto copyOptions = fs::copy_options::none;
    if (noClobber)
        copyOptions |= fs::copy_options::skip_existing;
    else
        copyOptions |= fs::copy_options::overwrite_existing;
    if (recursive)
        copyOptions |= fs::copy_options::recursive;

    bool success = true;
    for (auto const& src: sources)
    {
        auto const srcPath = fs::path(src);
        std::error_code ec;

        if (!fs::exists(srcPath, ec))
        {
            error("cp: cannot stat '{}': No such file or directory", src);
            success = false;
            continue;
        }

        if (fs::is_directory(srcPath) && !recursive)
        {
            error("cp: -r not specified; omitting directory '{}'", src);
            success = false;
            continue;
        }

        auto const target = destIsDir ? dest / srcPath.filename() : dest;

        if (recursive && fs::is_directory(srcPath) && verbose)
        {
            // Verbose recursive copy: iterate and copy individually to report each file
            for (auto const& entry: fs::recursive_directory_iterator(srcPath, ec))
            {
                auto const relativePath = fs::relative(entry.path(), srcPath, ec);
                auto const entryTarget = target / relativePath;

                if (entry.is_directory())
                {
                    fs::create_directories(entryTarget, ec);
                    if (ec)
                    {
                        error("cp: cannot create directory '{}': {}", entryTarget.string(), ec.message());
                        success = false;
                    }
                }
                else
                {
                    // Ensure parent directory exists
                    fs::create_directories(entryTarget.parent_path(), ec);
                    auto fileCopyOptions = fs::copy_options::none;
                    if (noClobber)
                        fileCopyOptions |= fs::copy_options::skip_existing;
                    else
                        fileCopyOptions |= fs::copy_options::overwrite_existing;
                    fs::copy_file(entry.path(), entryTarget, fileCopyOptions, ec);
                    if (ec)
                    {
                        error("cp: cannot copy '{}': {}", entry.path().string(), ec.message());
                        success = false;
                        continue;
                    }
                    writeOutput(std::format("'{}' -> '{}'\n", entry.path().string(), entryTarget.string()));
                }
            }
            // Copy the top-level directory itself (create it if needed)
            fs::create_directories(target, ec);
            if (ec)
            {
                error("cp: cannot create directory '{}': {}", target.string(), ec.message());
                success = false;
            }
        }
        else
        {
            if (fs::is_directory(srcPath))
            {
                fs::copy(srcPath, target, copyOptions, ec);
            }
            else
            {
                auto fileCopyOptions = fs::copy_options::none;
                if (noClobber)
                    fileCopyOptions |= fs::copy_options::skip_existing;
                else
                    fileCopyOptions |= fs::copy_options::overwrite_existing;
                fs::copy_file(srcPath, target, fileCopyOptions, ec);
            }

            if (ec)
            {
                error("cp: cannot copy '{}' to '{}': {}", src, target.string(), ec.message());
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

    namespace fs = std::filesystem;

    auto const dest = fs::path(paths.back());
    auto const sources = std::span(paths.data(), paths.size() - 1);
    auto const destIsDir = fs::is_directory(dest);

    if (sources.size() > 1 && !destIsDir)
    {
        error("mv: target '{}' is not a directory", dest.string());
        return 1;
    }

    auto success = true;
    for (auto const& src: sources)
    {
        auto const srcPath = fs::path(src);
        std::error_code ec;

        auto const srcStatus = fs::symlink_status(srcPath, ec);
        if (ec || !fs::exists(srcStatus))
        {
            error("mv: cannot stat '{}': No such file or directory", src);
            success = false;
            continue;
        }

        auto const target = destIsDir ? dest / srcPath.filename() : dest;

        // Check if target already exists
        if (fs::exists(target))
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
        fs::rename(srcPath, target, ec);
        if (ec)
        {
            // Cross-device move: fallback to copy + remove
            if (ec == std::errc::cross_device_link)
            {
                auto copyOptions = fs::copy_options::recursive | fs::copy_options::overwrite_existing;
                fs::copy(srcPath, target, copyOptions, ec);
                if (ec)
                {
                    error("mv: cannot move '{}' to '{}': {}", src, target.string(), ec.message());
                    success = false;
                    continue;
                }
                fs::remove_all(srcPath, ec);
                if (ec)
                {
                    error("mv: moved '{}' to '{}' but failed to remove source: {}",
                          src,
                          target.string(),
                          ec.message());
                    success = false;
                    continue;
                }
            }
            else
            {
                error("mv: cannot move '{}' to '{}': {}", src, target.string(), ec.message());
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
    namespace fs = std::filesystem;

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

    for (auto const& searchPath: options.searchPaths)
    {
        std::error_code ec;

        // Output the search path itself (depth 0)
        if (!options.minDepth.has_value() || options.minDepth.value() <= 0)
        {
            auto const status = fs::symlink_status(searchPath, ec);
            if (ec)
            {
                error("find: '{}': {}", searchPath.string(), ec.message());
                continue;
            }

            find::FindEntry entry {
                .path = searchPath,
                .filename = searchPath.filename().string(),
                .type = status.type(),
                .size = fs::is_regular_file(status) ? fs::file_size(searchPath, ec) : 0,
                .mtime = fs::last_write_time(searchPath, ec),
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

        auto dirIter =
            fs::recursive_directory_iterator(searchPath, fs::directory_options::skip_permission_denied, ec);
        if (ec)
            continue;

        for (auto const& dirEntry: dirIter)
        {
            auto const depth = dirIter.depth() + 1;

            if (options.maxDepth.has_value() && depth > options.maxDepth.value())
            {
                dirIter.disable_recursion_pending();
                continue;
            }

            if (options.minDepth.has_value() && depth < options.minDepth.value())
                continue;

            auto const status = dirEntry.symlink_status(ec);
            if (ec)
                continue;

            find::FindEntry entry {
                .path = dirEntry.path(),
                .filename = dirEntry.path().filename().string(),
                .type = status.type(),
                .size = dirEntry.is_regular_file(ec) ? dirEntry.file_size(ec) : 0,
                .mtime = dirEntry.last_write_time(ec),
                .depth = depth,
            };

            if (!expression || expression->evaluate(entry))
            {
                auto const output = dirEntry.path().string() + std::string(separator);
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
#if !defined(_WIN32)
        useColor = (isatty(outputFd) != 0);
#endif
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
            std::ifstream file(filePath);
            if (!file.is_open())
            {
                if (!opts.suppressErrors)
                    error("grep: {}: Permission denied", filePath.string());
                hasError = true;
                continue;
            }

            std::vector<std::string> lines;
            std::string line;
            while (std::getline(file, line))
                lines.push_back(std::move(line));

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
        std::cerr << std::format(
            "timeout: sending signal {} to command '{}'\n", opts.signal, opts.command[0]);
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
            std::cerr << std::format("timeout: sending SIGKILL to command '{}'\n", opts.command[0]);

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

} // namespace endo
