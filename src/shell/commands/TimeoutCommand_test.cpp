// SPDX-License-Identifier: Apache-2.0
#include <shell/commands/TimeoutCommand.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace endo::timeout;

// --- Duration parsing ---

TEST_CASE("parseDuration.integer_seconds", "[timeout]")
{
    auto result = parseDuration("5");
    REQUIRE(result.has_value());
    CHECK(result.value() == 5.0);
}

TEST_CASE("parseDuration.float_seconds", "[timeout]")
{
    auto result = parseDuration("1.5");
    REQUIRE(result.has_value());
    CHECK(result.value() == 1.5);
}

TEST_CASE("parseDuration.suffix_s", "[timeout]")
{
    auto result = parseDuration("3s");
    REQUIRE(result.has_value());
    CHECK(result.value() == 3.0);
}

TEST_CASE("parseDuration.suffix_m", "[timeout]")
{
    auto result = parseDuration("2m");
    REQUIRE(result.has_value());
    CHECK(result.value() == 120.0);
}

TEST_CASE("parseDuration.suffix_h", "[timeout]")
{
    auto result = parseDuration("1h");
    REQUIRE(result.has_value());
    CHECK(result.value() == 3600.0);
}

TEST_CASE("parseDuration.suffix_d", "[timeout]")
{
    auto result = parseDuration("0.5d");
    REQUIRE(result.has_value());
    CHECK(result.value() == 43200.0);
}

TEST_CASE("parseDuration.zero", "[timeout]")
{
    auto result = parseDuration("0");
    REQUIRE(result.has_value());
    CHECK(result.value() == 0.0);
}

TEST_CASE("parseDuration.negative", "[timeout]")
{
    auto result = parseDuration("-1");
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("parseDuration.empty", "[timeout]")
{
    auto result = parseDuration("");
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("parseDuration.invalid", "[timeout]")
{
    auto result = parseDuration("abc");
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("parseDuration.invalid_suffix", "[timeout]")
{
    auto result = parseDuration("5x");
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("parseDuration.float_with_suffix", "[timeout]")
{
    auto result = parseDuration("1.5m");
    REQUIRE(result.has_value());
    CHECK(result.value() == 90.0);
}

// --- Signal parsing ---

TEST_CASE("parseSignalSpec.name_TERM", "[timeout]")
{
    auto result = parseSignalSpec("TERM");
    REQUIRE(result.has_value());
    CHECK(result.value() == 15);
}

TEST_CASE("parseSignalSpec.name_KILL", "[timeout]")
{
    auto result = parseSignalSpec("KILL");
    REQUIRE(result.has_value());
    CHECK(result.value() == 9);
}

TEST_CASE("parseSignalSpec.name_INT", "[timeout]")
{
    auto result = parseSignalSpec("INT");
    REQUIRE(result.has_value());
    CHECK(result.value() == 2);
}

TEST_CASE("parseSignalSpec.prefixed_SIGTERM", "[timeout]")
{
    auto result = parseSignalSpec("SIGTERM");
    REQUIRE(result.has_value());
    CHECK(result.value() == 15);
}

TEST_CASE("parseSignalSpec.prefixed_SIGKILL", "[timeout]")
{
    auto result = parseSignalSpec("SIGKILL");
    REQUIRE(result.has_value());
    CHECK(result.value() == 9);
}

TEST_CASE("parseSignalSpec.numeric_9", "[timeout]")
{
    auto result = parseSignalSpec("9");
    REQUIRE(result.has_value());
    CHECK(result.value() == 9);
}

TEST_CASE("parseSignalSpec.numeric_15", "[timeout]")
{
    auto result = parseSignalSpec("15");
    REQUIRE(result.has_value());
    CHECK(result.value() == 15);
}

TEST_CASE("parseSignalSpec.invalid_name", "[timeout]")
{
    auto result = parseSignalSpec("NOSUCHSIG");
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("parseSignalSpec.empty", "[timeout]")
{
    auto result = parseSignalSpec("");
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("parseSignalSpec.out_of_range", "[timeout]")
{
    auto result = parseSignalSpec("65");
    REQUIRE_FALSE(result.has_value());
}

// --- Argument parsing ---

TEST_CASE("parseTimeoutArgs.basic", "[timeout]")
{
    auto const args = std::vector<std::string> { "5", "sleep", "10" };
    auto result = parseTimeoutArgs(args);
    REQUIRE(result.has_value());
    CHECK(result->durationSeconds == 5.0);
    CHECK(result->signal == 15);
    CHECK(result->command == std::vector<std::string> { "sleep", "10" });
}

TEST_CASE("parseTimeoutArgs.signal_long", "[timeout]")
{
    auto const args = std::vector<std::string> { "--signal=KILL", "5", "cmd" };
    auto result = parseTimeoutArgs(args);
    REQUIRE(result.has_value());
    CHECK(result->signal == 9);
    CHECK(result->durationSeconds == 5.0);
}

TEST_CASE("parseTimeoutArgs.signal_short", "[timeout]")
{
    auto const args = std::vector<std::string> { "-s", "INT", "5", "cmd" };
    auto result = parseTimeoutArgs(args);
    REQUIRE(result.has_value());
    CHECK(result->signal == 2);
}

TEST_CASE("parseTimeoutArgs.kill_after_long", "[timeout]")
{
    auto const args = std::vector<std::string> { "--kill-after=2", "5", "cmd" };
    auto result = parseTimeoutArgs(args);
    REQUIRE(result.has_value());
    CHECK(result->killAfterSeconds == 2.0);
}

TEST_CASE("parseTimeoutArgs.kill_after_short", "[timeout]")
{
    auto const args = std::vector<std::string> { "-k", "3s", "5", "cmd" };
    auto result = parseTimeoutArgs(args);
    REQUIRE(result.has_value());
    CHECK(result->killAfterSeconds == 3.0);
}

TEST_CASE("parseTimeoutArgs.preserve_status", "[timeout]")
{
    auto const args = std::vector<std::string> { "--preserve-status", "5", "cmd" };
    auto result = parseTimeoutArgs(args);
    REQUIRE(result.has_value());
    CHECK(result->preserveStatus == true);
}

TEST_CASE("parseTimeoutArgs.foreground", "[timeout]")
{
    auto const args = std::vector<std::string> { "--foreground", "5", "cmd" };
    auto result = parseTimeoutArgs(args);
    REQUIRE(result.has_value());
    CHECK(result->foreground == true);
}

TEST_CASE("parseTimeoutArgs.verbose", "[timeout]")
{
    auto const args = std::vector<std::string> { "-v", "5", "cmd" };
    auto result = parseTimeoutArgs(args);
    REQUIRE(result.has_value());
    CHECK(result->verbose == true);
}

TEST_CASE("parseTimeoutArgs.help", "[timeout]")
{
    auto const args = std::vector<std::string> { "--help" };
    auto result = parseTimeoutArgs(args);
    REQUIRE(result.has_value());
    CHECK(result->showHelp == true);
}

TEST_CASE("parseTimeoutArgs.double_dash", "[timeout]")
{
    auto const args = std::vector<std::string> { "--", "5", "cmd", "-v" };
    auto result = parseTimeoutArgs(args);
    REQUIRE(result.has_value());
    CHECK(result->durationSeconds == 5.0);
    CHECK(result->command == std::vector<std::string> { "cmd", "-v" });
}

TEST_CASE("parseTimeoutArgs.missing_duration", "[timeout]")
{
    auto const args = std::vector<std::string> {};
    auto result = parseTimeoutArgs(args);
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("parseTimeoutArgs.missing_command", "[timeout]")
{
    auto const args = std::vector<std::string> { "5" };
    auto result = parseTimeoutArgs(args);
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("parseTimeoutArgs.all_flags", "[timeout]")
{
    auto const args = std::vector<std::string> { "-s",           "KILL", "-k", "2",      "--preserve-status",
                                                 "--foreground", "-v",   "10", "my_cmd", "arg1" };
    auto result = parseTimeoutArgs(args);
    REQUIRE(result.has_value());
    CHECK(result->signal == 9);
    CHECK(result->killAfterSeconds == 2.0);
    CHECK(result->preserveStatus == true);
    CHECK(result->foreground == true);
    CHECK(result->verbose == true);
    CHECK(result->durationSeconds == 10.0);
    CHECK(result->command == std::vector<std::string> { "my_cmd", "arg1" });
}

TEST_CASE("parseTimeoutArgs.subcommand_with_dash_args", "[timeout]")
{
    auto const args = std::vector<std::string> { "5", "grep", "-rn", "TODO", "src/" };
    auto result = parseTimeoutArgs(args);
    REQUIRE(result.has_value());
    CHECK(result->durationSeconds == 5.0);
    CHECK(result->command == std::vector<std::string> { "grep", "-rn", "TODO", "src/" });
}

TEST_CASE("parseTimeoutArgs.duration_with_suffix", "[timeout]")
{
    auto const args = std::vector<std::string> { "1.5m", "cmd" };
    auto result = parseTimeoutArgs(args);
    REQUIRE(result.has_value());
    CHECK(result->durationSeconds == 90.0);
}
