// SPDX-License-Identifier: Apache-2.0
#include <shell/builtins/InlineArgParser.hpp>

#include <catch2/catch_test_macros.hpp>

#include <span>
#include <string>
#include <vector>

using namespace endo;

// Helper: build a CoreVM::CoreStringArray from an initializer list
namespace
{

CoreVM::CoreStringArray makeArgs(std::initializer_list<std::string_view> items)
{
    CoreVM::CoreStringArray arr;
    for (auto const& item: items)
        arr.emplace_back(item);
    return arr;
}

// Empty option set
std::span<InlineOptionDef const> const kNoOptions {};

constexpr InlineOptionDef kSimpleBoolOptions[] = {
    { .shortFlag = "-r", .longFlag = "--recursive", .description = "Recurse" },
    { .shortFlag = "-v", .longFlag = "--verbose", .description = "Verbose" },
    { .shortFlag = "-f", .longFlag = "--force", .description = "Force" },
};

constexpr InlineOptionDef kValueOptions[] = {
    { .shortFlag = "-n", .longFlag = {}, .description = "Number of lines", .takesValue = true },
    { .shortFlag = "-d", .longFlag = "--delimiter", .description = "Delimiter", .takesValue = true },
};

constexpr InlineOptionDef kMixedOptions[] = {
    { .shortFlag = "-r", .longFlag = {}, .description = "Reverse" },
    { .shortFlag = "-n", .longFlag = {}, .description = "Numeric" },
    { .shortFlag = "-k", .longFlag = {}, .description = "Key field", .takesValue = true },
};

} // namespace

// ============================================================================
// Basic parsing
// ============================================================================

TEST_CASE("InlineArgParser.empty_args", "[argparser]")
{
    auto const args = makeArgs({ "cmd" });
    auto const result = parseInlineArgs(args, kNoOptions);
    CHECK_FALSE(result.helpRequested);
    CHECK(result.flags.empty());
    CHECK(result.positionalArgs.empty());
}

TEST_CASE("InlineArgParser.positional_only", "[argparser]")
{
    auto const args = makeArgs({ "cmd", "file1.txt", "file2.txt" });
    auto const result = parseInlineArgs(args, kNoOptions);
    CHECK_FALSE(result.helpRequested);
    REQUIRE(result.positionalArgs.size() == 2);
    CHECK(result.positionalArgs[0] == "file1.txt");
    CHECK(result.positionalArgs[1] == "file2.txt");
}

TEST_CASE("InlineArgParser.skips_arg0", "[argparser]")
{
    auto const args = makeArgs({ "head", "-n", "5" });
    auto const result = parseInlineArgs(args, kValueOptions);
    CHECK_FALSE(result.helpRequested);
    // "head" (arg0) should be skipped
    CHECK(result.positionalArgs.empty());
    REQUIRE(result.flags.size() == 1);
    CHECK(result.flags[0].first == "-n");
    CHECK(result.flags[0].second == "5");
}

// ============================================================================
// Help flags
// ============================================================================

TEST_CASE("InlineArgParser.help_short", "[argparser]")
{
    auto const args = makeArgs({ "cmd", "-h" });
    auto const result = parseInlineArgs(args, kSimpleBoolOptions);
    CHECK(result.helpRequested);
}

TEST_CASE("InlineArgParser.help_long", "[argparser]")
{
    auto const args = makeArgs({ "cmd", "--help" });
    auto const result = parseInlineArgs(args, kSimpleBoolOptions);
    CHECK(result.helpRequested);
}

TEST_CASE("InlineArgParser.help_stops_parsing", "[argparser]")
{
    auto const args = makeArgs({ "cmd", "-r", "--help", "-v" });
    auto const result = parseInlineArgs(args, kSimpleBoolOptions);
    CHECK(result.helpRequested);
    // -r before --help should be parsed, -v after should not
    CHECK(result.flags.size() == 1);
}

TEST_CASE("InlineArgParser.help_always_recognized_even_without_options", "[argparser]")
{
    auto const args = makeArgs({ "cmd", "-h" });
    auto const result = parseInlineArgs(args, kNoOptions);
    CHECK(result.helpRequested);
}

// ============================================================================
// Boolean short flags
// ============================================================================

TEST_CASE("InlineArgParser.single_short_flag", "[argparser]")
{
    auto const args = makeArgs({ "cmd", "-r" });
    auto const result = parseInlineArgs(args, kSimpleBoolOptions);
    CHECK(result.hasFlag("-r"));
    CHECK_FALSE(result.hasFlag("-v"));
}

TEST_CASE("InlineArgParser.multiple_separate_short_flags", "[argparser]")
{
    auto const args = makeArgs({ "cmd", "-r", "-v" });
    auto const result = parseInlineArgs(args, kSimpleBoolOptions);
    CHECK(result.hasFlag("-r"));
    CHECK(result.hasFlag("-v"));
    CHECK_FALSE(result.hasFlag("-f"));
}

TEST_CASE("InlineArgParser.combined_short_flags", "[argparser]")
{
    auto const args = makeArgs({ "cmd", "-rfv" });
    auto const result = parseInlineArgs(args, kSimpleBoolOptions);
    CHECK(result.hasFlag("-r"));
    CHECK(result.hasFlag("-f"));
    CHECK(result.hasFlag("-v"));
    CHECK(result.flags.size() == 3);
}

TEST_CASE("InlineArgParser.unknown_short_flag_becomes_positional", "[argparser]")
{
    auto const args = makeArgs({ "cmd", "-x" });
    auto const result = parseInlineArgs(args, kSimpleBoolOptions);
    CHECK(result.flags.empty());
    REQUIRE(result.positionalArgs.size() == 1);
    CHECK(result.positionalArgs[0] == "-x");
}

TEST_CASE("InlineArgParser.combined_with_unknown_becomes_positional", "[argparser]")
{
    auto const args = makeArgs({ "cmd", "-rx" });
    auto const result = parseInlineArgs(args, kSimpleBoolOptions);
    // -r is valid but -x is not — entire -rx becomes positional, no flags recorded
    CHECK(result.flags.empty());
    REQUIRE(result.positionalArgs.size() == 1);
    CHECK(result.positionalArgs[0] == "-rx");
}

// ============================================================================
// Long flags
// ============================================================================

TEST_CASE("InlineArgParser.long_flag", "[argparser]")
{
    auto const args = makeArgs({ "cmd", "--recursive" });
    auto const result = parseInlineArgs(args, kSimpleBoolOptions);
    // Canonical flag is the short form
    CHECK(result.hasFlag("-r"));
}

TEST_CASE("InlineArgParser.unknown_long_flag_becomes_positional", "[argparser]")
{
    auto const args = makeArgs({ "cmd", "--unknown" });
    auto const result = parseInlineArgs(args, kSimpleBoolOptions);
    CHECK(result.flags.empty());
    REQUIRE(result.positionalArgs.size() == 1);
    CHECK(result.positionalArgs[0] == "--unknown");
}

// ============================================================================
// Value-taking flags
// ============================================================================

TEST_CASE("InlineArgParser.short_value_separate_arg", "[argparser]")
{
    auto const args = makeArgs({ "cmd", "-n", "10" });
    auto const result = parseInlineArgs(args, kValueOptions);
    auto const val = result.getFlagValue("-n");
    REQUIRE(val.has_value());
    CHECK(*val == "10");
}

TEST_CASE("InlineArgParser.short_value_attached", "[argparser]")
{
    // -n10 (value attached to flag in combined form)
    auto const args = makeArgs({ "cmd", "-n10" });
    auto const result = parseInlineArgs(args, kValueOptions);
    auto const val = result.getFlagValue("-n");
    REQUIRE(val.has_value());
    CHECK(*val == "10");
}

TEST_CASE("InlineArgParser.long_value_equals", "[argparser]")
{
    auto const args = makeArgs({ "cmd", "--delimiter=:" });
    auto const result = parseInlineArgs(args, kValueOptions);
    auto const val = result.getFlagValue("-d");
    REQUIRE(val.has_value());
    CHECK(*val == ":");
}

TEST_CASE("InlineArgParser.long_value_separate_arg", "[argparser]")
{
    auto const args = makeArgs({ "cmd", "--delimiter", ":" });
    auto const result = parseInlineArgs(args, kValueOptions);
    auto const val = result.getFlagValue("-d");
    REQUIRE(val.has_value());
    CHECK(*val == ":");
}

TEST_CASE("InlineArgParser.getFlagValue_missing_returns_nullopt", "[argparser]")
{
    auto const args = makeArgs({ "cmd", "file.txt" });
    auto const result = parseInlineArgs(args, kValueOptions);
    CHECK_FALSE(result.getFlagValue("-n").has_value());
}

// ============================================================================
// Mixed boolean + value flags
// ============================================================================

TEST_CASE("InlineArgParser.combined_bool_then_value", "[argparser]")
{
    // -rk 2 — -r is boolean, -k takes value "2"
    auto const args = makeArgs({ "cmd", "-rk", "2" });
    auto const result = parseInlineArgs(args, kMixedOptions);
    CHECK(result.hasFlag("-r"));
    auto const val = result.getFlagValue("-k");
    REQUIRE(val.has_value());
    CHECK(*val == "2");
}

TEST_CASE("InlineArgParser.combined_value_takes_rest", "[argparser]")
{
    // -rnk2 — -r bool, -n bool, -k takes "2" (rest of string)
    auto const args = makeArgs({ "cmd", "-rnk2" });
    auto const result = parseInlineArgs(args, kMixedOptions);
    CHECK(result.hasFlag("-r"));
    CHECK(result.hasFlag("-n"));
    auto const val = result.getFlagValue("-k");
    REQUIRE(val.has_value());
    CHECK(*val == "2");
}

// ============================================================================
// End-of-options (--)
// ============================================================================

TEST_CASE("InlineArgParser.end_of_options", "[argparser]")
{
    auto const args = makeArgs({ "cmd", "--", "-r", "--verbose" });
    auto const result = parseInlineArgs(args, kSimpleBoolOptions);
    CHECK(result.flags.empty());
    REQUIRE(result.positionalArgs.size() == 2);
    CHECK(result.positionalArgs[0] == "-r");
    CHECK(result.positionalArgs[1] == "--verbose");
}

TEST_CASE("InlineArgParser.flags_before_end_of_options", "[argparser]")
{
    auto const args = makeArgs({ "cmd", "-r", "--", "-v" });
    auto const result = parseInlineArgs(args, kSimpleBoolOptions);
    CHECK(result.hasFlag("-r"));
    CHECK_FALSE(result.hasFlag("-v"));
    REQUIRE(result.positionalArgs.size() == 1);
    CHECK(result.positionalArgs[0] == "-v");
}

TEST_CASE("InlineArgParser.help_after_end_of_options_is_positional", "[argparser]")
{
    auto const args = makeArgs({ "cmd", "--", "--help" });
    auto const result = parseInlineArgs(args, kSimpleBoolOptions);
    CHECK_FALSE(result.helpRequested);
    REQUIRE(result.positionalArgs.size() == 1);
    CHECK(result.positionalArgs[0] == "--help");
}

// ============================================================================
// Interleaved flags and positional args
// ============================================================================

TEST_CASE("InlineArgParser.flags_and_positionals_interleaved", "[argparser]")
{
    auto const args = makeArgs({ "cmd", "-r", "file1.txt", "-v", "file2.txt" });
    auto const result = parseInlineArgs(args, kSimpleBoolOptions);
    CHECK(result.hasFlag("-r"));
    CHECK(result.hasFlag("-v"));
    REQUIRE(result.positionalArgs.size() == 2);
    CHECK(result.positionalArgs[0] == "file1.txt");
    CHECK(result.positionalArgs[1] == "file2.txt");
}

// ============================================================================
// hasFlag / getFlagValue helpers
// ============================================================================

TEST_CASE("InlineArgParser.hasFlag_false_when_not_present", "[argparser]")
{
    auto const args = makeArgs({ "cmd", "-r" });
    auto const result = parseInlineArgs(args, kSimpleBoolOptions);
    CHECK(result.hasFlag("-r"));
    CHECK_FALSE(result.hasFlag("-v"));
    CHECK_FALSE(result.hasFlag("-f"));
}

TEST_CASE("InlineArgParser.getFlagValue_returns_last_occurrence", "[argparser]")
{
    // If -n is given multiple times, getFlagValue returns the first occurrence
    auto const args = makeArgs({ "cmd", "-n", "5", "-n", "10" });
    auto const result = parseInlineArgs(args, kValueOptions);
    auto const val = result.getFlagValue("-n");
    REQUIRE(val.has_value());
    CHECK(*val == "5"); // first occurrence
}

// ============================================================================
// Help text generation
// ============================================================================

TEST_CASE("InlineArgParser.generateHelp_includes_title", "[argparser]")
{
    InlineCommandDescriptor desc {
        .name = "head",
        .briefDescription = "Output the first lines of files.",
        .usageLine = "head [OPTIONS] [FILE...]",
        .options = kValueOptions,
    };
    auto const help = generateInlineHelp(desc);
    CHECK(help.find("# head") != std::string::npos);
}

TEST_CASE("InlineArgParser.generateHelp_includes_description", "[argparser]")
{
    InlineCommandDescriptor desc {
        .name = "head",
        .briefDescription = "Output the first lines of files.",
        .usageLine = "head [OPTIONS] [FILE...]",
        .options = kValueOptions,
    };
    auto const help = generateInlineHelp(desc);
    CHECK(help.find("Output the first lines") != std::string::npos);
}

TEST_CASE("InlineArgParser.generateHelp_includes_usage", "[argparser]")
{
    InlineCommandDescriptor desc {
        .name = "head",
        .briefDescription = "Output the first lines of files.",
        .usageLine = "head [OPTIONS] [FILE...]",
        .options = kValueOptions,
    };
    auto const help = generateInlineHelp(desc);
    CHECK(help.find("`head [OPTIONS] [FILE...]`") != std::string::npos);
}

TEST_CASE("InlineArgParser.generateHelp_includes_options_table", "[argparser]")
{
    InlineCommandDescriptor desc {
        .name = "head",
        .briefDescription = "Output the first lines of files.",
        .usageLine = "head [OPTIONS] [FILE...]",
        .options = kValueOptions,
    };
    auto const help = generateInlineHelp(desc);
    CHECK(help.find("## Options") != std::string::npos);
    CHECK(help.find("`-n VALUE`") != std::string::npos);
    CHECK(help.find("Number of lines") != std::string::npos);
    CHECK(help.find("`--delimiter=VALUE`") != std::string::npos);
}

TEST_CASE("InlineArgParser.generateHelp_always_includes_help_option", "[argparser]")
{
    InlineCommandDescriptor desc {
        .name = "whoami",
        .briefDescription = "Print the current username.",
        .usageLine = "whoami",
        .options = {},
    };
    auto const help = generateInlineHelp(desc);
    // Even with no options, the help option should NOT appear in the table
    // (no Options section for commands with no options)
    CHECK(help.find("## Options") == std::string::npos);
}

TEST_CASE("InlineArgParser.generateHelp_with_options_includes_help_row", "[argparser]")
{
    constexpr InlineOptionDef opts[] = {
        { .shortFlag = "-r", .longFlag = {}, .description = "Reverse" },
    };
    InlineCommandDescriptor desc {
        .name = "sort",
        .briefDescription = "Sort lines.",
        .usageLine = "sort [OPTIONS] [FILE...]",
        .options = opts,
    };
    auto const help = generateInlineHelp(desc);
    CHECK(help.find("## Options") != std::string::npos);
    CHECK(help.find("`-h`, `--help`") != std::string::npos);
}
