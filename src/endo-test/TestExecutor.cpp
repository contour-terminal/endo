// SPDX-License-Identifier: Apache-2.0
#include "TestExecutor.hpp"

#include <endo-language/TestHelper.hpp>
#include <endo-language/ast/AST.hpp>

#include <format>

namespace endo::test
{

namespace
{

    /// Joins expected output lines with newlines as separator (not terminator).
    /// To express a trailing newline, add an empty `# expect:` line at the end.
    [[nodiscard]] std::string joinExpectedOutput(std::vector<std::string> const& lines)
    {
        if (lines.empty())
            return {};

        std::string result;
        for (size_t i = 0; i < lines.size(); ++i)
        {
            if (i > 0)
                result += '\n';
            result += lines[i];
        }
        return result;
    }

    /// Escapes newlines and other control characters for display.
    [[nodiscard]] std::string escapeForDisplay(std::string_view s)
    {
        std::string result;
        result.reserve(s.size());
        for (auto c: s)
        {
            if (c == '\n')
                result += "\\n";
            else if (c == '\t')
                result += "\\t";
            else if (c == '\r')
                result += "\\r";
            else
                result += c;
        }
        return result;
    }

} // namespace

TestResult TestExecutor::run(TestFile const& testFile)
{
    TestResult result;
    result.testFile = &testFile;

    // Handle skipped tests
    if (testFile.skipReason.has_value())
    {
        result.outcome = TestOutcome::Skip;
        return result;
    }

    auto const startTime = std::chrono::steady_clock::now();

    // Validate mutually exclusive directives
    if (!testFile.expectedErrors.empty()
        && (!testFile.expectedOutput.empty() || testFile.expectedExitCode != 0))
    {
        result.outcome = TestOutcome::Fail;
        result.failureMessage = "expect-error and expect/expect-exit are mutually exclusive";
        return result;
    }

    auto& testRuntime = TestRuntime::instance();

    // Apply mock environment variables
    if (!testFile.mockEnv.empty() || !testFile.mockWhichPaths.empty())
    {
        testRuntime.clearMockEnvVars();
        testRuntime.clearMockWhichPaths();
        for (auto const& [key, value]: testFile.mockEnv)
            testRuntime.setMockEnvVar(key, value);
        for (auto const& [prog, path]: testFile.mockWhichPaths)
            testRuntime.setMockWhichPath(prog, path);
    }

    switch (testFile.mode)
    {
        case TestMode::ParseOnly: {
            testRuntime.clearErrors();
            auto ast = parse(testFile.source);
            auto const endTime = std::chrono::steady_clock::now();
            result.duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);

            if (!testFile.expectedErrors.empty())
            {
                // Expect parse failure
                if (ast && !testRuntime.hasErrors())
                {
                    result.outcome = TestOutcome::Fail;
                    result.failureMessage = "Expected parse error but parsing succeeded";
                }
                else
                {
                    result.outcome = TestOutcome::Pass;
                }
            }
            else
            {
                // Expect parse success
                if (!ast || testRuntime.hasErrors())
                {
                    result.outcome = TestOutcome::Fail;
                    result.failureMessage = "Parse failed";
                }
                else
                {
                    result.outcome = TestOutcome::Pass;
                }
            }
            return result;
        }

        case TestMode::IROnly: {
            if (!testFile.expectedErrors.empty())
            {
                // Expect IR generation failure with specific errors
                // An empty expected error string means "any error" (just fail IR gen)
                bool allFound = true;
                for (auto const& expectedError: testFile.expectedErrors)
                {
                    if (expectedError.empty())
                    {
                        // Wildcard: just check that IR generation fails
                        if (generatesIRSuccessfully(testFile.source, testFile.unusedValueDetection))
                        {
                            allFound = false;
                            result.failureMessage = "Expected IR generation failure but it succeeded";
                            break;
                        }
                    }
                    else if (!generatesIRWithError(
                                 testFile.source, expectedError, testFile.unusedValueDetection))
                    {
                        allFound = false;
                        result.failureMessage =
                            std::format("Expected error containing \"{}\" not found", expectedError);
                        break;
                    }
                }
                auto const endTime = std::chrono::steady_clock::now();
                result.duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
                result.outcome = allFound ? TestOutcome::Pass : TestOutcome::Fail;
            }
            else
            {
                auto success = generatesIRSuccessfully(testFile.source, testFile.unusedValueDetection);
                auto const endTime = std::chrono::steady_clock::now();
                result.duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
                result.outcome = success ? TestOutcome::Pass : TestOutcome::Fail;
                if (!success)
                    result.failureMessage = "IR generation failed";
            }
            return result;
        }

        case TestMode::Structured: {
            // Execute with structured command state
            auto execResult = executeSourceWithStructuredState(testFile.source);

            auto const endTime = std::chrono::steady_clock::now();
            result.duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);

            if (!execResult.has_value())
            {
                result.outcome = TestOutcome::Fail;
                result.failureMessage = std::format("Execution failed: {}", toString(execResult.error()));
                return result;
            }

            result.actualOutput = execResult->output;
            result.actualExitCode = execResult->exitCode;

            // Check non-empty output assertion
            if (testFile.expectNonEmptyOutput && result.actualOutput.empty())
            {
                result.outcome = TestOutcome::Fail;
                result.failureMessage = "Expected non-empty output, but output was empty";
                return result;
            }

            // Check exit code
            if (result.actualExitCode != testFile.expectedExitCode)
            {
                result.outcome = TestOutcome::Fail;
                result.failureMessage = std::format("Exit code mismatch: expected {}, got {}",
                                                    testFile.expectedExitCode,
                                                    result.actualExitCode);
                return result;
            }

            // Check output if expected
            if (!testFile.expectedOutput.empty())
            {
                auto const expected = joinExpectedOutput(testFile.expectedOutput);
                if (result.actualOutput != expected)
                {
                    result.outcome = TestOutcome::Fail;
                    result.failureMessage =
                        std::format("Output mismatch:\n        Expected: \"{}\"\n        Actual:   \"{}\"",
                                    escapeForDisplay(expected),
                                    escapeForDisplay(result.actualOutput));
                    return result;
                }
            }

            // Clean up mocks
            if (!testFile.mockEnv.empty() || !testFile.mockWhichPaths.empty())
            {
                testRuntime.clearMockEnvVars();
                testRuntime.clearMockWhichPaths();
            }

            result.outcome = TestOutcome::Pass;
            return result;
        }

        case TestMode::Execute: {
            // Error tests: expect compilation failure
            if (!testFile.expectedErrors.empty())
            {
                bool allFound = true;
                for (auto const& expectedError: testFile.expectedErrors)
                {
                    if (expectedError.empty())
                    {
                        // Wildcard: just check that IR generation fails
                        if (generatesIRSuccessfully(testFile.source, testFile.unusedValueDetection))
                        {
                            allFound = false;
                            result.failureMessage = "Expected IR generation failure but it succeeded";
                            break;
                        }
                    }
                    else if (!generatesIRWithError(
                                 testFile.source, expectedError, testFile.unusedValueDetection))
                    {
                        allFound = false;
                        result.failureMessage =
                            std::format("Expected error containing \"{}\" not found", expectedError);
                        break;
                    }
                }
                auto const endTime = std::chrono::steady_clock::now();
                result.duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
                result.outcome = allFound ? TestOutcome::Pass : TestOutcome::Fail;
                return result;
            }

            // Resolve STDLIB magic token in module paths
            auto modulePaths = testFile.modulePaths;
            for (auto& p: modulePaths)
            {
                if (p == "STDLIB")
                    p = ENDO_STDLIB_DIR;
            }

            // Execution tests
            ExecutionResult execResult;
            if (testFile.isSessionTest && !testFile.sessionPrompts.empty())
            {
                if (!modulePaths.empty())
                    execResult = executeSession(testFile.sessionPrompts, modulePaths);
                else
                    execResult = executeSession(testFile.sessionPrompts);
            }
            else
            {
                if (!modulePaths.empty())
                    execResult = executeSource(testFile.source, modulePaths, testFile.unusedValueDetection);
                else
                    execResult = executeSource(testFile.source, testFile.unusedValueDetection);
            }

            auto const endTime = std::chrono::steady_clock::now();
            result.duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);

            if (!execResult.has_value())
            {
                result.outcome = TestOutcome::Fail;
                result.failureMessage = std::format("Execution failed: {}", toString(execResult.error()));
                return result;
            }

            result.actualOutput = execResult->output;
            result.actualExitCode = execResult->exitCode;

            // Check exit code
            if (result.actualExitCode != testFile.expectedExitCode)
            {
                result.outcome = TestOutcome::Fail;
                result.failureMessage = std::format("Exit code mismatch: expected {}, got {}",
                                                    testFile.expectedExitCode,
                                                    result.actualExitCode);
                return result;
            }

            // Check non-empty output assertion
            if (testFile.expectNonEmptyOutput && result.actualOutput.empty())
            {
                result.outcome = TestOutcome::Fail;
                result.failureMessage = "Expected non-empty output, but output was empty";
                return result;
            }

            // Check output if expected
            if (!testFile.expectedOutput.empty())
            {
                auto const expected = joinExpectedOutput(testFile.expectedOutput);
                if (result.actualOutput != expected)
                {
                    result.outcome = TestOutcome::Fail;
                    result.failureMessage =
                        std::format("Output mismatch:\n        Expected: \"{}\"\n        Actual:   \"{}\"",
                                    escapeForDisplay(expected),
                                    escapeForDisplay(result.actualOutput));
                    return result;
                }
            }

            // Verify expected environment variables
            for (auto const& [key, value]: testFile.expectedEnv)
            {
                auto const& env = testRuntime.env();
                auto it = env.find(key);
                if (it == env.end())
                {
                    result.outcome = TestOutcome::Fail;
                    result.failureMessage = std::format("Expected env var \"{}\" not found", key);
                    return result;
                }
                if (it->second != value)
                {
                    result.outcome = TestOutcome::Fail;
                    result.failureMessage = std::format(
                        R"(Env var "{}" mismatch: expected "{}", got "{}")", key, value, it->second);
                    return result;
                }
            }

            // Clean up mocks
            if (!testFile.mockEnv.empty() || !testFile.mockWhichPaths.empty())
            {
                testRuntime.clearMockEnvVars();
                testRuntime.clearMockWhichPaths();
            }

            result.outcome = TestOutcome::Pass;
            return result;
        }
    }

    result.outcome = TestOutcome::Fail;
    result.failureMessage = "Unknown test mode";
    return result;
}

} // namespace endo::test
