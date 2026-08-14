// SPDX-License-Identifier: Apache-2.0
#include <shell/Shell.hpp>
#include <shell/builtins/CatRenderMode.hpp>
#include <shell/commands/FindExpression.hpp>
#include <shell/commands/GrepCommand.hpp>
#include <shell/commands/KillCommand.hpp>
#include <shell/commands/PgrepCommand.hpp>
#include <shell/commands/PidofCommand.hpp>
#include <shell/commands/PkillCommand.hpp>
#include <shell/commands/ProcessMatch.hpp>
#include <shell/commands/TimeoutCommand.hpp>
#include <shell/history/RequiredPaths.hpp>

#include <tui/GenericSyntaxHighlighter.hpp>
#include <tui/ImageLoader.hpp>
#include <tui/ImageProvider.hpp>
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
#include <optional>
#include <random>
#include <ranges>
#include <span>
#include <sstream>
#include <thread>
#include <utility>

#include <fcntl.h>

#include <platform/Generator.hpp>
#include <platform/InterruptThrottle.hpp>
#include <platform/PathUtils.hpp>
#include <platform/Process.hpp>
#include <platform/ProcessProvider.hpp>
#include <platform/SignalHandler.hpp>
#include <platform/SystemInfo.hpp>
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

/// Reads from fd using poll() with 100ms timeout, checking for SIGINT between polls.
/// Calls onChunk(data, size) for each chunk of data read.
///
/// @return 0 on EOF, 1 on I/O error, 130 on SIGINT interruption.
int interruptibleReadLoop(endo::NativeHandle fd, auto const& onChunk)
{
    using namespace endo;
    using namespace endo::platform;

    std::array<char, 4096> buffer {};

    while (true)
    {
        SignalHandler::processSignalFd();
        if (SignalHandler::hasPendingSigint())
        {
            SignalHandler::clearPendingSigint();
            return 130;
        }
#if !defined(_WIN32)
        pollfd pfd { .fd = fd, .events = POLLIN, .revents = 0 };
        auto const pollResult = poll(&pfd, 1, 100);
        if (pollResult < 0)
        {
            if (errno == EINTR)
                continue;
            return 1;
        }
        if ((pfd.revents & (POLLERR | POLLNVAL)) != 0)
            return 1;
        if (pollResult == 0)
            continue;
#else
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
#endif
        auto const bytesRead = platformRead(fd, buffer.data(), buffer.size());
        if (bytesRead < 0)
            return 1;
        if (bytesRead == 0)
            break;
        onChunk(buffer.data(), static_cast<size_t>(bytesRead));
    }
    return 0;
}

/// Reads all data from fd interruptibly.
///
/// @return {data, exitCode} where exitCode is 0 on EOF or 130 on SIGINT.
std::pair<std::string, int> interruptibleReadAll(endo::NativeHandle fd)
{
    std::string data;
    auto const exitCode =
        interruptibleReadLoop(fd, [&](char const* buf, size_t len) { data.append(buf, len); });
    return { std::move(data), exitCode };
}

/// @brief A TerminalOutput that writes to a caller-supplied file descriptor.
///
/// tui::TerminalOutput composes escape sequences into an internal buffer and, by
/// default, flushes them to standard output. Retargeting the sink lets `cat`
/// render to whatever descriptor the shell's redirection model handed it.
class FdTerminalOutput final: public tui::TerminalOutput
{
  public:
    /// @param fd The descriptor to write to.
    /// @param columns Terminal width in cells.
    /// @param rows Terminal height in cells.
    FdTerminalOutput(endo::NativeHandle fd, int columns, int rows) noexcept:
        _fd(fd), _columns(columns), _rows(rows)
    {
    }

    [[nodiscard]] auto columns() const noexcept -> int override { return _columns; }

    [[nodiscard]] auto rows() const noexcept -> int override { return _rows; }

    /// Dimensions come from the shell's injected TTY, not from an ioctl on stdout.
    void updateDimensions() override {}

  protected:
    void writeToDestination(std::string_view bytes) override
    {
        [[maybe_unused]] auto const written = endo::platformWrite(_fd, bytes.data(), bytes.size());
    }

  private:
    endo::NativeHandle _fd;
    int _columns;
    int _rows;
};

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

int Shell::renderMarkdownDocument(NativeHandle outputFd,
                                  std::string_view markdownContent,
                                  std::filesystem::path const& baseDir,
                                  int indent)
{
    // Fallback cell metrics for terminals that do not report pixel dimensions.
    static constexpr auto FallbackCellWidthPx = 8;
    static constexpr auto FallbackCellHeightPx = 16;
    static constexpr auto FallbackColumns = 80;
    static constexpr auto FallbackRows = 24;

    auto const size = _tty.getSize();
    auto const columns = size.has_value() && size->cols > 0 ? int { size->cols } : FallbackColumns;
    auto const rows = size.has_value() && size->rows > 0 ? int { size->rows } : FallbackRows;
    auto const cellWidthPx = size.has_value() && size->xpixel > 0 && size->cols > 0
                                 ? size->xpixel / size->cols
                                 : FallbackCellWidthPx;
    auto const cellHeightPx = size.has_value() && size->ypixel > 0 && size->rows > 0
                                  ? size->ypixel / size->rows
                                  : FallbackCellHeightPx;

    auto output = FdTerminalOutput(outputFd, columns, rows);
    auto imageProvider =
        tui::FilesystemImageProvider(tui::ImageRenderConfig { .baseDir = baseDir,
                                                              .cellWidthPx = cellWidthPx,
                                                              .cellHeightPx = cellHeightPx,
                                                              .maxColumns = columns - indent },
                                     [this] { return _sixelCapability->supportsSixel(); });

    auto renderer = tui::MarkdownRenderer(output);
    renderer.setMaxWidth(columns);
    renderer.setIndent(indent);
    renderer.setFullWidthMode(true); // double-width/height H1 and H2 titles
    renderer.setImageProvider(&imageProvider);
    renderer.render(markdownContent);
    output.flush();
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
                {
                    suppressNewline = true;
                }
                else if (arg[j] == 'e')
                {
                    interpretEscapes = true;
                }
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
    auto markdownIndent = DefaultMarkdownIndent;
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
        if (arg == "--indent" || arg.starts_with("--indent="))
        {
            std::string_view indentValue;
            if (arg == "--indent")
            {
                if (i + 1 >= catArgs.size())
                {
                    error("cat: --indent requires a value");
                    return 1;
                }
                indentValue = catArgs[++i];
            }
            else
            {
                indentValue = arg.substr(9); // skip "--indent="
            }
            int value = 0;
            auto const [ptr, ec] =
                std::from_chars(indentValue.data(), indentValue.data() + indentValue.size(), value);
            if (ec != std::errc {} || ptr != indentValue.data() + indentValue.size() || value < 0)
            {
                error("cat: --indent: invalid value '{}'", indentValue);
                return 1;
            }
            markdownIndent = value;
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
            "| `--indent N` | Left margin for rendered markdown, in columns (default 1) |\n"
            "| `--raw` | Disable all rendering (markdown, images, highlighting) |\n"
            "| `-h`, `--help` | Display this help |\n"
            "\n"
            "## Rendering\n"
            "\n"
            "On a terminal, markdown files render as formatted documents: tables get\n"
            "borders, links become clickable (OSC 8), titles use double-width and\n"
            "double-height characters, and local images embed as Sixel graphics when the\n"
            "terminal supports them.\n"
            "\n"
            "Rendering is suppressed when the output is redirected or piped, when `--raw`\n"
            "is given, or when a line-processing flag such as `-n` asks for the source\n"
            "text. Set `ENDO_SIXEL=0` to disable inline images while keeping the rest.\n");
    }

    // Detect whether output goes to a TTY for syntax highlighting
    bool const outputIsTty = isTerminal(outputFd);

    int lineNumber = 1;
    bool lastLineWasBlank = false;

    auto processContent = [&](std::string const& content, tui::LanguageId language) {
        auto highlightState = tui::HighlightState::Normal;
        auto const& theme = tui::currentTheme();
        // Highlighting is a rendering, so --raw suppresses it too.
        auto const highlight = shouldHighlightCatOutput(outputIsTty, rawMode, language);
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

    bool const hasProcessingFlags =
        numberLines || numberNonBlank || squeezeBlank || showEnds || showTabs || rangeSpecified;

    bool success = true;

    if (files.empty())
        files.emplace_back("-");

    for (auto const& file: files)
    {
        if (file == "-")
        {
            if (hasProcessingFlags)
            {
                auto [content, exitCode] = interruptibleReadAll(stdinFd);
                if (exitCode != 0)
                    return exitCode;
                processContent(content, tui::LanguageId::None);
            }
            else
            {
                // Stream directly for plain cat (best UX for interactive stdin)
                auto const exitCode = interruptibleReadLoop(stdinFd, [outputFd](char const* buf, size_t len) {
                    [[maybe_unused]] auto written = platformWrite(outputFd, buf, len);
                });
                if (exitCode != 0)
                    return exitCode;
            }
        }
        else
        {
            auto const ext = std::filesystem::path(file).extension().string();
            auto const renderContext =
                CatRenderContext { .rawMode = rawMode,
                                   .outputIsTty = outputIsTty,
                                   .hasProcessingFlags = hasProcessingFlags,
                                   .isImageExt = tui::isImageExtension(ext),
                                   .forceImage = imageColumns.has_value() || imageRows.has_value(),
                                   .language = tui::detectLanguageFromPath(file) };
            auto const renderMode = chooseCatRenderMode(renderContext);

            if (renderMode == CatRenderMode::SixelImage)
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
            auto [content, exitCode] = interruptibleReadAll(fd);
            _processManager.closeHandle(fd);
            if (exitCode != 0)
                return exitCode;

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

            if (renderMode == CatRenderMode::Markdown)
            {
                [[maybe_unused]] auto const rendered = renderMarkdownDocument(
                    outputFd, content, std::filesystem::path(file).parent_path(), markdownIndent);
            }
            else if (renderMode == CatRenderMode::Raw)
            {
                writeOutput(content);
            }
            else
            {
                processContent(content, renderContext.language);
            }
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
                // Collect every entry via the interruptible lazy walk first (so enumerating a
                // huge tree is itself abortable), then remove leaf-to-root. We collect rather
                // than delete during the walk because mutating the tree we are iterating would
                // be undefined. Both the collect and the delete phases poll for Ctrl+C, and we
                // only print in verbose mode — this unifies what used to be separate verbose /
                // removeAll() paths, the latter of which was a single non-interruptible call.
                auto rmThrottle = InterruptThrottle { 1 };
                std::error_code walkError;
                auto entries = std::vector<std::filesystem::path> {};
                for (auto const& entry: _fs.walkDirectoryRecursive(path, &walkError))
                {
                    if (rmThrottle.pending())
                        return 130;
                    entries.push_back(entry.path);
                }
                // If the tree could not be fully enumerated, report it and skip removal so we do
                // not delete a partial subtree (and leave the still-referenced remainder dangling).
                if (walkError)
                {
                    error("rm: cannot remove '{}': {}", path, walkError.message());
                    success = false;
                    continue;
                }
                std::ranges::reverse(entries); // children before their parents

                auto allOk = true;
                for (auto const& entry: entries)
                {
                    if (rmThrottle.pending())
                        return 130;
                    auto const removeResult = _fs.remove(entry);
                    if (!removeResult.has_value() || !removeResult.value())
                    {
                        error("rm: cannot remove '{}': {}",
                              platform::normalizePath(entry),
                              removeResult.has_value() ? "Unknown error" : removeResult.error());
                        allOk = false;
                        break;
                    }
                    if (verbose)
                        writeOutput(std::format("removed '{}'\n", platform::normalizePath(entry)));
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
                    else if (verbose)
                    {
                        writeOutput(std::format("removed '{}'\n", path));
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
        error("cp: target '{}' is not a directory", platform::normalizePath(dest));
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
            // Recursive directory copy: stream entries from the lazy walk and copy each as it
            // arrives, polling for Ctrl+C between entries (interval 1 — each entry does real I/O)
            // so a large tree aborts promptly. The walk yields parents before their contents, which
            // is the order cp needs (create a directory, then populate it). We copy only — never
            // mutate the source while walking it. walkError captures an enumeration failure so a
            // partial copy is reported rather than silently succeeding.

            // Create the top-level target directory
            if (auto const mkResult = _fs.createDirectories(target); !mkResult.has_value())
            {
                error("cp: cannot create directory '{}': {}",
                      platform::normalizePath(target),
                      mkResult.error());
                success = false;
                continue;
            }

            auto cpThrottle = InterruptThrottle { 1 };
            std::error_code walkError;
            for (auto const& entry: _fs.walkDirectoryRecursive(srcPath, &walkError))
            {
                if (cpThrottle.pending())
                    return 130;

                auto const relativePath = entry.path.lexically_relative(srcPath);
                auto const entryTarget = target / relativePath;

                if (entry.isDirectory)
                {
                    if (auto const mkResult = _fs.createDirectories(entryTarget); !mkResult.has_value())
                    {
                        error("cp: cannot create directory '{}': {}",
                              platform::normalizePath(entryTarget),
                              mkResult.error());
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
                              platform::normalizePath(entryTarget.parent_path()),
                              mkResult.error());
                        success = false;
                        continue;
                    }
                    if (auto const cpResult = _fs.copyFile(entry.path, entryTarget, overwrite);
                        !cpResult.has_value())
                    {
                        error("cp: cannot copy '{}': {}",
                              platform::normalizePath(entry.path),
                              cpResult.error());
                        success = false;
                        continue;
                    }
                    if (verbose)
                        writeOutput(std::format("'{}' -> '{}'\n",
                                                platform::normalizePath(entry.path),
                                                platform::normalizePath(entryTarget)));
                }
            }

            // The source tree could not be fully enumerated; report it rather than reporting
            // success for a partial copy.
            if (walkError)
            {
                error("cp: cannot copy '{}': {}", src, walkError.message());
                success = false;
            }
        }
        else
        {
            // Single file copy -- skip silently if no-clobber and target exists
            if (noClobber && _fs.exists(target))
                continue;

            if (auto const cpResult = _fs.copyFile(srcPath, target, overwrite); !cpResult.has_value())
            {
                error("cp: cannot copy '{}' to '{}': {}",
                      src,
                      platform::normalizePath(target),
                      cpResult.error());
                success = false;
                continue;
            }

            if (verbose)
                writeOutput(std::format("'{}' -> '{}'\n", src, platform::normalizePath(target)));
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

    // A single-source move whose destination names the very same entry as the source,
    // differing only in the lettercase of the final path component (e.g. `mv foo Foo`),
    // is a pure rename-to-recase. On case-insensitive filesystems the destination appears
    // to already exist (it *is* the source) and isDirectory(dest) reports the source's own
    // type, so without this guard the move would be misrouted into the directory (target
    // `Foo/foo`) or rejected as a self-overwrite. Detect it up front so the entry itself
    // is renamed; NativeFileSystem::rename performs the underlying recase safely.
    auto const caseOnlyRename =
        sources.size() == 1 && platform::isCaseOnlyRename(std::filesystem::path(sources.front()), dest);

    if (sources.size() > 1 && !destIsDir)
    {
        error("mv: target '{}' is not a directory", platform::normalizePath(dest));
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

        auto const target = (destIsDir && !caseOnlyRename) ? dest / srcPath.filename() : dest;

        // Check if target already exists. A case-only recase necessarily collides with the
        // source itself on case-insensitive filesystems, so skip the clobber/overwrite gate.
        if (!caseOnlyRename && _fs.exists(target))
        {
            if (noClobber)
                continue;

            if (interactive && !force)
            {
                auto const prompt = std::format("mv: overwrite '{}'? ", platform::normalizePath(target));
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
                error("mv: cannot move '{}' to '{}': {}", src, platform::normalizePath(target), renameError);
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
                    error("mv: cannot move '{}' to '{}': {}",
                          src,
                          platform::normalizePath(target),
                          mkResult.error());
                    success = false;
                    continue;
                }
                // Cross-device move falls back to a recursive copy (then the source is removed
                // below). Stream the lazy walk and copy each entry as it arrives, polling for
                // Ctrl+C between entries (interval 1 — each entry does real I/O). Copy only — the
                // source is not touched until the walk has fully completed. walkError captures an
                // enumeration failure: if the source could not be fully read, we treat it as a copy
                // failure so the source is NOT removed (otherwise a partial copy + removeAll would
                // lose the un-copied remainder).
                auto mvThrottle = InterruptThrottle { 1 };
                std::error_code walkError;
                for (auto const& entry: _fs.walkDirectoryRecursive(srcPath, &walkError))
                {
                    if (mvThrottle.pending())
                        return 130;

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

                // A failed/partial enumeration must abort the move so removeAll does not delete a
                // source that was only partially copied.
                if (walkError && !copyFailed)
                {
                    copyFailed = true;
                    copyError = walkError.message();
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
                error("mv: cannot move '{}' to '{}': {}", src, platform::normalizePath(target), copyError);
                success = false;
                continue;
            }

            auto const removeResult = _fs.removeAll(srcPath);
            if (!removeResult.has_value())
            {
                error("mv: moved '{}' to '{}' but failed to remove source: {}",
                      src,
                      platform::normalizePath(target),
                      removeResult.error());
                success = false;
                continue;
            }
        }

        if (verbose)
            writeOutput(std::format("'{}' -> '{}'\n", src, platform::normalizePath(target)));
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

    context.setResult(static_cast<CoreVM::CoreNumber>(_exitCode));
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

    // Only stat for size/mtime when a predicate actually needs it; otherwise `find -name`/`-type`/
    // `-path` would issue two wasted stat syscalls (fileSize + lastWriteTime) per entry on a large
    // tree. The expression tree is scanned once here.
    auto const needsSize = expression && expression->requiresSize();
    auto const needsMtime = expression && expression->requiresMtime();

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
                error("find: '{}': No such file or directory", platform::normalizePath(searchPath));
                continue;
            }

            auto const ftype = pathFileType(searchPath);
            auto const isFile = ftype == std::filesystem::file_type::regular;
            auto const size =
                (needsSize && isFile) ? _fs.fileSize(searchPath).value_or(0) : std::uintmax_t {};
            auto const mtime =
                needsMtime ? _fs.lastWriteTime(searchPath).value_or(std::filesystem::file_time_type {})
                           : std::filesystem::file_time_type {};

            find::FindEntry entry {
                .path = searchPath,
                .filename = searchPath.filename().string(),
                .type = ftype,
                .size = size,
                .mtime = mtime,
                .depth = 0,
            };

            if (!expression || expression->evaluate(entry))
            {
                auto const output = platform::normalizePath(searchPath) + std::string(separator);
                platformWrite(outputFd, output.data(), output.size());
            }
        }

        // Skip recursion if maxdepth is 0
        if (options.maxDepth.has_value() && options.maxDepth.value() == 0)
            continue;

        // Stream entries lazily from the coroutine walk rather than materializing the whole
        // subtree first: output appears immediately, and we poll for Ctrl+C between entries so a
        // long walk aborts promptly with exit code 130 (128 + SIGINT), matching sleep/tail.
        auto throttle = InterruptThrottle {};

        for (auto const& dirEntry: _fs.walkDirectoryRecursive(searchPath))
        {
            if (throttle.pending())
                return 130;

            // Depth is supplied by the walk (root-relative: direct child = 1), so no per-entry
            // lexically_relative + distance recompute is needed here.
            auto const depth = dirEntry.depth;

            if (options.maxDepth.has_value() && depth > options.maxDepth.value())
                continue;

            if (options.minDepth.has_value() && depth < options.minDepth.value())
                continue;

            auto const ftype = entryFileType(dirEntry);
            auto const size = (needsSize && dirEntry.isRegularFile) ? _fs.fileSize(dirEntry.path).value_or(0)
                                                                    : std::uintmax_t {};
            auto const mtime =
                needsMtime ? _fs.lastWriteTime(dirEntry.path).value_or(std::filesystem::file_time_type {})
                           : std::filesystem::file_time_type {};

            find::FindEntry entry {
                .path = dirEntry.path,
                .filename = dirEntry.path.filename().string(),
                .type = ftype,
                .size = size,
                .mtime = mtime,
                .depth = depth,
            };

            if (!expression || expression->evaluate(entry))
            {
                auto const output = platform::normalizePath(dirEntry.path) + std::string(separator);
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
    auto const toTerminal = isTerminal(outputFd);
    bool useColor = false;
    if (opts.colorMode == grep::ColorMode::Always)
    {
        useColor = true;
    }
    else if (opts.colorMode == grep::ColorMode::Auto)
    {
        useColor = toTerminal;
    }

    // `--color=never` is grep's conventional "give me plain output" switch, so it suppresses
    // hyperlinks too — a caller asking for undecorated text means all decoration, not just SGR.
    // Terminal-ness gates them independently of the resolved color mode, so `--color=always`
    // into a pipe still yields no OSC 8 bytes in the captured output.
    auto const useHyperlinks = _hyperlinks && toTerminal && opts.colorMode != grep::ColorMode::Never;
    auto const render = grep::GrepRenderOptions {
        .useColor = useColor,
        .useHyperlinks = useHyperlinks,
        // Only the linking path reads these, and resolving the base directory is a getcwd.
        .uriHost = useHyperlinks ? platform::cachedHostName() : std::string {},
        .baseDirectory = useHyperlinks ? platform::normalizePath(_fs.currentPath()) : std::string {},
    };

    // Output + error writer lambdas
    auto const writer = [outputFd](std::string_view sv) {
        platformWrite(outputFd, sv.data(), sv.size());
    };

    bool hasError = false;
    auto const errWriter = [this](std::string_view sv) {
        error("{}", sv.substr(0, sv.size() - (sv.ends_with('\n') ? 1 : 0)));
    };

    // One throttle drives the whole grep run (collect, read, search) at a single cadence; the grep
    // helpers poll it non-consumingly (peek) and leave the pending flag set, so `interrupted()`
    // consumes it once and the driver exits with 130.
    auto throttle = InterruptThrottle {};
    auto const interrupted = [] {
        if (!SignalHandler::hasPendingSigint())
            return false;
        SignalHandler::clearPendingSigint();
        return true;
    };

    auto files = grep::collectFiles(_fs, opts, errWriter, hasError, &throttle);
    if (interrupted())
        return 130;

    // Determine file count for filename display
    auto const readingStdin = opts.files.empty() && !opts.recursive;
    auto const fileCount = readingStdin ? 0uz : files.size();
    auto const showFilename = opts.showFilename(fileCount);

    size_t totalMatches = 0;

    if (readingStdin)
    {
        // Read from stdin
        auto [stdinData, exitCode] = interruptibleReadAll(stdinFd);
        if (exitCode != 0)
            return exitCode;

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

        totalMatches = grep::searchLines(
            lines, *regex, opts, "(standard input)", showFilename, render, writer, &throttle);
        if (interrupted())
            return 130;
    }
    else
    {
        // Search files
        for (auto const& filePath: files)
        {
            if (throttle.pending())
                return 130;

            // Binary detection
            if (opts.skipBinary && grep::isBinaryFile(_fs, filePath))
                continue;

            // Read file
            auto fileStream = _fs.openRead(filePath);
            if (!fileStream || !fileStream->good())
            {
                if (!opts.suppressErrors)
                    error("grep: {}: Permission denied", platform::normalizePath(filePath));
                hasError = true;
                continue;
            }

            // Read the file line by line, polling for Ctrl+C so a large file stays interruptible
            // during the read (not just during the search below) and moving each line into the
            // buffer to avoid a per-line copy.
            std::vector<std::string> lines;
            std::string line;
            while (std::getline(*fileStream, line))
            {
                if (throttle.pending())
                    return 130;
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                lines.push_back(std::move(line));
            }

            auto const matches = grep::searchLines(lines,
                                                   *regex,
                                                   opts,
                                                   platform::normalizePath(filePath),
                                                   showFilename,
                                                   render,
                                                   writer,
                                                   &throttle);
            if (interrupted())
                return 130;
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
        config.markBackgroundGroup(); // New process group, shielded from the console's Ctrl+C (Windows)

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
        static constexpr std::pair<int, std::string_view> Signals[] = {
            {  1, "HUP" }, {  2, "INT"  }, {  3, "QUIT" }, {  4, "ILL"  }, {  5, "TRAP" },
            {  6, "ABRT"}, {  7, "BUS"  }, {  8, "FPE"  }, {  9, "KILL" }, { 10, "USR1" },
            { 11, "SEGV"}, { 12, "USR2" }, { 13, "PIPE" }, { 14, "ALRM" }, { 15, "TERM" },
        };
        // clang-format on
        std::string output;
        for (auto const& [num, name]: Signals)
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
// pkill
// ---------------------------------------------------------------------------

int Shell::executeInlinePkill(CoreVM::CoreStringArray const& args, NativeHandle outputFd)
{
    auto pkillArgs = std::vector<std::string> {};
    for (auto const i: std::views::iota(1uz, args.size()))
        pkillArgs.push_back(args.at(i));

    auto parsed = pkill_cmd::parsePkillArgs(pkillArgs);
    if (!parsed.has_value())
    {
        error("{}", parsed.error());
        return 2;
    }

    auto const& opts = parsed.value();

    if (opts.showHelp)
        return renderMarkdownHelp(outputFd,
                                  "# pkill\n"
                                  "\n"
                                  "Send signals to processes matched by name or command-line pattern.\n"
                                  "\n"
                                  "## Usage\n"
                                  "\n"
                                  "`pkill [OPTIONS] [-SIGNAL] PATTERN`\n"
                                  "\n"
                                  "## Options\n"
                                  "\n"
                                  "| Option | Description |\n"
                                  "|---|---|\n"
                                  "| `-SIGNAL` | Signal to send by name or number (default: `TERM`) |\n"
                                  "| `-s SIGNAL` | Signal to send (POSIX style) |\n"
                                  "| `-f` | Match PATTERN against the full command line |\n"
                                  "| `-x` | Require an exact (anchored) match |\n"
                                  "| `-i` | Case-insensitive pattern match |\n"
                                  "| `-c` | Print the count of matched processes |\n"
                                  "| `-l` | List matches (`pid name`) without signalling |\n"
                                  "| `-n` | Match only the newest (highest PID) process |\n"
                                  "| `-o` | Match only the oldest (lowest PID) process |\n"
                                  "| `-u USER[,USER]` | Only match processes owned by listed users |\n"
                                  "| `-h`, `--help` | Show this help message |\n"
                                  "\n"
                                  "## Notes\n"
                                  "\n"
                                  "- PATTERN is an ECMAScript regular expression.\n"
                                  "- The shell's own process is always excluded from matches.\n"
                                  "- Matching runs against the platform's most reliable command field "
                                  "(argv[0] on Linux, the command string on Darwin/Windows); `-f` is "
                                  "accepted for CLI compatibility but targets the same field.\n"
                                  "\n"
                                  "## Examples\n"
                                  "\n"
                                  "| Example | Description |\n"
                                  "|---|---|\n"
                                  "| `pkill sleep` | Send `SIGTERM` to every process named `sleep` |\n"
                                  "| `pkill -9 firefox` | Send `SIGKILL` to every firefox process |\n"
                                  "| `pkill -f \"python myscript\"` | Match against the full command line |\n"
                                  "| `pkill -l nginx` | List matching processes without signalling |\n"
                                  "| `pkill -u alice bash` | Only signal alice's bash processes |\n");

    auto provider = createNativeProcessProvider();
    auto entries = provider->listProcesses();

    // Full-cmdline matching (-f) is accepted for CLI compatibility but matches
    // the same field since richer data is not available across all platforms.
    auto matched = process_match::filterProcesses(entries,
                                                  process_match::MatchOptions {
                                                      .pattern = opts.pattern,
                                                      .exactMatch = opts.exactMatch,
                                                      .caseInsensitive = opts.caseInsensitive,
                                                      .userFilter = opts.userFilter,
                                                      .newestOnly = opts.newestOnly,
                                                      .oldestOnly = opts.oldestOnly,
                                                      .excludePid = static_cast<int64_t>(_shellPid),
                                                  });
    if (!matched.has_value())
    {
        error("pkill: {}", matched.error());
        return 2;
    }

    auto const& matches = matched.value();
    if (matches.empty())
        return 1;

    if (opts.listOnly)
    {
        auto output = std::string {};
        for (auto const& m: matches)
            output += std::format("{} {}\n", m.pid, m.command);
        [[maybe_unused]] auto const written = platformWrite(outputFd, output.data(), output.size());
        return 0;
    }

    if (opts.countOnly)
    {
        auto const output = std::format("{}\n", matches.size());
        [[maybe_unused]] auto const written = platformWrite(outputFd, output.data(), output.size());
    }

    auto exitCode = 0;
    for (auto const& m: matches)
    {
        auto const result = _processManager.sendSignal(static_cast<ProcessId>(m.pid), opts.signal);
        if (!result.has_value())
        {
            error("pkill: ({}) - {}", m.pid, toString(result.error()));
            exitCode = 1;
        }
    }
    return exitCode;
}

// ---------------------------------------------------------------------------
// pgrep
// ---------------------------------------------------------------------------

int Shell::executeInlinePgrep(CoreVM::CoreStringArray const& args, NativeHandle outputFd)
{
    auto pgrepArgs = std::vector<std::string> {};
    for (auto const i: std::views::iota(1uz, args.size()))
        pgrepArgs.push_back(args.at(i));

    auto parsed = pgrep_cmd::parsePgrepArgs(pgrepArgs);
    if (!parsed.has_value())
    {
        error("{}", parsed.error());
        return 2;
    }

    auto const& opts = parsed.value();

    if (opts.showHelp)
        return renderMarkdownHelp(outputFd,
                                  "# pgrep\n"
                                  "\n"
                                  "Print PIDs of processes matched by name or command-line pattern.\n"
                                  "\n"
                                  "## Usage\n"
                                  "\n"
                                  "`pgrep [OPTIONS] PATTERN`\n"
                                  "\n"
                                  "## Options\n"
                                  "\n"
                                  "| Option | Description |\n"
                                  "|---|---|\n"
                                  "| `-f` | Match PATTERN against the full command line |\n"
                                  "| `-x` | Require an exact (anchored) match |\n"
                                  "| `-i` | Case-insensitive pattern match |\n"
                                  "| `-v` | Invert the match: select non-matching processes |\n"
                                  "| `-c` | Print only the count of matched processes |\n"
                                  "| `-l` | Print the process name along with the PID |\n"
                                  "| `-n` | Match only the newest (highest PID) process |\n"
                                  "| `-o` | Match only the oldest (lowest PID) process |\n"
                                  "| `-u USER[,USER]` | Only match processes owned by listed users |\n"
                                  "| `-d DELIM` | Delimiter between printed PIDs (default: newline) |\n"
                                  "| `-h`, `--help` | Show this help message |\n"
                                  "\n"
                                  "## Notes\n"
                                  "\n"
                                  "- PATTERN is an ECMAScript regular expression.\n"
                                  "- Unlike `pkill`, the shell's own process can appear in the results.\n"
                                  "- Matching runs against the platform's most reliable command field "
                                  "(argv[0] on Linux, the command string on Darwin/Windows); `-f` is "
                                  "accepted for CLI compatibility but targets the same field.\n"
                                  "- Exit status is 0 if at least one process matched, 1 otherwise.\n"
                                  "\n"
                                  "## Examples\n"
                                  "\n"
                                  "| Example | Description |\n"
                                  "|---|---|\n"
                                  "| `pgrep sleep` | Print PIDs of every process matching `sleep` |\n"
                                  "| `pgrep -x sleep` | Only exact name matches |\n"
                                  "| `pgrep -l ssh` | Print `pid name` for each match |\n"
                                  "| `pgrep -c -u alice bash` | Count alice's bash processes |\n"
                                  "| `pgrep -d , sleep` | Separate PIDs with a comma |\n");

    auto provider = createNativeProcessProvider();
    auto entries = provider->listProcesses();

    // Full-cmdline matching (-f) is accepted for CLI compatibility but matches
    // the same field since richer data is not available across all platforms.
    auto matched = process_match::filterProcesses(entries,
                                                  process_match::MatchOptions {
                                                      .pattern = opts.pattern,
                                                      .exactMatch = opts.exactMatch,
                                                      .caseInsensitive = opts.caseInsensitive,
                                                      .invert = opts.invert,
                                                      .userFilter = opts.userFilter,
                                                      .newestOnly = opts.newestOnly,
                                                      .oldestOnly = opts.oldestOnly,
                                                  });
    if (!matched.has_value())
    {
        error("pgrep: {}", matched.error());
        return 2;
    }

    auto const& matches = matched.value();

    if (opts.countOnly)
    {
        auto const output = std::format("{}\n", matches.size());
        [[maybe_unused]] auto const written = platformWrite(outputFd, output.data(), output.size());
        return matches.empty() ? 1 : 0;
    }

    if (matches.empty())
        return 1;

    auto output = std::string {};
    for (auto const& m: matches)
    {
        if (!output.empty())
            output += opts.delimiter;
        if (opts.listName)
            output += std::format("{} {}", m.pid, m.command);
        else
            output += std::format("{}", m.pid);
    }
    output += '\n';
    [[maybe_unused]] auto const written = platformWrite(outputFd, output.data(), output.size());
    return 0;
}

// ---------------------------------------------------------------------------
// pidof
// ---------------------------------------------------------------------------

int Shell::executeInlinePidof(CoreVM::CoreStringArray const& args, NativeHandle outputFd)
{
    auto pidofArgs = std::vector<std::string> {};
    for (auto const i: std::views::iota(1uz, args.size()))
        pidofArgs.push_back(args.at(i));

    auto parsed = pidof_cmd::parsePidofArgs(pidofArgs);
    if (!parsed.has_value())
    {
        error("{}", parsed.error());
        return 2;
    }

    auto const& opts = parsed.value();

    if (opts.showHelp)
        return renderMarkdownHelp(
            outputFd,
            "# pidof\n"
            "\n"
            "Print PIDs of running processes matching program names.\n"
            "\n"
            "## Usage\n"
            "\n"
            "`pidof [OPTIONS] PROGRAM...`\n"
            "\n"
            "## Options\n"
            "\n"
            "| Option | Description |\n"
            "|---|---|\n"
            "| `-s` | Single shot: print at most one PID |\n"
            "| `-q` | Quiet: print nothing, only set the exit status |\n"
            "| `-S SEP`, `--separator SEP` | Separator between printed PIDs (default: space) |\n"
            "| `-d SEP` | Alias for `-S` (sysvinit compatibility) |\n"
            "| `-o PID[,PID]` | Omit the listed PIDs from the result (repeatable) |\n"
            "| `-h`, `--help` | Show this help message |\n"
            "\n"
            "## Notes\n"
            "\n"
            "- Program names are matched exactly against the platform's process "
            "name (argv[0] or its basename on Linux, the command name on "
            "Darwin/Windows).\n"
            "- On Windows, matching is case-insensitive and a trailing `.exe` is "
            "ignored.\n"
            "- PIDs are printed on one line, newest (highest PID) first.\n"
            "- The shell's own process is included when it matches; use `-o` to "
            "exclude specific PIDs.\n"
            "- Exit status is 0 if at least one PID was found, 1 otherwise.\n"
            "\n"
            "## Examples\n"
            "\n"
            "| Example | Description |\n"
            "|---|---|\n"
            "| `pidof sleep` | Print PIDs of every process named `sleep` |\n"
            "| `pidof -s sleep` | Print only the newest matching PID |\n"
            "| `pidof -q sleep` | No output; exit 0 only if a `sleep` process exists |\n"
            "| `pidof -d , sleep` | Separate PIDs with a comma |\n"
            "| `pidof -o 1234 sleep` | Exclude PID 1234 from the result |\n");

    auto provider = createNativeProcessProvider();
    auto entries = provider->listProcesses();

    auto const pids = pidof_cmd::findPids(entries, opts, nativeProcessNameMatchPolicy());
    if (pids.empty())
        return 1;

    if (!opts.quiet)
    {
        auto output = std::string {};
        for (auto const pid: pids)
        {
            if (!output.empty())
                output += opts.separator;
            output += std::format("{}", pid);
        }
        output += '\n';
        [[maybe_unused]] auto const written = platformWrite(outputFd, output.data(), output.size());
    }
    return 0;
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
// pwd
// ---------------------------------------------------------------------------

int Shell::executeInlinePwd(CoreVM::CoreStringArray const& args, NativeHandle outputFd)
{
    for (auto const i: std::views::iota(1uz, args.size()))
    {
        std::string_view arg = args.at(i);
        if (arg == "-h" || arg == "--help")
            return renderMarkdownHelp(outputFd,
                                      "# pwd\n"
                                      "\n"
                                      "Print the current working directory.\n"
                                      "\n"
                                      "## Usage\n"
                                      "\n"
                                      "`pwd`\n"
                                      "\n"
                                      "## Options\n"
                                      "\n"
                                      "| Option | Description |\n"
                                      "|--------|-------------|\n"
                                      "| `-h`, `--help` | Display this help |\n");
    }

    auto output = std::format("{}\n", _env.currentDirectory());
    [[maybe_unused]] auto written = platformWrite(outputFd, output.data(), output.size());
    return 0;
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
// cal
// ---------------------------------------------------------------------------

namespace
{

    /// SGR strings used to style the calendar. Kept as a data table so the render
    /// loop is free of hardcoded escape sequences.
    struct CalSgr
    {
        std::string_view reset;
        std::string_view title;      ///< Month + year header
        std::string_view weekdayRow; ///< Header row of weekday abbreviations
        std::string_view weekend;    ///< Foreground color for Sat/Sun cells
        std::string_view today;      ///< Highlight for the current day
    };

    constexpr CalSgr CalStyled = {
        .reset = "\x1b[0m",
        .title = "\x1b[1;38;5;220m", // bold gold
        .weekdayRow = "\x1b[1;2m",
        .weekend = "\x1b[38;5;39m",    // cyan
        .today = "\x1b[1;7;38;5;226m", // bold + reverse + yellow
    };

    constexpr CalSgr CalPlain = {
        .reset = "",
        .title = "",
        .weekdayRow = "",
        .weekend = "",
        .today = "",
    };

    /// English fallback month names. `std::chrono::format` is locale-aware, but
    /// for wide portability we build the header from a static table.
    constexpr std::array<std::string_view, 12> CalMonthNames = {
        "January", "February", "March",     "April",   "May",      "June",
        "July",    "August",   "September", "October", "November", "December",
    };

    /// Short weekday labels starting with Monday (index 0). Locale-independent.
    constexpr std::array<std::string_view, 7> CalWeekdayShort = {
        "Mo", "Tu", "We", "Th", "Fr", "Sa", "Su",
    };

    /// @brief Returns the zero-based column index (0..6) for a given weekday when
    /// the week starts with @p firstDay.
    constexpr size_t weekdayColumn(std::chrono::weekday wd, std::chrono::weekday firstDay) noexcept
    {
        // std::chrono::weekday::c_encoding(): 0 = Sunday, 1 = Monday, ..., 6 = Saturday.
        auto const day = static_cast<int>(wd.c_encoding());
        auto const first = static_cast<int>(firstDay.c_encoding());
        return static_cast<size_t>((day - first + 7) % 7);
    }

    /// @brief Number of days in a calendar month.
    constexpr unsigned daysInMonth(std::chrono::year_month const& ym) noexcept
    {
        auto const last =
            std::chrono::year_month_day_last { ym.year(), std::chrono::month_day_last { ym.month() } };
        return last.day().operator unsigned int();
    }

    struct CalStyle
    {
        bool useColor = true;
        bool startMonday = true;
    };

    constexpr size_t CalBlockWidth = 20; ///< 7 columns * 2 chars + 6 gutters = 20.

    /// Centers @p text within @p width, padding with spaces.
    std::string centerText(std::string_view text, size_t width)
    {
        if (text.size() >= width)
            return std::string(text);
        auto const pad = width - text.size();
        auto const left = pad / 2;
        auto const right = pad - left;
        return std::string(left, ' ') + std::string(text) + std::string(right, ' ');
    }

    /// Renders one month as a list of 8 equally-wide lines (title, weekday row,
    /// and up to 6 week rows). Later callers compose these blocks horizontally.
    std::vector<std::string> renderCalMonthBlock(std::chrono::year_month ym,
                                                 std::chrono::year_month_day today,
                                                 CalStyle const& style)
    {
        auto const& sgr = style.useColor ? CalStyled : CalPlain;
        auto const firstDay = style.startMonday ? std::chrono::Monday : std::chrono::Sunday;

        std::vector<std::string> lines;
        lines.reserve(8);

        // Title: "April 2026"
        auto const monthIdx = static_cast<unsigned>(ym.month()) - 1u;
        auto const titleText = std::format("{} {}", CalMonthNames.at(monthIdx), static_cast<int>(ym.year()));
        lines.push_back(std::format("{}{}{}", sgr.title, centerText(titleText, CalBlockWidth), sgr.reset));

        // Weekday header row (e.g., "Mo Tu We Th Fr Sa Su").
        std::string header;
        header.reserve(CalBlockWidth + 16);
        header += sgr.weekdayRow;
        for (auto const col: std::views::iota(0uz, 7uz))
        {
            if (col > 0)
                header += ' ';
            auto const wd = firstDay + std::chrono::days { static_cast<int>(col) };
            auto const labelIdx = (static_cast<int>(wd.c_encoding()) + 6) % 7; // Mon=0..Sun=6
            auto const isWeekend = wd == std::chrono::Saturday || wd == std::chrono::Sunday;
            if (isWeekend && style.useColor)
            {
                header += sgr.reset;
                header += sgr.weekend;
                header += CalWeekdayShort.at(static_cast<size_t>(labelIdx));
                header += sgr.reset;
                header += sgr.weekdayRow;
            }
            else
            {
                header += CalWeekdayShort.at(static_cast<size_t>(labelIdx));
            }
        }
        header += sgr.reset;
        lines.push_back(std::move(header));

        // Compute the starting column of day-1 and fill six week rows.
        auto const first = std::chrono::year_month_day { ym.year(), ym.month(), std::chrono::day { 1 } };
        auto const firstWd = std::chrono::weekday { std::chrono::sys_days { first } };
        auto const startCol = weekdayColumn(firstWd, firstDay);
        auto const totalDays = daysInMonth(ym);

        // Each empty cell contributes exactly 2 chars and each separator contributes
        // 1 char, so an all-empty row already has the correct calBlockWidth width.
        unsigned dayNum = 1;
        for (auto const week: std::views::iota(0, 6))
        {
            std::string row;
            row.reserve(CalBlockWidth + 64);
            for (auto const col: std::views::iota(0uz, 7uz))
            {
                if (col > 0)
                    row += ' ';
                auto const cellIndex = static_cast<size_t>(week * 7) + col;
                if (cellIndex < startCol || dayNum > totalDays)
                {
                    row += "  ";
                    continue;
                }

                auto const wd = firstDay + std::chrono::days { static_cast<int>(col) };
                auto const isWeekend = wd == std::chrono::Saturday || wd == std::chrono::Sunday;
                auto const cellDate =
                    std::chrono::year_month_day { ym.year(), ym.month(), std::chrono::day { dayNum } };
                auto const isToday = cellDate == today;

                auto const cell = std::format("{:2}", dayNum);
                if (style.useColor && isToday)
                {
                    row += sgr.today;
                    row += cell;
                    row += sgr.reset;
                }
                else if (style.useColor && isWeekend)
                {
                    row += sgr.weekend;
                    row += cell;
                    row += sgr.reset;
                }
                else
                {
                    row += cell;
                }

                ++dayNum;
            }
            lines.push_back(std::move(row));
            if (dayNum > totalDays)
                break;
        }
        // Ensure we always have 8 lines so horizontal composition aligns cleanly.
        while (lines.size() < 8)
            lines.emplace_back(CalBlockWidth, ' ');
        return lines;
    }

    /// Joins a row of month blocks side-by-side with a two-space gutter.
    std::string joinCalBlocksHorizontally(std::span<std::vector<std::string> const> blocks)
    {
        if (blocks.empty())
            return {};
        constexpr std::string_view Gutter = "  ";
        std::string out;
        size_t const rows = blocks.front().size();
        for (auto const row: std::views::iota(0uz, rows))
        {
            for (auto const bi: std::views::iota(0uz, blocks.size()))
            {
                if (bi > 0)
                    out += Gutter;
                out += blocks[bi].at(row);
            }
            out += '\n';
        }
        return out;
    }

    /// Parses a positive decimal integer. Returns std::nullopt on parse failure.
    std::optional<int> parseCalInt(std::string_view s)
    {
        int value = 0;
        auto const [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), value);
        if (ec != std::errc {} || ptr != s.data() + s.size())
            return std::nullopt;
        return value;
    }

} // namespace

int Shell::executeInlineCal(CoreVM::CoreStringArray const& args, NativeHandle outputFd)
{
    bool threeMonth = false;
    bool yearMode = false;
    std::optional<bool> startMondayOverride;
    bool forceNoColor = false;
    std::vector<std::string> positional;

    for (size_t i = 1; i < args.size(); ++i)
    {
        std::string_view arg = args.at(i);
        if (arg == "-h" || arg == "--help")
            return renderMarkdownHelp(
                outputFd,
                "# cal\n"
                "\n"
                "Display a colorful calendar for a month or year.\n"
                "\n"
                "## Usage\n"
                "\n"
                "`cal [OPTIONS] [[MONTH] YEAR]`\n"
                "\n"
                "With no arguments, shows the current month. Pass `MONTH YEAR` for a\n"
                "specific month (MONTH is 1-12) or just `YEAR` for the whole year.\n"
                "\n"
                "## Options\n"
                "\n"
                "| Option | Description |\n"
                "|--------|-------------|\n"
                "| `-3`, `--three` | Show previous, current, and next month side-by-side |\n"
                "| `-y`, `--year` | Show the entire year |\n"
                "| `-m`, `--monday` | Start the week on Monday (ISO 8601, default) |\n"
                "| `-s`, `--sunday` | Start the week on Sunday |\n"
                "| `-n`, `--no-color` | Disable colorized output even on a terminal |\n"
                "| `-h`, `--help` | Display this help |\n"
                "\n"
                "## Colors\n"
                "\n"
                "Today's date is highlighted; weekend columns are tinted. Color output\n"
                "is suppressed automatically when stdout is not a terminal or when the\n"
                "`NO_COLOR` environment variable is set.\n");
        if (arg == "-3" || arg == "--three")
        {
            threeMonth = true;
            continue;
        }
        if (arg == "-y" || arg == "--year")
        {
            yearMode = true;
            continue;
        }
        if (arg == "-m" || arg == "--monday")
        {
            startMondayOverride = true;
            continue;
        }
        if (arg == "-s" || arg == "--sunday")
        {
            startMondayOverride = false;
            continue;
        }
        if (arg == "-n" || arg == "--no-color")
        {
            forceNoColor = true;
            continue;
        }
        if (arg.starts_with('-') && arg != "-")
        {
            error("cal: unrecognized option '{}'", arg);
            return 1;
        }
        positional.emplace_back(arg);
    }

    // Determine today in local time.
    auto const nowTp = std::chrono::system_clock::now();
    auto const nowSec = std::chrono::system_clock::to_time_t(nowTp);
    std::tm tmBuf {};
#if defined(_WIN32)
    localtime_s(&tmBuf, &nowSec);
#else
    localtime_r(&nowSec, &tmBuf);
#endif
    auto const today = std::chrono::year_month_day {
        std::chrono::year { tmBuf.tm_year + 1900 },
        std::chrono::month { static_cast<unsigned>(tmBuf.tm_mon + 1) },
        std::chrono::day { static_cast<unsigned>(tmBuf.tm_mday) },
    };

    // Resolve target month/year from positional args.
    auto targetYear = today.year();
    auto targetMonth = today.month();
    bool explicitYearOnly = false;

    if (positional.size() == 1)
    {
        auto const n = parseCalInt(positional[0]);
        if (!n || *n < 1 || *n > 9999)
        {
            error("cal: invalid year '{}'", positional[0]);
            return 1;
        }
        targetYear = std::chrono::year { *n };
        explicitYearOnly = true;
    }
    else if (positional.size() == 2)
    {
        auto const m = parseCalInt(positional[0]);
        auto const y = parseCalInt(positional[1]);
        if (!m || *m < 1 || *m > 12)
        {
            error("cal: invalid month '{}' (expected 1-12)", positional[0]);
            return 1;
        }
        if (!y || *y < 1 || *y > 9999)
        {
            error("cal: invalid year '{}'", positional[1]);
            return 1;
        }
        targetMonth = std::chrono::month { static_cast<unsigned>(*m) };
        targetYear = std::chrono::year { *y };
    }
    else if (positional.size() > 2)
    {
        error("cal: too many arguments");
        return 1;
    }

    // Resolve color capability.
    auto const* noColorEnv = std::getenv("NO_COLOR");
    bool const useColor =
        !forceNoColor && isTerminal(outputFd) && (noColorEnv == nullptr || noColorEnv[0] == '\0');

    CalStyle const style { .useColor = useColor, .startMonday = startMondayOverride.value_or(true) };

    // Year mode: 12 months in a 3x4 grid. Activated by -y or by a single year arg.
    bool const showYear = yearMode || explicitYearOnly;
    std::string output;
    output.reserve(showYear ? 16 * 1024 : 512);

    if (showYear)
    {
        auto const& headerSgr = useColor ? CalStyled.title : CalPlain.title;
        auto const& resetSgr = useColor ? CalStyled.reset : CalPlain.reset;
        auto const yearTitle = std::format("{:^{}}", static_cast<int>(targetYear), (CalBlockWidth * 3) + 4);
        output += std::format("{}{}{}\n\n", headerSgr, yearTitle, resetSgr);

        for (auto const bandIdx: std::views::iota(0, 4))
        {
            std::array<std::vector<std::string>, 3> band {};
            for (auto const col: std::views::iota(0, 3))
            {
                auto const monthNum = (bandIdx * 3) + col + 1;
                band.at(static_cast<size_t>(col)) = renderCalMonthBlock(
                    std::chrono::year_month { targetYear,
                                              std::chrono::month { static_cast<unsigned>(monthNum) } },
                    today,
                    style);
            }
            output += joinCalBlocksHorizontally(std::span<std::vector<std::string> const> { band });
            output += '\n';
        }
    }
    else if (threeMonth)
    {
        auto const curr = std::chrono::year_month { targetYear, targetMonth };
        auto const prev = curr - std::chrono::months { 1 };
        auto const next = curr + std::chrono::months { 1 };
        std::vector<std::vector<std::string>> blocks {
            renderCalMonthBlock(prev, today, style),
            renderCalMonthBlock(curr, today, style),
            renderCalMonthBlock(next, today, style),
        };
        output += joinCalBlocksHorizontally(std::span { blocks });
    }
    else
    {
        auto const block =
            renderCalMonthBlock(std::chrono::year_month { targetYear, targetMonth }, today, style);
        for (auto const& line: block)
        {
            output += line;
            output += '\n';
        }
    }

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

    auto parent = platform::normalizePath(std::filesystem::path(args.at(1)).parent_path());
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
        auto output = std::format("{}\n", platform::normalizePath(canonical));
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
    namespace fs = std::filesystem;

    bool symbolic = false;
    bool force = false;
    bool noDereference = false;
    bool verbose = false;
    std::optional<std::string> targetDir; // set by -t / --target-directory
    bool noMoreOptions = false;
    std::vector<std::string> operands;

    for (size_t i = 1; i < args.size(); ++i)
    {
        std::string_view arg = args.at(i);
        if (!noMoreOptions)
        {
            if (arg == "--help")
                return renderMarkdownHelp(
                    outputFd,
                    "# ln\n"
                    "\n"
                    "Create hard or symbolic links.\n"
                    "\n"
                    "## Usage\n"
                    "\n"
                    "`ln [OPTIONS] TARGET`            create a link to TARGET in the current directory\n"
                    "`ln [OPTIONS] TARGET LINK_NAME`  create LINK_NAME as a link to TARGET\n"
                    "`ln [OPTIONS] TARGET... DIR`     create links to each TARGET inside DIR\n"
                    "\n"
                    "If LINK_NAME is an existing directory, the link is created inside it,\n"
                    "named after TARGET's basename.\n"
                    "\n"
                    "## Options\n"
                    "\n"
                    "| Option | Description |\n"
                    "|--------|-------------|\n"
                    "| `-s` | Create symbolic link |\n"
                    "| `-f` | Remove existing destination (including a dangling symlink) |\n"
                    "| `-n`, `--no-dereference` | Treat an existing symlink at the destination as a "
                    "normal file (replace it rather than follow into it) |\n"
                    "| `-t`, `--target-directory=DIR` | Create all links inside DIR |\n"
                    "| `-v` | Explain what is being done |\n"
                    "| `--help` | Display this help |\n");
            if (arg == "--")
            {
                noMoreOptions = true;
                continue;
            }
            if (arg == "--no-dereference")
            {
                noDereference = true;
                continue;
            }
            if (arg.starts_with("--target-directory="))
            {
                targetDir = std::string(arg.substr(std::string_view("--target-directory=").size()));
                continue;
            }
            if (arg.starts_with("-") && arg.size() > 1 && arg[1] != '-')
            {
                for (auto const j: std::views::iota(1uz, arg.size()))
                {
                    switch (arg[j])
                    {
                        case 's': symbolic = true; break;
                        case 'f': force = true; break;
                        case 'n': noDereference = true; break;
                        case 'v': verbose = true; break;
                        case 't':
                            // -t takes the next argument as the target directory.
                            if (i + 1 >= args.size())
                            {
                                error("ln: option requires an argument -- 't'");
                                return 1;
                            }
                            targetDir = args.at(++i);
                            break;
                        default: error("ln: invalid option -- '{}'", arg[j]); return 1;
                    }
                }
                continue;
            }
        }
        operands.emplace_back(arg);
    }

    if (operands.empty())
    {
        error("ln: missing file operand");
        return 1;
    }

    /// Decides whether @p dest should be treated as a directory to place the link inside.
    /// With -n, an existing symlink at @p dest is treated as a plain destination so it can
    /// be replaced rather than dereferenced into.
    auto const isDirectoryForLinking = [&](std::string const& dest) {
        std::error_code ec;
        if (noDereference && fs::is_symlink(fs::symlink_status(dest, ec)))
            return false;
        return fs::is_directory(dest, ec);
    };

    /// Creates a single link from @p target to @p linkName, honoring -s/-f/-n/-v.
    /// @return 0 on success, 1 on failure (after reporting the error).
    auto const createOneLink = [&](std::string const& target, std::string const& linkName) -> int {
        std::error_code ec;

        // lstat-style existence check: symlink_status does not follow the final component,
        // so a dangling symlink at linkName is detected (and removable) — unlike exists().
        auto const st = fs::symlink_status(linkName, ec);
        bool const present = !ec && st.type() != fs::file_type::not_found;
        if (present && (force || (noDereference && fs::is_symlink(st))))
        {
            std::error_code removeEc;
            fs::remove(linkName, removeEc); // remove the link/file itself, never recurse
        }

        ec.clear();
        if (symbolic)
            fs::create_symlink(target, linkName, ec);
        else
            fs::create_hard_link(target, linkName, ec);

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
    };

    auto const linkInDir = [](std::string const& target, std::string const& dir) {
        return (fs::path(dir) / fs::path(target).filename()).string();
    };

    /// Creates a link to each target inside @p dir (which must already exist as a directory),
    /// each named after its target's basename. Used by both -t DIR and the TARGET... DIRECTORY
    /// form. @return the worst (non-zero on any failure) exit code.
    auto const linkAllIntoDir = [&](std::string const& dir, std::span<std::string const> targets) -> int {
        std::error_code ec;
        if (!fs::is_directory(dir, ec))
        {
            error("ln: target directory '{}' is not a directory", dir);
            return 1;
        }
        int rc = 0;
        for (auto const& target: targets)
            rc |= createOneLink(target, linkInDir(target, dir));
        return rc;
    };

    if (targetDir)
        return linkAllIntoDir(*targetDir, operands);

    if (operands.size() == 1)
    {
        // Single operand: link in the current directory, named after the target's basename.
        auto const& target = operands[0];
        return createOneLink(target, fs::path(target).filename().string());
    }

    if (operands.size() == 2)
    {
        auto const& target = operands[0];
        auto const& dest = operands[1];
        auto const linkName = isDirectoryForLinking(dest) ? linkInDir(target, dest) : dest;
        return createOneLink(target, linkName);
    }

    // More than two operands: the last must be an existing directory.
    return linkAllIntoDir(operands.back(), std::span(operands).first(operands.size() - 1));
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
    static constexpr std::string_view Chars =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    std::string suffix = "tmp.";
    std::mt19937 rng(static_cast<unsigned>(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::uniform_int_distribution<size_t> dist(0, Chars.size() - 1);
    for (auto const _: std::views::iota(0, 10))
    {
        (void) _;
        suffix += Chars[dist(rng)];
    }

    auto const path = tmpdir / suffix;

    std::error_code ec;
    if (createDir)
    {
        std::filesystem::create_directories(path, ec);
        if (ec)
        {
            error("mktemp: failed to create directory '{}': {}", platform::normalizePath(path), ec.message());
            return 1;
        }
    }
    else
    {
        std::ofstream ofs(path);
        if (!ofs)
        {
            error("mktemp: failed to create file '{}'", platform::normalizePath(path));
            return 1;
        }
    }

    auto output = std::format("{}\n", platform::normalizePath(path));
    [[maybe_unused]] auto written = platformWrite(outputFd, output.data(), output.size());
    return 0;
}

// ---------------------------------------------------------------------------
// Helper: read all lines from stdin or files
// ---------------------------------------------------------------------------

namespace
{

    struct ReadLinesResult
    {
        std::vector<std::string> lines;
        bool hadError = false;
        int exitCode = 0;
    };

    ReadLinesResult readLinesFromInput(endo::NativeHandle stdinFd,
                                       std::span<std::string const> files,
                                       auto const& errorFn)
    {
        using namespace endo::platform;

        ReadLinesResult result;

        auto const readFromStream = [&](std::istream& stream) {
            std::string line;
            while (std::getline(stream, line))
            {
                SignalHandler::processSignalFd();
                if (SignalHandler::hasPendingSigint())
                {
                    SignalHandler::clearPendingSigint();
                    result.exitCode = 130;
                    return;
                }
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                result.lines.push_back(std::move(line));
            }
        };

        auto const readFromStdin = [&]() {
            auto [stdinData, exitCode] = interruptibleReadAll(stdinFd);
            if (exitCode != 0)
            {
                result.exitCode = exitCode;
                return;
            }
            std::istringstream iss(stdinData);
            readFromStream(iss);
        };

        if (files.empty())
        {
            readFromStdin();
        }
        else
        {
            for (auto const& file: files)
            {
                if (result.exitCode != 0)
                    break;
                if (file == "-")
                {
                    readFromStdin();
                }
                else
                {
                    std::ifstream ifs(platform::resolveDevicePath(file));
                    if (!ifs)
                    {
                        errorFn(std::format("{}: No such file or directory", file));
                        result.hadError = true;
                        continue;
                    }
                    readFromStream(ifs);
                }
            }
        }
        return result;
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
    auto const result = readLinesFromInput(stdinFd, files, errorFn);
    if (result.exitCode != 0)
        return result.exitCode;
    if (result.hadError)
        return 1;
    auto const& lines = result.lines;

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
        std::ifstream ifs(platform::resolveDevicePath(filePath));
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
        auto const result = readLinesFromInput(stdinFd, files, errorFn);
        if (result.exitCode != 0)
            return result.exitCode;
        auto const& lines = result.lines;
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
    auto const result = readLinesFromInput(stdinFd, files, errorFn);
    if (result.exitCode != 0)
        return result.exitCode;
    if (result.hadError)
        return 1;
    auto const& lines = result.lines;

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
                                      "| `search [--cwd] [--no-validate] PATTERN` | Search entries; `--cwd` "
                                      "restricts to the current directory or its ancestors, `--no-validate` "
                                      "skips the required-paths existence filter |\n"
                                      "| `clear` | Clear all history |\n"
                                      "| `-h`, `--help` | Display this help |\n");

        if (subcmd == "clear")
        {
            history.clear();
            return 0;
        }

        if (subcmd == "search")
        {
            // Optional flags may appear before the pattern:
            //   --cwd           restrict to entries whose stored cwd == or is an
            //                   ancestor of the current working directory
            //   --no-validate   skip the required-paths existence filter
            auto cwdOnly = false;
            auto validate = true;
            auto patternIdx = size_t { 2 };
            while (patternIdx < args.size())
            {
                auto const& flag = args.at(patternIdx);
                if (flag == "--cwd")
                {
                    cwdOnly = true;
                    ++patternIdx;
                    continue;
                }
                if (flag == "--no-validate")
                {
                    validate = false;
                    ++patternIdx;
                    continue;
                }
                break;
            }

            if (patternIdx >= args.size())
            {
                error("history: search requires a pattern");
                return 1;
            }

            auto const& pattern = args.at(patternIdx);
            auto options = FuzzySearchOptions {
                .currentCwd = _env.currentDirectory(),
                .home = normalizedHomeDirectory(_env),
                .validateRequiredPaths = validate,
                .fs = &_fs,
            };

            // When --cwd is set, filter out entries that aren't exact- or ancestor-CWD matches
            // by computing canonical forms and rejecting others after the fuzzy search.
            auto const currentCwdCanonical = canonicalizeForHistory(options.currentCwd, options.home);
            auto const results = history.searchFuzzy(pattern, 50, options);

            auto const& theme = tui::currentTheme();
            std::string buf;
            for (auto const& match: results)
            {
                if (cwdOnly)
                {
                    // Re-check CWD against the original entry to distinguish a boosted-but-not-matching
                    // result from an actual match. `match.entry` is a view into the history entry's
                    // command string, so we need to locate the full entry to inspect its `cwd`.
                    auto const& rich = history.richEntries();
                    auto it =
                        std::ranges::find_if(rich, [&](auto const& e) { return e.command == match.entry; });
                    if (it == rich.end())
                        continue;
                    auto const exact = !it->cwd.empty() && it->cwd == currentCwdCanonical;
                    auto const ancestor = !it->cwd.empty() && it->cwd != currentCwdCanonical
                                          && currentCwdCanonical.starts_with(it->cwd)
                                          && currentCwdCanonical.size() > it->cwd.size()
                                          && currentCwdCanonical[it->cwd.size()] == '/';
                    if (!exact && !ancestor)
                        continue;
                }

                auto const entry = std::string { match.entry };
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
    // args[0] = "source"/"."; args[1] = filename; args[2..] = script arguments
    auto scriptArgs = std::vector<std::string>(args.begin() + 2, args.end());
    return executeEndoScript(std::filesystem::path(filePath), scriptArgs);
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
    auto const result = readLinesFromInput(stdinFd, files, errorFn);
    if (result.exitCode != 0)
        return result.exitCode;
    if (result.hadError)
        return 1;
    auto const& lines = result.lines;

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
            {
                inWord = false;
            }
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
    auto result = readLinesFromInput(stdinFd, files, errorFn);
    if (result.exitCode != 0)
        return result.exitCode;
    if (result.hadError)
        return 1;
    auto& lines = result.lines;

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
    auto const result = readLinesFromInput(stdinFd, files, errorFn);
    if (result.exitCode != 0)
        return result.exitCode;
    if (result.hadError)
        return 1;
    auto const& lines = result.lines;

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
    auto const result = readLinesFromInput(stdinFd, files, errorFn);
    if (result.exitCode != 0)
        return result.exitCode;
    if (result.hadError)
        return 1;
    auto const& lines = result.lines;

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
    auto [inputData, exitCode] = interruptibleReadAll(stdinFd);
    if (exitCode != 0)
        return exitCode;

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
        outStreams.emplace_back(platform::resolveDevicePath(file), mode);
        if (!outStreams.back())
        {
            error("tee: {}: Permission denied", file);
            return 1;
        }
    }

    // Read from stdin, write to stdout + files
    auto const exitCode = interruptibleReadLoop(stdinFd, [&](char const* buf, size_t len) {
        [[maybe_unused]] auto written = platformWrite(outputFd, buf, len);
        for (auto& ofs: outStreams)
            ofs.write(buf, static_cast<std::streamsize>(len));
    });
    if (exitCode != 0)
        return exitCode;

    return 0;
}

} // namespace endo
