// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <chrono>
#include <cstdint>
#include <string>

#include "TestFileParser.hpp"

namespace endo::test
{

/// Outcome of a single test execution.
enum class TestOutcome : std::uint8_t
{
    Pass, ///< Test passed all checks
    Fail, ///< Test failed (output or exit code mismatch, or unexpected error)
    Skip, ///< Test was skipped
};

/// Result of executing a single test file.
struct TestResult
{
    TestFile const* testFile = nullptr;      ///< Pointer to the parsed test file
    TestOutcome outcome = TestOutcome::Fail; ///< Pass/Fail/Skip
    std::string failureMessage;              ///< Descriptive failure message (empty on pass)
    std::string actualOutput;                ///< Actual captured output
    int64_t actualExitCode = 0;              ///< Actual exit code
    std::chrono::microseconds duration = std::chrono::microseconds(0); ///< Wall-clock execution time
};

/// Executes .endo test files using the TestRuntime singleton.
class TestExecutor
{
  public:
    /// Runs a single parsed test file.
    ///
    /// @param testFile The parsed test file to execute
    /// @return The test result
    [[nodiscard]] static TestResult run(TestFile const& testFile);
};

} // namespace endo::test
