// SPDX-License-Identifier: Apache-2.0
#include <shell/commands/GrepCommand.hpp>
#include <shell/util/GlobMatcher.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <filesystem>
#include <format>
#include <fstream>
#include <ranges>
#include <regex>
#include <stop_token>
#include <utility>

#include <platform/SignalHandler.hpp>

namespace endo::grep
{

namespace
{

    constexpr auto BinaryCheckSize = size_t { 8192 };

    // ANSI color codes matching GNU grep defaults
    constexpr auto ColorFilename = std::string_view { "\033[35m" };   // magenta
    constexpr auto ColorLineNumber = std::string_view { "\033[32m" }; // green
    constexpr auto ColorMatch = std::string_view { "\033[01;31m" };   // bold red
    constexpr auto ColorSeparator = std::string_view { "\033[36m" };  // cyan
    constexpr auto ColorReset = std::string_view { "\033[m" };

    /// Escapes regex metacharacters for fixed-string matching.
    std::string escapeRegex(std::string_view pattern)
    {
        std::string result;
        result.reserve(pattern.size() * 2);
        for (auto const ch: pattern)
        {
            switch (ch)
            {
                case '.':
                case '^':
                case '$':
                case '|':
                case '(':
                case ')':
                case '[':
                case ']':
                case '{':
                case '}':
                case '*':
                case '+':
                case '?':
                case '\\': result += '\\'; [[fallthrough]];
                default: result += ch;
            }
        }
        return result;
    }

    /// Parses a numeric argument from a string.
    std::expected<int, std::string> parseNumericArg(std::string_view value, std::string_view optionName)
    {
        int result = 0;
        auto const [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), result);
        if (ec != std::errc {} || ptr != value.data() + value.size())
            return std::unexpected(std::format("{}: invalid argument '{}'", optionName, value));
        if (result < 0)
            return std::unexpected(std::format("{}: invalid argument '{}'", optionName, value));
        return result;
    }

    /// Extracts the value part from a --option=value argument.
    std::string_view extractLongOptValue(std::string_view arg)
    {
        auto const pos = arg.find('=');
        if (pos == std::string_view::npos)
            return {};
        return arg.substr(pos + 1);
    }

    /// Checks if a directory entry matches include/exclude glob filters.
    bool matchesFilters(std::filesystem::path const& filePath, GrepOptions const& opts)
    {
        auto const filename = filePath.filename().string();

        // Check include patterns (if any specified, file must match at least one)
        if (!opts.includeGlobs.empty())
        {
            auto const matches = std::ranges::any_of(
                opts.includeGlobs, [&](auto const& glob) { return globMatchFilename(filename, glob); });
            if (!matches)
                return false;
        }

        // Check exclude patterns
        if (std::ranges::any_of(opts.excludeGlobs,
                                [&](auto const& glob) { return globMatchFilename(filename, glob); }))
            return false;

        return true;
    }

    /// Checks if a directory should be excluded based on --exclude-dir patterns.
    bool isExcludedDir(std::filesystem::path const& dirPath, GrepOptions const& opts)
    {
        auto const dirname = dirPath.filename().string();
        return std::ranges::any_of(opts.excludeDirs,
                                   [&](auto const& pattern) { return globMatchFilename(dirname, pattern); });
    }

    /// Number of loop iterations between interrupt polls. Draining the OS signal
    /// has a small cost, so cheap loops (directory scans, line matching) only
    /// poll every so often.
    constexpr auto InterruptPollInterval = std::size_t { 256 };

    /// Throttled, non-clearing check for whether a long grep loop should abort.
    ///
    /// Drains pending OS signals (required on Linux, where SIGINT arrives via
    /// signalfd) and reports whether a Ctrl+C is pending or @p stopToken has
    /// requested a stop. The pending-SIGINT flag is deliberately NOT cleared
    /// here: the caller leaves it set so the grep driver can observe it after the
    /// pure helper returns, consume it once, and exit with code 130.
    ///
    /// @param counter Per-loop iteration counter, advanced on every call.
    /// @param stopToken External cancellation token (may be empty).
    /// @return true if the loop should stop.
    [[nodiscard]] bool shouldAbort(std::size_t& counter, std::stop_token const& stopToken)
    {
        if (++counter % InterruptPollInterval != 0)
            return false;
        platform::SignalHandler::processSignalFd();
        return platform::SignalHandler::hasPendingSigint() || stopToken.stop_requested();
    }

} // namespace

std::expected<GrepOptions, std::string> parseGrepArgs(std::span<std::string const> args)
{
    GrepOptions opts;
    bool doubleDash = false;
    bool patternFromPositional = false;

    for (size_t i = 0; i < args.size(); ++i)
    {
        auto const& arg = args[i];

        // After --, everything is a pattern (first) or file
        if (doubleDash)
        {
            if (!patternFromPositional && opts.patterns.empty())
            {
                opts.patterns.push_back(arg);
                patternFromPositional = true;
            }
            else
            {
                opts.files.push_back(arg);
            }
            continue;
        }

        if (arg == "--")
        {
            doubleDash = true;
            continue;
        }

        if (arg == "--help")
        {
            opts.showHelp = true;
            return opts;
        }

        // Long options with = syntax
        if (arg.starts_with("--color=") || arg.starts_with("--colour="))
        {
            auto const value = extractLongOptValue(arg);
            if (value == "always")
                opts.colorMode = ColorMode::Always;
            else if (value == "never")
                opts.colorMode = ColorMode::Never;
            else if (value == "auto")
                opts.colorMode = ColorMode::Auto;
            else
                return std::unexpected(std::format("grep: invalid argument '{}' for '--color'", value));
            continue;
        }

        if (arg == "--color" || arg == "--colour")
        {
            opts.colorMode = ColorMode::Always;
            continue;
        }

        if (arg.starts_with("--include="))
        {
            opts.includeGlobs.emplace_back(extractLongOptValue(arg));
            continue;
        }

        if (arg.starts_with("--exclude="))
        {
            opts.excludeGlobs.emplace_back(extractLongOptValue(arg));
            continue;
        }

        if (arg.starts_with("--exclude-dir="))
        {
            opts.excludeDirs.emplace_back(extractLongOptValue(arg));
            continue;
        }

        if (arg.starts_with("--regexp="))
        {
            opts.patterns.emplace_back(extractLongOptValue(arg));
            continue;
        }

        if (arg.starts_with("--max-count="))
        {
            auto const value = extractLongOptValue(arg);
            auto const parsed = parseNumericArg(value, "--max-count");
            if (!parsed.has_value())
                return std::unexpected(parsed.error());
            opts.maxCount = *parsed;
            continue;
        }

        if (arg == "--recursive")
        {
            opts.recursive = true;
            continue;
        }

        if (arg == "--quiet" || arg == "--silent")
        {
            opts.quiet = true;
            continue;
        }

        if (arg.starts_with("--after-context="))
        {
            auto const parsed = parseNumericArg(extractLongOptValue(arg), "--after-context");
            if (!parsed.has_value())
                return std::unexpected(parsed.error());
            opts.afterContext = *parsed;
            continue;
        }

        if (arg.starts_with("--before-context="))
        {
            auto const parsed = parseNumericArg(extractLongOptValue(arg), "--before-context");
            if (!parsed.has_value())
                return std::unexpected(parsed.error());
            opts.beforeContext = *parsed;
            continue;
        }

        if (arg.starts_with("--context="))
        {
            auto const parsed = parseNumericArg(extractLongOptValue(arg), "--context");
            if (!parsed.has_value())
                return std::unexpected(parsed.error());
            opts.bothContext = *parsed;
            continue;
        }

        // Short options (can be combined, e.g. -inr)
        if (arg.starts_with("-") && arg.size() > 1 && arg[1] != '-')
        {
            for (size_t j = 1; j < arg.size(); ++j)
            {
                switch (arg[j])
                {
                    case 'F': opts.fixedStrings = true; break;
                    case 'E': opts.extendedRegex = true; break;
                    case 'i': opts.ignoreCase = true; break;
                    case 'w': opts.wordRegexp = true; break;
                    case 'x': opts.lineRegexp = true; break;
                    case 'c': opts.countOnly = true; break;
                    case 'l': opts.filesWithMatches = true; break;
                    case 'L': opts.filesWithoutMatch = true; break;
                    case 'n': opts.lineNumbers = true; break;
                    case 'o': opts.onlyMatching = true; break;
                    case 'v': opts.invertMatch = true; break;
                    case 'q': opts.quiet = true; break;
                    case 's': opts.suppressErrors = true; break;
                    case 'r':
                    case 'R': opts.recursive = true; break;
                    case 'H': opts.filenameMode = FilenameMode::Always; break;
                    case 'h': opts.filenameMode = FilenameMode::Never; break;
                    case 'I': opts.skipBinary = true; break;
                    case 'e': {
                        // -e takes the next argument as the pattern
                        // If there are more characters in this combined flag, they are the pattern
                        if (j + 1 < arg.size())
                        {
                            opts.patterns.emplace_back(arg.substr(j + 1));
                            j = arg.size(); // consumed rest of this arg
                        }
                        else
                        {
                            if (i + 1 >= args.size())
                                return std::unexpected("grep: option requires an argument -- 'e'");
                            opts.patterns.push_back(args[++i]);
                        }
                        break;
                    }
                    case 'A': {
                        std::string_view numStr;
                        if (j + 1 < arg.size())
                        {
                            numStr = std::string_view(arg).substr(j + 1);
                            j = arg.size();
                        }
                        else
                        {
                            if (i + 1 >= args.size())
                                return std::unexpected("grep: option requires an argument -- 'A'");
                            numStr = args[++i];
                        }
                        auto const parsed = parseNumericArg(numStr, "-A");
                        if (!parsed.has_value())
                            return std::unexpected(parsed.error());
                        opts.afterContext = *parsed;
                        break;
                    }
                    case 'B': {
                        std::string_view numStr;
                        if (j + 1 < arg.size())
                        {
                            numStr = std::string_view(arg).substr(j + 1);
                            j = arg.size();
                        }
                        else
                        {
                            if (i + 1 >= args.size())
                                return std::unexpected("grep: option requires an argument -- 'B'");
                            numStr = args[++i];
                        }
                        auto const parsed = parseNumericArg(numStr, "-B");
                        if (!parsed.has_value())
                            return std::unexpected(parsed.error());
                        opts.beforeContext = *parsed;
                        break;
                    }
                    case 'C': {
                        std::string_view numStr;
                        if (j + 1 < arg.size())
                        {
                            numStr = std::string_view(arg).substr(j + 1);
                            j = arg.size();
                        }
                        else
                        {
                            if (i + 1 >= args.size())
                                return std::unexpected("grep: option requires an argument -- 'C'");
                            numStr = args[++i];
                        }
                        auto const parsed = parseNumericArg(numStr, "-C");
                        if (!parsed.has_value())
                            return std::unexpected(parsed.error());
                        opts.bothContext = *parsed;
                        break;
                    }
                    case 'm': {
                        std::string_view numStr;
                        if (j + 1 < arg.size())
                        {
                            numStr = std::string_view(arg).substr(j + 1);
                            j = arg.size();
                        }
                        else
                        {
                            if (i + 1 >= args.size())
                                return std::unexpected("grep: option requires an argument -- 'm'");
                            numStr = args[++i];
                        }
                        auto const parsed = parseNumericArg(numStr, "-m");
                        if (!parsed.has_value())
                            return std::unexpected(parsed.error());
                        opts.maxCount = *parsed;
                        break;
                    }
                    default: return std::unexpected(std::format("grep: invalid option -- '{}'", arg[j]));
                }
            }
            continue;
        }

        // Positional arguments: first is the pattern, rest are files
        if (!patternFromPositional && opts.patterns.empty())
        {
            opts.patterns.push_back(arg);
            patternFromPositional = true;
        }
        else
        {
            opts.files.push_back(arg);
        }
    }

    if (opts.showHelp)
        return opts;

    if (opts.patterns.empty())
        return std::unexpected(std::string("grep: no pattern specified"));

    // GNU grep: -r implies -H (always show filenames) unless -h was explicitly given
    if (opts.recursive && opts.filenameMode == FilenameMode::Auto)
        opts.filenameMode = FilenameMode::Always;

    return opts;
}

std::expected<std::regex, std::string> buildRegex(GrepOptions const& opts)
{
    // Build combined pattern from all -e patterns
    std::string combined;
    // macOS libc++ does not yet provide std::views::enumerate (C++23).
#if defined(__cpp_lib_ranges_enumerate) && __cpp_lib_ranges_enumerate >= 202302L
    for (auto const& [idx, pat]: std::views::enumerate(opts.patterns))
#else
    for (size_t idx = 0; auto const& pat: opts.patterns)
#endif
    {
        if (idx > 0)
            combined += '|';

        auto processed = opts.fixedStrings ? escapeRegex(pat) : pat;

        if (opts.wordRegexp)
        {
            processed.insert(0, "\\b");
            processed += "\\b";
        }

        if (opts.lineRegexp)
        {
            processed.insert(0, "^");
            processed += "$";
        }

        combined += processed;
#if !defined(__cpp_lib_ranges_enumerate) || __cpp_lib_ranges_enumerate < 202302L
        ++idx;
#endif
    }

    // Wrap multiple alternatives in a group to keep correct precedence
    if (opts.patterns.size() > 1 && !opts.wordRegexp && !opts.lineRegexp)
        combined = "(?:" + combined + ")";

    auto flags = std::regex::ECMAScript;
    if (opts.ignoreCase)
        flags |= std::regex::icase;

    try
    {
        return std::regex(combined, flags);
    }
    catch (std::regex_error const& e)
    {
        return std::unexpected(std::format("grep: invalid regular expression: {}", e.what()));
    }
    catch (...)
    {
        return std::unexpected(std::string("grep: invalid regular expression"));
    }
}

bool isBinaryFile(platform::FileSystem const& fs, std::filesystem::path const& path)
{
    // Open through the injected FileSystem so binary detection works against any
    // backend (e.g. InMemoryFileSystem in tests), consistent with collectFiles.
    auto stream = fs.openRead(path);
    if (!stream || !stream->good())
        return true;

    auto buffer = std::array<char, BinaryCheckSize> {};
    stream->read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    auto const bytesRead = static_cast<size_t>(stream->gcount());

    return std::any_of(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(bytesRead), [](char c) {
        return c == '\0';
    });
}

std::vector<std::filesystem::path> collectFiles(platform::FileSystem const& fs,
                                                GrepOptions const& opts,
                                                ErrorWriter const& errWriter,
                                                bool& hasError,
                                                std::stop_token const& stopToken)
{
    namespace stdfs = std::filesystem;
    std::vector<stdfs::path> result;
    std::size_t pollCounter = 0;

    // Expands one directory root recursively via the FileSystem's lazy coroutine
    // walk, applying grep's include/exclude filters. Returns false if the walk
    // was interrupted (Ctrl+C or external stop), so the caller can stop early.
    auto const recurseInto = [&](stdfs::path const& root) -> bool {
        for (auto const& entry: fs.walkDirectoryRecursive(root))
        {
            if (shouldAbort(pollCounter, stopToken))
                return false;
            if (!entry.isRegularFile)
                continue;

            // Check parent directories (relative to the root) against --exclude-dir.
            auto const rel = entry.path.lexically_relative(root);
            auto excluded = false;
            for (auto const& component: rel)
            {
                if (component == rel.filename())
                    break;
                if (isExcludedDir(component, opts))
                {
                    excluded = true;
                    break;
                }
            }
            if (excluded || !matchesFilters(entry.path, opts))
                continue;
            result.push_back(entry.path);
        }
        return true;
    };

    if (opts.files.empty())
    {
        // No file arguments: recurse the current directory (-r) or read stdin (caller).
        if (opts.recursive)
            recurseInto(".");
        return result;
    }

    for (auto const& fileArg: opts.files)
    {
        auto const path = stdfs::path(fileArg);

        if (!fs.exists(path))
        {
            if (!opts.suppressErrors)
                errWriter(std::format("grep: {}: No such file or directory\n", fileArg));
            hasError = true;
            continue;
        }

        if (fs.isDirectory(path))
        {
            if (opts.recursive)
            {
                if (!recurseInto(path))
                    return result; // interrupted
            }
            else
            {
                if (!opts.suppressErrors)
                    errWriter(std::format("grep: {}: Is a directory\n", fileArg));
                hasError = true;
            }
            continue;
        }

        if (fs.isRegularFile(path))
        {
            if (!matchesFilters(path, opts))
                continue;
            result.push_back(path);
        }
    }

    return result;
}

size_t searchLines(std::vector<std::string> const& lines,
                   std::regex const& regex,
                   GrepOptions const& opts,
                   std::string_view filename,
                   bool showFilename,
                   bool useColor,
                   OutputWriter const& writer,
                   std::stop_token const& stopToken)
{
    auto const beforeCtx = opts.effectiveBeforeContext();
    auto const afterCtx = opts.effectiveAfterContext();
    auto const hasContext = beforeCtx > 0 || afterCtx > 0;
    auto const lineCount = static_cast<int>(lines.size());
    std::size_t pollCounter = 0;

    // Find all matching line indices. Polling here keeps grep on a single huge
    // file interruptible — regex_search over millions of lines is CPU-bound and
    // would otherwise ignore Ctrl+C until the whole file is scanned.
    std::vector<int> matchIndices;
    for (auto const i: std::views::iota(0, lineCount))
    {
        if (shouldAbort(pollCounter, stopToken))
            return matchIndices.size();

        auto const matches = std::regex_search(lines[static_cast<size_t>(i)], regex);
        auto const isMatch = opts.invertMatch ? !matches : matches;
        if (isMatch)
        {
            matchIndices.push_back(i);
            if (opts.maxCount > 0 && std::cmp_greater_equal(matchIndices.size(), opts.maxCount))
                break;
        }
    }

    auto const matchCount = matchIndices.size();

    if (opts.quiet)
        return matchCount;

    // -c: count only
    if (opts.countOnly)
    {
        std::string output;
        if (showFilename)
        {
            if (useColor)
                output += std::string(ColorFilename) + std::string(filename) + std::string(ColorReset) + ":";
            else
                output += std::string(filename) + ":";
        }
        output += std::to_string(matchCount) + "\n";
        writer(output);
        return matchCount;
    }

    // -l: files with matches
    if (opts.filesWithMatches)
    {
        if (matchCount > 0)
            writer(std::string(filename) + "\n");
        return matchCount;
    }

    // -L: files without matches
    if (opts.filesWithoutMatch)
    {
        if (matchCount == 0)
            writer(std::string(filename) + "\n");
        return matchCount;
    }

    // Build set of lines to print (including context)
    // Each entry: (lineIndex, isMatch)
    struct OutputLine
    {
        int lineIndex;
        bool isMatch;
    };

    std::vector<OutputLine> outputLines;
    if (hasContext)
    {
        // Build ranges with context
        std::vector<bool> shouldPrint(static_cast<size_t>(lineCount), false);
        std::vector<bool> isMatchLine(static_cast<size_t>(lineCount), false);

        for (auto const matchIdx: matchIndices)
        {
            isMatchLine[static_cast<size_t>(matchIdx)] = true;
            auto const start = std::max(0, matchIdx - beforeCtx);
            auto const end = std::min(lineCount - 1, matchIdx + afterCtx);
            for (auto const j: std::views::iota(start, end + 1))
                shouldPrint[static_cast<size_t>(j)] = true;
        }

        for (auto const i: std::views::iota(0, lineCount))
        {
            if (shouldPrint[static_cast<size_t>(i)])
                outputLines.push_back({ .lineIndex = i, .isMatch = isMatchLine[static_cast<size_t>(i)] });
        }
    }
    else
    {
        for (auto const matchIdx: matchIndices)
            outputLines.push_back({ .lineIndex = matchIdx, .isMatch = true });
    }

    // Print output lines
    int lastPrintedLine = -2; // Track for group separators
    for (auto const& [lineIndex, isMatch]: outputLines)
    {
        if (shouldAbort(pollCounter, stopToken))
            return matchCount;

        // Print group separator between non-contiguous groups
        if (hasContext && lastPrintedLine >= 0 && lineIndex > lastPrintedLine + 1)
        {
            if (useColor)
                writer(std::string(ColorSeparator) + "--" + std::string(ColorReset) + "\n");
            else
                writer("--\n");
        }
        lastPrintedLine = lineIndex;

        auto const& line = lines[static_cast<size_t>(lineIndex)];

        // -o: only matching (only for match lines)
        if (opts.onlyMatching && isMatch)
        {
            auto searchStart = line.cbegin();
            while (searchStart != line.cend())
            {
                std::smatch match;
                if (!std::regex_search(searchStart, line.cend(), match, regex))
                    break;

                std::string output;
                if (showFilename)
                {
                    if (useColor)
                        output += std::string(ColorFilename) + std::string(filename) + std::string(ColorReset)
                                  + ":";
                    else
                        output += std::string(filename) + ":";
                }
                if (opts.lineNumbers)
                {
                    if (useColor)
                        output += std::string(ColorLineNumber) + std::to_string(lineIndex + 1)
                                  + std::string(ColorReset) + ":";
                    else
                        output += std::to_string(lineIndex + 1) + ":";
                }

                if (useColor)
                    output += std::string(ColorMatch) + match.str() + std::string(ColorReset);
                else
                    output += match.str();

                output += "\n";
                writer(output);
                searchStart = match.suffix().first;
            }
            continue;
        }

        // Build output line
        std::string output;
        if (showFilename)
        {
            if (useColor)
                output += std::string(ColorFilename) + std::string(filename) + std::string(ColorReset);
            else
                output += std::string(filename);

            // Use : for match lines, - for context lines
            if (hasContext && !isMatch)
            {
                if (useColor)
                    output += std::string(ColorSeparator) + "-" + std::string(ColorReset);
                else
                    output += "-";
            }
            else
            {
                if (useColor)
                    output += std::string(ColorSeparator) + ":" + std::string(ColorReset);
                else
                    output += ":";
            }
        }

        if (opts.lineNumbers)
        {
            if (useColor)
                output +=
                    std::string(ColorLineNumber) + std::to_string(lineIndex + 1) + std::string(ColorReset);
            else
                output += std::to_string(lineIndex + 1);

            if (hasContext && !isMatch)
            {
                if (useColor)
                    output += std::string(ColorSeparator) + "-" + std::string(ColorReset);
                else
                    output += "-";
            }
            else
            {
                if (useColor)
                    output += std::string(ColorSeparator) + ":" + std::string(ColorReset);
                else
                    output += ":";
            }
        }

        // Colorize matches in the line
        if (useColor && isMatch && !opts.invertMatch)
        {
            std::string colorized;
            auto searchStart = line.cbegin();
            while (searchStart != line.cend())
            {
                std::smatch match;
                if (!std::regex_search(searchStart, line.cend(), match, regex))
                {
                    colorized.append(searchStart, line.cend());
                    break;
                }
                colorized.append(searchStart, match[0].first);
                colorized.append(ColorMatch);
                colorized += match.str();
                colorized.append(ColorReset);
                searchStart = match.suffix().first;
                if (match.empty())
                {
                    if (searchStart != line.cend())
                        colorized += *searchStart++;
                    else
                        break;
                }
            }
            output += colorized;
        }
        else
        {
            output += line;
        }

        output += "\n";
        writer(output);
    }

    return matchCount;
}

} // namespace endo::grep
