// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <chrono>
#include <cstdint>
#include <vector>

#include "TestExecutor.hpp"

namespace endo::test
{

/// Output format for test results.
enum class OutputFormat : std::uint8_t
{
    Pretty, ///< Human-friendly colored output
    TAP,    ///< TAP v13 for CI integration
};

/// Reports test results to stdout.
class TestReporter
{
  public:
    /// Constructs a reporter with the given output format and verbosity.
    ///
    /// @param format  Output format (Pretty or TAP)
    /// @param verbose If true, show output for passing tests too
    explicit TestReporter(OutputFormat format = OutputFormat::Pretty, bool verbose = false);

    /// Reports the header before any tests run (TAP plan line, etc.).
    ///
    /// @param totalTests Total number of tests to run
    void reportHeader(size_t totalTests);

    /// Reports a single test result.
    ///
    /// @param result The test result to report
    /// @param index  1-based test index (for TAP numbering)
    void reportResult(TestResult const& result, size_t index);

    /// Reports the final summary after all tests complete.
    ///
    /// @param results All test results
    /// @param totalDuration Wall-clock duration for the entire test run
    void reportSummary(std::vector<TestResult> const& results, std::chrono::microseconds totalDuration);

  private:
    OutputFormat _format;
    bool _verbose;

    void reportPrettyResult(TestResult const& result) const;
    static void reportTAPResult(TestResult const& result, size_t index);
    static void reportPrettySummary(std::vector<TestResult> const& results,
                                    std::chrono::microseconds totalDuration);
    static void reportTAPSummary(std::vector<TestResult> const& results,
                                 std::chrono::microseconds totalDuration);
};

} // namespace endo::test
