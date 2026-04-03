// SPDX-License-Identifier: Apache-2.0

#include <shell/util/GlobMatcher.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <format>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "TestExecutor.hpp"
#include "TestFileParser.hpp"
#include "TestReporter.hpp"

namespace fs = std::filesystem;

namespace
{

/// Discovers .endo test files in the given directory.
[[nodiscard]] std::vector<fs::path> discoverTestFiles(fs::path const& testDir)
{
    std::vector<fs::path> files;

    if (!fs::exists(testDir) || !fs::is_directory(testDir))
        return files;

    for (auto const& entry: fs::recursive_directory_iterator(testDir))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".endo")
            files.push_back(entry.path());
    }

    std::ranges::sort(files);
    return files;
}

/// Prints usage information.
void printUsage(char const* progName)
{
    std::cout << std::format("Usage: {} [options] [filter-patterns...]\n\n", progName);
    std::cout << "Options:\n";
    std::cout << "  -d, --dir <path>     Test directory (default: ENDO_TEST_DIR or ./tests)\n";
    std::cout << "  -f, --format <fmt>   Output format: pretty (default), tap\n";
    std::cout << "  -v, --verbose        Show output for passing tests\n";
    std::cout << "  --list               List test files without running\n";
    std::cout << "  -h, --help           Show this help\n";
    std::cout << R"(
Test File Format:
  .endo files with # directives at the top, followed by source code.

  Directives:
    # description: <text>        Human-readable test description (default: filename)
    # expect: <line>             Expected output line (repeatable, joined with \n)
    # expect-exit: <code>        Expected exit code (default: 0)
    # expect-error: <substring>  Expected compilation error (repeatable, empty = any error)
    # mode: <mode>               execute (default), ir-only, parse-only, structured
    # skip: <reason>             Skip this test
    # session-separator: <sep>   Split source into REPL prompts at "# <sep>" lines
    # mock-env: KEY=VALUE        Set mock environment variable
    # mock-which: PROG=/path     Set mock which path
    # expect-env: KEY=VALUE      Verify environment variable after execution
    # expect-nonempty            Assert output is non-empty
    # aux-file: <filename>       Start an auxiliary file section (for multi-file module tests)
    # main-file:                 End aux file section, rest is main test source
    # source-file: <path>        Load external file as session prompt (relative to project root)

  Example:
    # description: Addition of two integers
    # expect: 52

    let x = 42
    let y = 10
    println (x + y)

  Rules:
    - expect-error is mutually exclusive with expect/expect-exit
    - Session tests split source at "# <separator>" lines
    - Directives must appear before any non-comment, non-blank line
    - Expected output uses separator model: lines joined by \n, not terminated
    - To express a trailing newline, add an empty # expect: at the end
)";
}

struct CliOptions
{
    fs::path testDir;
    endo::test::OutputFormat format = endo::test::OutputFormat::Pretty;
    bool verbose = false;
    bool listOnly = false;
    std::vector<std::string> filters;
};

/// Parses command-line arguments.
[[nodiscard]] std::optional<CliOptions> parseArgs(int argc, char* argv[])
{
    CliOptions opts;

#ifdef ENDO_TEST_DIR
    opts.testDir = ENDO_TEST_DIR;
#else
    opts.testDir = "./tests";
#endif

    for (int i = 1; i < argc; ++i)
    {
        std::string_view arg = argv[i];

        if (arg == "-h" || arg == "--help")
        {
            printUsage(argv[0]);
            return std::nullopt;
        }
        else if (arg == "-d" || arg == "--dir")
        {
            if (++i >= argc)
            {
                std::cerr << "Error: --dir requires a path argument\n";
                return std::nullopt;
            }
            opts.testDir = argv[i];
        }
        else if (arg == "-f" || arg == "--format")
        {
            if (++i >= argc)
            {
                std::cerr << "Error: --format requires an argument\n";
                return std::nullopt;
            }
            std::string_view fmt = argv[i];
            if (fmt == "tap")
                opts.format = endo::test::OutputFormat::TAP;
            else if (fmt == "pretty")
                opts.format = endo::test::OutputFormat::Pretty;
            else
            {
                std::cerr << std::format("Error: unknown format \"{}\"\n", fmt);
                return std::nullopt;
            }
        }
        else if (arg == "-v" || arg == "--verbose")
        {
            opts.verbose = true;
        }
        else if (arg == "--list")
        {
            opts.listOnly = true;
        }
        else if (arg.starts_with('-'))
        {
            std::cerr << std::format("Error: unknown option \"{}\"\n", arg);
            return std::nullopt;
        }
        else
        {
            opts.filters.emplace_back(arg);
        }
    }

    return opts;
}

/// Checks if a relative path matches any of the given filter patterns.
[[nodiscard]] bool matchesFilters(std::string_view relativePath, std::vector<std::string> const& filters)
{
    if (filters.empty())
        return true;
    return std::ranges::any_of(
        filters, [relativePath](auto const& pattern) { return endo::globMatch(relativePath, pattern); });
}

} // namespace

int main(int argc, char* argv[])
{
    auto opts = parseArgs(argc, argv);
    if (!opts.has_value())
        return (argc > 1 && (std::string_view(argv[1]) == "-h" || std::string_view(argv[1]) == "--help")) ? 0
                                                                                                          : 1;

    auto const& testDir = opts->testDir;
    if (!fs::exists(testDir))
    {
        std::cerr << std::format("Error: test directory \"{}\" does not exist\n", testDir.string());
        return 1;
    }

    // Discover test files
    auto const testFiles = discoverTestFiles(testDir);
    if (testFiles.empty())
    {
        std::cerr << std::format("No .endo test files found in \"{}\"\n", testDir.string());
        return 1;
    }

    // Parse all test files and apply filters
    std::vector<endo::test::TestFile> parsedTests;
    for (auto const& filePath: testFiles)
    {
        auto const relativePath = fs::relative(filePath, testDir).string();
        if (!matchesFilters(relativePath, opts->filters))
            continue;

        auto testFile = endo::test::TestFileParser::parse(filePath, relativePath);
        if (testFile.has_value())
            parsedTests.push_back(std::move(*testFile));
        else
            std::cerr << std::format("Warning: failed to parse \"{}\"\n", filePath.string());
    }

    if (parsedTests.empty())
    {
        std::cerr << "No matching test files found\n";
        return 1;
    }

    // List mode
    if (opts->listOnly)
    {
        for (auto const& test: parsedTests)
            std::cout << test.relativePath << '\n';
        return 0;
    }

    // Run tests
    endo::test::TestReporter reporter(opts->format, opts->verbose);
    endo::test::TestExecutor executor;

    reporter.reportHeader(parsedTests.size());

    auto const runStart = std::chrono::steady_clock::now();
    std::vector<endo::test::TestResult> results;
    results.reserve(parsedTests.size());

    for (size_t i = 0; i < parsedTests.size(); ++i)
    {
        auto result = endo::test::TestExecutor::run(parsedTests[i]);
        reporter.reportResult(result, i + 1);
        results.push_back(std::move(result));
    }

    auto const runEnd = std::chrono::steady_clock::now();
    auto const totalDuration = std::chrono::duration_cast<std::chrono::microseconds>(runEnd - runStart);

    reporter.reportSummary(results, totalDuration);

    // Return non-zero if any tests failed
    auto const anyFailed = std::ranges::any_of(
        results, [](auto const& r) { return r.outcome == endo::test::TestOutcome::Fail; });

    return anyFailed ? 1 : 0;
}
