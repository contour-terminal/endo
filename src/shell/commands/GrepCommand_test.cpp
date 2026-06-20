// SPDX-License-Identifier: Apache-2.0
#include <shell/commands/GrepCommand.hpp>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <stop_token>
#include <vector>

#include <platform/SignalHandler.hpp>
#include <platform/testing/InMemoryFileSystem.hpp>

using namespace endo::grep;

// ============================================================================
// Argument parser tests
// ============================================================================

TEST_CASE("grep.parse.basic_pattern", "[grep]")
{
    std::vector<std::string> args = { "hello" };
    auto result = parseGrepArgs(args);
    REQUIRE(result.has_value());
    CHECK(result->patterns.size() == 1);
    CHECK(result->patterns[0] == "hello");
    CHECK(result->files.empty());
}

TEST_CASE("grep.parse.multiple_e_patterns", "[grep]")
{
    std::vector<std::string> args = { "-e", "pat1", "-e", "pat2" };
    auto result = parseGrepArgs(args);
    REQUIRE(result.has_value());
    CHECK(result->patterns.size() == 2);
    CHECK(result->patterns[0] == "pat1");
    CHECK(result->patterns[1] == "pat2");
}

TEST_CASE("grep.parse.combined_short_flags", "[grep]")
{
    std::vector<std::string> args = { "-inr", "pattern" };
    auto result = parseGrepArgs(args);
    REQUIRE(result.has_value());
    CHECK(result->ignoreCase);
    CHECK(result->lineNumbers);
    CHECK(result->recursive);
    CHECK(result->patterns[0] == "pattern");
}

TEST_CASE("grep.parse.fixed_strings", "[grep]")
{
    std::vector<std::string> args = { "-F", "a.b" };
    auto result = parseGrepArgs(args);
    REQUIRE(result.has_value());
    CHECK(result->fixedStrings);
}

TEST_CASE("grep.parse.context_flags", "[grep]")
{
    SECTION("-A")
    {
        std::vector<std::string> args = { "-A", "3", "pattern" };
        auto result = parseGrepArgs(args);
        REQUIRE(result.has_value());
        CHECK(result->afterContext == 3);
    }

    SECTION("-B")
    {
        std::vector<std::string> args = { "-B", "2", "pattern" };
        auto result = parseGrepArgs(args);
        REQUIRE(result.has_value());
        CHECK(result->beforeContext == 2);
    }

    SECTION("-C")
    {
        std::vector<std::string> args = { "-C", "5", "pattern" };
        auto result = parseGrepArgs(args);
        REQUIRE(result.has_value());
        CHECK(result->bothContext == 5);
        CHECK(result->effectiveAfterContext() == 5);
        CHECK(result->effectiveBeforeContext() == 5);
    }
}

TEST_CASE("grep.parse.max_count", "[grep]")
{
    SECTION("-m")
    {
        std::vector<std::string> args = { "-m", "10", "pattern" };
        auto result = parseGrepArgs(args);
        REQUIRE(result.has_value());
        CHECK(result->maxCount == 10);
    }

    SECTION("--max-count=")
    {
        std::vector<std::string> args = { "--max-count=5", "pattern" };
        auto result = parseGrepArgs(args);
        REQUIRE(result.has_value());
        CHECK(result->maxCount == 5);
    }
}

TEST_CASE("grep.parse.include_exclude", "[grep]")
{
    std::vector<std::string> args = { "--include=*.cpp", "--exclude=*.o", "--exclude-dir=build", "pattern" };
    auto result = parseGrepArgs(args);
    REQUIRE(result.has_value());
    CHECK(result->includeGlobs.size() == 1);
    CHECK(result->includeGlobs[0] == "*.cpp");
    CHECK(result->excludeGlobs.size() == 1);
    CHECK(result->excludeGlobs[0] == "*.o");
    CHECK(result->excludeDirs.size() == 1);
    CHECK(result->excludeDirs[0] == "build");
}

TEST_CASE("grep.parse.color_modes", "[grep]")
{
    SECTION("always")
    {
        std::vector<std::string> args = { "--color=always", "pattern" };
        auto result = parseGrepArgs(args);
        REQUIRE(result.has_value());
        CHECK(result->colorMode == ColorMode::Always);
    }

    SECTION("never")
    {
        std::vector<std::string> args = { "--color=never", "pattern" };
        auto result = parseGrepArgs(args);
        REQUIRE(result.has_value());
        CHECK(result->colorMode == ColorMode::Never);
    }

    SECTION("auto")
    {
        std::vector<std::string> args = { "--color=auto", "pattern" };
        auto result = parseGrepArgs(args);
        REQUIRE(result.has_value());
        CHECK(result->colorMode == ColorMode::Auto);
    }
}

TEST_CASE("grep.parse.double_dash", "[grep]")
{
    std::vector<std::string> args = { "--", "-pattern" };
    auto result = parseGrepArgs(args);
    REQUIRE(result.has_value());
    CHECK(result->patterns.size() == 1);
    CHECK(result->patterns[0] == "-pattern");
}

TEST_CASE("grep.parse.long_equals_syntax", "[grep]")
{
    std::vector<std::string> args = { "--regexp=PATTERN" };
    auto result = parseGrepArgs(args);
    REQUIRE(result.has_value());
    CHECK(result->patterns.size() == 1);
    CHECK(result->patterns[0] == "PATTERN");
}

TEST_CASE("grep.parse.error_missing_pattern", "[grep]")
{
    std::vector<std::string> args = {};
    auto result = parseGrepArgs(args);
    CHECK_FALSE(result.has_value());
    CHECK(result.error().find("no pattern") != std::string::npos);
}

TEST_CASE("grep.parse.error_invalid_num", "[grep]")
{
    std::vector<std::string> args = { "-A", "foo", "pattern" };
    auto result = parseGrepArgs(args);
    CHECK_FALSE(result.has_value());
}

TEST_CASE("grep.parse.error_missing_arg", "[grep]")
{
    std::vector<std::string> args = { "-e" };
    auto result = parseGrepArgs(args);
    CHECK_FALSE(result.has_value());
}

TEST_CASE("grep.parse.files_after_pattern", "[grep]")
{
    std::vector<std::string> args = { "pattern", "file1", "file2" };
    auto result = parseGrepArgs(args);
    REQUIRE(result.has_value());
    CHECK(result->patterns[0] == "pattern");
    CHECK(result->files.size() == 2);
    CHECK(result->files[0] == "file1");
    CHECK(result->files[1] == "file2");
}

TEST_CASE("grep.parse.word_and_line_regexp", "[grep]")
{
    SECTION("-w")
    {
        std::vector<std::string> args = { "-w", "foo" };
        auto result = parseGrepArgs(args);
        REQUIRE(result.has_value());
        CHECK(result->wordRegexp);
    }

    SECTION("-x")
    {
        std::vector<std::string> args = { "-x", "foo" };
        auto result = parseGrepArgs(args);
        REQUIRE(result.has_value());
        CHECK(result->lineRegexp);
    }
}

TEST_CASE("grep.parse.quiet_silent", "[grep]")
{
    SECTION("-q")
    {
        std::vector<std::string> args = { "-q", "pattern" };
        auto result = parseGrepArgs(args);
        REQUIRE(result.has_value());
        CHECK(result->quiet);
    }

    SECTION("--quiet")
    {
        std::vector<std::string> args = { "--quiet", "pattern" };
        auto result = parseGrepArgs(args);
        REQUIRE(result.has_value());
        CHECK(result->quiet);
    }

    SECTION("--silent")
    {
        std::vector<std::string> args = { "--silent", "pattern" };
        auto result = parseGrepArgs(args);
        REQUIRE(result.has_value());
        CHECK(result->quiet);
    }
}

TEST_CASE("grep.parse.filename_modes", "[grep]")
{
    SECTION("-H")
    {
        std::vector<std::string> args = { "-H", "pattern" };
        auto result = parseGrepArgs(args);
        REQUIRE(result.has_value());
        CHECK(result->filenameMode == FilenameMode::Always);
    }

    SECTION("-h")
    {
        std::vector<std::string> args = { "-h", "pattern" };
        auto result = parseGrepArgs(args);
        REQUIRE(result.has_value());
        CHECK(result->filenameMode == FilenameMode::Never);
    }
}

// ============================================================================
// Regex builder tests
// ============================================================================

TEST_CASE("grep.regex.basic", "[grep]")
{
    GrepOptions opts;
    opts.patterns = { "hello" };
    auto result = buildRegex(opts);
    REQUIRE(result.has_value());
    CHECK(std::regex_search("say hello world", *result));
    CHECK_FALSE(std::regex_search("goodbye", *result));
}

TEST_CASE("grep.regex.fixed_strings_escapes", "[grep]")
{
    GrepOptions opts;
    opts.patterns = { "a.b+c" };
    opts.fixedStrings = true;
    auto result = buildRegex(opts);
    REQUIRE(result.has_value());
    CHECK(std::regex_search("a.b+c", *result));
    CHECK_FALSE(std::regex_search("axbbc", *result)); // dot and + should be literal
}

TEST_CASE("grep.regex.word_regexp", "[grep]")
{
    GrepOptions opts;
    opts.patterns = { "foo" };
    opts.wordRegexp = true;
    auto result = buildRegex(opts);
    REQUIRE(result.has_value());
    CHECK(std::regex_search("say foo bar", *result));
    CHECK_FALSE(std::regex_search("foobar", *result));
}

TEST_CASE("grep.regex.line_regexp", "[grep]")
{
    GrepOptions opts;
    opts.patterns = { "foo" };
    opts.lineRegexp = true;
    auto result = buildRegex(opts);
    REQUIRE(result.has_value());
    CHECK(std::regex_search("foo", *result));
    CHECK_FALSE(std::regex_search("foobar", *result));
    CHECK_FALSE(std::regex_search("say foo", *result));
}

TEST_CASE("grep.regex.case_insensitive", "[grep]")
{
    GrepOptions opts;
    opts.patterns = { "hello" };
    opts.ignoreCase = true;
    auto result = buildRegex(opts);
    REQUIRE(result.has_value());
    CHECK(std::regex_search("HELLO", *result));
    CHECK(std::regex_search("Hello", *result));
}

TEST_CASE("grep.regex.multiple_patterns_joined", "[grep]")
{
    GrepOptions opts;
    opts.patterns = { "foo", "bar" };
    auto result = buildRegex(opts);
    REQUIRE(result.has_value());
    CHECK(std::regex_search("foo", *result));
    CHECK(std::regex_search("bar", *result));
    CHECK_FALSE(std::regex_search("baz", *result));
}

TEST_CASE("grep.regex.invalid_pattern", "[grep]")
{
    GrepOptions opts;
    opts.patterns = { "[invalid" };
    auto result = buildRegex(opts);
    CHECK_FALSE(result.has_value());
}

// ============================================================================
// Search engine tests
// ============================================================================

namespace
{

/// Helper to collect output from searchLines into a string.
std::string collectOutput(std::vector<std::string> const& lines,
                          std::regex const& regex,
                          GrepOptions const& opts,
                          std::string_view filename = "",
                          bool showFilename = false)
{
    std::string output;
    auto const matchCount = searchLines(
        lines, regex, opts, filename, showFilename, false, [&](std::string_view sv) { output += sv; });
    (void) matchCount;
    return output;
}

} // namespace

TEST_CASE("grep.search.basic_match", "[grep]")
{
    std::vector<std::string> lines = { "hello world", "goodbye", "hello again" };
    GrepOptions opts;
    opts.patterns = { "hello" };
    auto regex = buildRegex(opts);
    REQUIRE(regex.has_value());

    auto output = collectOutput(lines, *regex, opts);
    CHECK(output == "hello world\nhello again\n");
}

TEST_CASE("grep.search.no_match", "[grep]")
{
    std::vector<std::string> lines = { "foo", "bar", "baz" };
    GrepOptions opts;
    opts.patterns = { "xyz" };
    auto regex = buildRegex(opts);
    REQUIRE(regex.has_value());

    auto count = searchLines(lines, *regex, opts, "", false, false, [](std::string_view) {});
    CHECK(count == 0);
}

// ============================================================================
// Cancellation / FileSystem-routing tests
// ============================================================================

TEST_CASE("grep.collect.recursive_uses_filesystem", "[grep]")
{
    using endo::platform::testing::InMemoryFileSystem;

    auto const fs = InMemoryFileSystem {
        { .path = "/root", .isDirectory = true },
        { .path = "/root/a.txt", .content = "x" },
        { .path = "/root/sub", .isDirectory = true },
        { .path = "/root/sub/b.txt", .content = "y" },
    };

    GrepOptions opts;
    opts.patterns = { "x" };
    opts.recursive = true;
    opts.files = { "/root" };

    auto hasError = false;
    auto const files = collectFiles(fs, opts, [](std::string_view) {}, hasError);

    REQUIRE_FALSE(hasError);
    // Both regular files are collected through the FileSystem's coroutine walk;
    // directories are not search targets.
    REQUIRE(files.size() == 2);
}

TEST_CASE("grep.search.stop_token_aborts_early", "[grep]")
{
    endo::platform::SignalHandler::clearPendingSigint();

    // More lines than the interrupt poll interval so the throttled check is reached.
    auto const lines = std::vector<std::string>(1000, "match");
    GrepOptions opts;
    opts.patterns = { "match" };
    auto regex = buildRegex(opts);
    REQUIRE(regex.has_value());

    auto source = std::stop_source {};
    source.request_stop();

    auto const count =
        searchLines(lines, *regex, opts, "", false, false, [](std::string_view) {}, source.get_token());

    // The match loop aborts at the first poll boundary, long before all 1000 lines.
    CHECK(count < 1000);
}

TEST_CASE("grep.search.invert_match", "[grep]")
{
    std::vector<std::string> lines = { "foo", "bar", "baz" };
    GrepOptions opts;
    opts.patterns = { "bar" };
    opts.invertMatch = true;
    auto regex = buildRegex(opts);
    REQUIRE(regex.has_value());

    auto output = collectOutput(lines, *regex, opts);
    CHECK(output == "foo\nbaz\n");
}

TEST_CASE("grep.search.count_only", "[grep]")
{
    std::vector<std::string> lines = { "aa", "ab", "ac", "bb" };
    GrepOptions opts;
    opts.patterns = { "a" };
    opts.countOnly = true;
    auto regex = buildRegex(opts);
    REQUIRE(regex.has_value());

    auto output = collectOutput(lines, *regex, opts);
    CHECK(output == "3\n");
}

TEST_CASE("grep.search.only_matching", "[grep]")
{
    std::vector<std::string> lines = { "hello world" };
    GrepOptions opts;
    opts.patterns = { "world" };
    opts.onlyMatching = true;
    auto regex = buildRegex(opts);
    REQUIRE(regex.has_value());

    auto output = collectOutput(lines, *regex, opts);
    CHECK(output == "world\n");
}

TEST_CASE("grep.search.line_numbers", "[grep]")
{
    std::vector<std::string> lines = { "a", "b", "a" };
    GrepOptions opts;
    opts.patterns = { "a" };
    opts.lineNumbers = true;
    auto regex = buildRegex(opts);
    REQUIRE(regex.has_value());

    auto output = collectOutput(lines, *regex, opts);
    CHECK(output == "1:a\n3:a\n");
}

TEST_CASE("grep.search.filename_prefix", "[grep]")
{
    std::vector<std::string> lines = { "hello" };
    GrepOptions opts;
    opts.patterns = { "hello" };
    auto regex = buildRegex(opts);
    REQUIRE(regex.has_value());

    auto output = collectOutput(lines, *regex, opts, "test.txt", true);
    CHECK(output == "test.txt:hello\n");
}

TEST_CASE("grep.search.max_count", "[grep]")
{
    std::vector<std::string> lines = { "a", "a", "a", "a" };
    GrepOptions opts;
    opts.patterns = { "a" };
    opts.maxCount = 2;
    auto regex = buildRegex(opts);
    REQUIRE(regex.has_value());

    auto output = collectOutput(lines, *regex, opts);
    CHECK(output == "a\na\n");
}

TEST_CASE("grep.search.context_after", "[grep]")
{
    std::vector<std::string> lines = { "a", "b", "c", "d", "e" };
    GrepOptions opts;
    opts.patterns = { "b" };
    opts.afterContext = 1;
    auto regex = buildRegex(opts);
    REQUIRE(regex.has_value());

    auto output = collectOutput(lines, *regex, opts);
    CHECK(output == "b\nc\n");
}

TEST_CASE("grep.search.context_before", "[grep]")
{
    std::vector<std::string> lines = { "a", "b", "c", "d", "e" };
    GrepOptions opts;
    opts.patterns = { "c" };
    opts.beforeContext = 1;
    auto regex = buildRegex(opts);
    REQUIRE(regex.has_value());

    auto output = collectOutput(lines, *regex, opts);
    CHECK(output == "b\nc\n");
}

TEST_CASE("grep.search.context_both", "[grep]")
{
    std::vector<std::string> lines = { "a", "b", "c", "d", "e" };
    GrepOptions opts;
    opts.patterns = { "c" };
    opts.bothContext = 1;
    auto regex = buildRegex(opts);
    REQUIRE(regex.has_value());

    auto output = collectOutput(lines, *regex, opts);
    CHECK(output == "b\nc\nd\n");
}

TEST_CASE("grep.search.context_separator", "[grep]")
{
    std::vector<std::string> lines = { "a", "b", "c", "d", "e", "f", "g" };
    GrepOptions opts;
    opts.patterns = { "b|f" };
    opts.bothContext = 0;
    opts.afterContext = 0;
    opts.beforeContext = 0;
    auto regex = buildRegex(opts);
    REQUIRE(regex.has_value());

    // Without context, no separator needed (no context lines between groups)
    auto output = collectOutput(lines, *regex, opts);
    CHECK(output == "b\nf\n");

    // With context, separator between non-contiguous groups
    opts.afterContext = 1;
    regex = buildRegex(opts);
    output = collectOutput(lines, *regex, opts);
    CHECK(output == "b\nc\n--\nf\ng\n");
}

TEST_CASE("grep.search.context_overlap", "[grep]")
{
    std::vector<std::string> lines = { "a", "b", "c", "d", "e" };
    GrepOptions opts;
    opts.patterns = { "b|d" };
    opts.bothContext = 1;
    auto regex = buildRegex(opts);
    REQUIRE(regex.has_value());

    // Overlapping context ranges should merge (no separator, no duplicate lines)
    auto output = collectOutput(lines, *regex, opts);
    CHECK(output == "a\nb\nc\nd\ne\n");
}

// ============================================================================
// Binary detection tests
// ============================================================================

TEST_CASE("grep.binary.text_file", "[grep]")
{
    namespace fs = std::filesystem;
    auto const tmpDir = fs::temp_directory_path() / "endo_grep_test_binary";
    fs::create_directories(tmpDir);
    auto const textFile = tmpDir / "text.txt";
    {
        std::ofstream f(textFile);
        f << "hello world\nthis is text\n";
    }
    CHECK_FALSE(isBinaryFile(textFile));
    fs::remove_all(tmpDir);
}

TEST_CASE("grep.binary.null_bytes", "[grep]")
{
    namespace fs = std::filesystem;
    auto const tmpDir = fs::temp_directory_path() / "endo_grep_test_binary";
    fs::create_directories(tmpDir);
    auto const binFile = tmpDir / "binary.bin";
    {
        std::ofstream f(binFile, std::ios::binary);
        f << "hello";
        f.put('\0');
        f << "world";
    }
    CHECK(isBinaryFile(binFile));
    fs::remove_all(tmpDir);
}
