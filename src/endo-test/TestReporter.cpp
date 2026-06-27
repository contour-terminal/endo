// SPDX-License-Identifier: Apache-2.0
#include "TestReporter.hpp"

#include <algorithm>
#include <format>
#include <iostream>

namespace endo::test
{

namespace
{

    /// Formats a duration as a human-readable string.
    [[nodiscard]] std::string formatDuration(std::chrono::microseconds us)
    {
        if (us.count() < 1000)
            return std::format("{}us", us.count());
        if (us.count() < 1'000'000)
            return std::format("{:.1f}ms", static_cast<double>(us.count()) / 1000.0);
        return std::format("{:.2f}s", static_cast<double>(us.count()) / 1'000'000.0);
    }

    // ANSI color codes
    constexpr auto Reset = "\033[0m";
    constexpr auto Green = "\033[32m";
    constexpr auto Red = "\033[31m";
    constexpr auto Yellow = "\033[33m";
    constexpr auto Bold = "\033[1m";
    constexpr auto Dim = "\033[2m";

} // namespace

TestReporter::TestReporter(OutputFormat format, bool verbose): _format(format), _verbose(verbose)
{
}

void TestReporter::reportHeader(size_t totalTests)
{
    if (_format == OutputFormat::TAP)
        std::cout << "TAP version 13\n1.." << totalTests << '\n';
}

void TestReporter::reportResult(TestResult const& result, size_t index)
{
    switch (_format)
    {
        case OutputFormat::Pretty: reportPrettyResult(result); break;
        case OutputFormat::TAP: reportTAPResult(result, index); break;
    }
}

void TestReporter::reportSummary(std::vector<TestResult> const& results,
                                 std::chrono::microseconds totalDuration)
{
    switch (_format)
    {
        case OutputFormat::Pretty: reportPrettySummary(results, totalDuration); break;
        case OutputFormat::TAP: reportTAPSummary(results, totalDuration); break;
    }
}

void TestReporter::reportPrettyResult(TestResult const& result) const
{
    auto const& relPath = result.testFile->relativePath;
    auto const durationStr = formatDuration(result.duration);

    switch (result.outcome)
    {
        case TestOutcome::Pass:
            std::cout << std::format(
                "  {}PASS{}  {} {}{}{}\n", Green, Reset, relPath, Dim, "(" + durationStr + ")", Reset);
            if (_verbose && !result.actualOutput.empty())
                std::cout << std::format("        Output: \"{}\"\n", result.actualOutput);
            break;

        case TestOutcome::Fail:
            std::cout << std::format(
                "  {}FAIL{}  {} {}{}{}\n", Red, Reset, relPath, Dim, "(" + durationStr + ")", Reset);
            if (!result.failureMessage.empty())
                std::cout << std::format("        {}\n", result.failureMessage);
            break;

        case TestOutcome::Skip:
            std::cout << std::format("  {}SKIP{}  {}", Yellow, Reset, relPath);
            if (result.testFile->skipReason.has_value())
                std::cout << std::format(" {}{}{}", Dim, "(", Reset)
                          << std::format("{}", *result.testFile->skipReason)
                          << std::format("{}{}{}", Dim, ")", Reset);
            std::cout << '\n';
            break;
    }
}

void TestReporter::reportTAPResult(TestResult const& result, size_t index)
{
    auto const& description = result.testFile->description;

    switch (result.outcome)
    {
        case TestOutcome::Pass: std::cout << std::format("ok {} - {}\n", index, description); break;

        case TestOutcome::Fail:
            std::cout << std::format("not ok {} - {}\n", index, description);
            if (!result.failureMessage.empty())
            {
                std::cout << "  ---\n";
                std::cout << std::format("  message: \"{}\"\n", result.failureMessage);
                std::cout << "  ...\n";
            }
            break;

        case TestOutcome::Skip:
            std::cout << std::format("ok {} - {} # SKIP", index, description);
            if (result.testFile->skipReason.has_value())
                std::cout << " " << *result.testFile->skipReason;
            std::cout << '\n';
            break;
    }
}

void TestReporter::reportPrettySummary(std::vector<TestResult> const& results,
                                       std::chrono::microseconds totalDuration)
{
    auto const passed =
        std::ranges::count_if(results, [](auto const& r) { return r.outcome == TestOutcome::Pass; });
    auto const failed =
        std::ranges::count_if(results, [](auto const& r) { return r.outcome == TestOutcome::Fail; });
    auto const skipped =
        std::ranges::count_if(results, [](auto const& r) { return r.outcome == TestOutcome::Skip; });

    std::cout << '\n';

    if (failed > 0)
        std::cout << std::format("{}Results:{} {}{} passed{}, {}{} failed{}",
                                 Bold,
                                 Reset,
                                 Green,
                                 passed,
                                 Reset,
                                 Red,
                                 failed,
                                 Reset);
    else
        std::cout << std::format("{}Results:{} {}{} passed{}", Bold, Reset, Green, passed, Reset);

    if (skipped > 0)
        std::cout << std::format(", {}{} skipped{}", Yellow, skipped, Reset);

    std::cout << std::format(" {}{}{}\n", Dim, "(" + formatDuration(totalDuration) + ")", Reset);
}

void TestReporter::reportTAPSummary(std::vector<TestResult> const& results,
                                    std::chrono::microseconds totalDuration)
{
    auto const passed =
        std::ranges::count_if(results, [](auto const& r) { return r.outcome == TestOutcome::Pass; });
    auto const failed =
        std::ranges::count_if(results, [](auto const& r) { return r.outcome == TestOutcome::Fail; });
    auto const skipped =
        std::ranges::count_if(results, [](auto const& r) { return r.outcome == TestOutcome::Skip; });

    std::cout << std::format("# passed: {}, failed: {}, skipped: {}, duration: {}\n",
                             passed,
                             failed,
                             skipped,
                             formatDuration(totalDuration));
}

} // namespace endo::test
