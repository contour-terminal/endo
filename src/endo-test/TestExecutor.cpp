// SPDX-License-Identifier: Apache-2.0
#include "TestExecutor.hpp"
#include <shell/Shell.hpp>
#include <shell/TTY.hpp>

#include <endo-language/TestHelper.hpp>
#include <endo-language/ast/AST.hpp>

#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>

#include <platform/testing/InMemoryFileSystem.hpp>
#include <platform/testing/TestEnvironmentProvider.hpp>

namespace endo::test
{

namespace
{

    /// RAII helper that creates a temporary directory and removes it on destruction.
    struct TempTestDir
    {
        std::filesystem::path dir;

        TempTestDir():
            dir(std::filesystem::temp_directory_path()
                / ("endo_test_aux_"
                   + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())))
        {
            std::filesystem::create_directories(dir);
        }

        ~TempTestDir() { std::filesystem::remove_all(dir); }

        TempTestDir(TempTestDir const&) = delete;
        TempTestDir& operator=(TempTestDir const&) = delete;
    };

    /// Joins expected output lines with newlines as separator (not terminator).
    /// Trailing newlines are normalized during comparison, so a trailing empty
    /// `# expect:` line is accepted but not required for `println` output.
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

    /// Strips trailing newline characters from a string view.
    [[nodiscard]] constexpr std::string_view rtrimNewlines(std::string_view s) noexcept
    {
        while (!s.empty() && s.back() == '\n')
            s.remove_suffix(1);
        return s;
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

    /// Escapes a string for safe embedding in an endo string literal.
    [[nodiscard]] std::string escapeForEndoString(std::string_view s)
    {
        std::string result;
        result.reserve(s.size());
        for (auto c: s)
        {
            switch (c)
            {
                case '\\': result += "\\\\"; break;
                case '"': result += "\\\""; break;
                case '\n': result += "\\n"; break;
                case '\t': result += "\\t"; break;
                case '\r': result += "\\r"; break;
                default: result += c; break;
            }
        }
        return result;
    }

    /// Evaluates an expect-expr directive against actual output.
    /// Constructs and runs an endo program that binds _ to the trimmed output
    /// and prints the boolean result of the expression.
    /// @return std::nullopt on success, or a failure message string.
    [[nodiscard]] std::optional<std::string> evaluateExpectExpr(std::string_view expr,
                                                                std::string_view actualOutput)
    {
        auto const trimmedOutput = rtrimNewlines(actualOutput);
        auto const escaped = escapeForEndoString(trimmedOutput);
        auto const program = std::format("let _ = \"{}\"\nprint ({})", escaped, expr);

        auto evalResult = executeSource(program);
        if (!evalResult.has_value())
            return std::format("expect-expr evaluation failed: {}", toString(evalResult.error()));

        auto const evalOutput = rtrimNewlines(evalResult->output);
        if (evalOutput != "true")
            return std::format("expect-expr failed: `{}` evaluated to `{}` (output was \"{}\")",
                               expr,
                               evalOutput,
                               escapeForDisplay(trimmedOutput));

        return std::nullopt;
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

    // Apply mock working directory
    if (testFile.mockCwd.has_value())
        testRuntime.setMockCwd(*testFile.mockCwd);

    // Materialize auxiliary files to a temp directory if present
    std::optional<TempTestDir> auxDir;
    if (!testFile.auxiliaryFiles.empty())
    {
        auxDir.emplace();
        for (auto const& [filename, content]: testFile.auxiliaryFiles)
        {
            auto filePath = auxDir->dir / filename;
            // Create parent directories for nested paths (e.g., Geometry/Circle.endo)
            std::filesystem::create_directories(filePath.parent_path());
            std::ofstream(filePath) << content;
        }
    }

    // Build effective module paths (aux dir prepended if present)
    auto effectiveModulePaths = testFile.modulePaths;
    if (auxDir.has_value())
        effectiveModulePaths.insert(effectiveModulePaths.begin(), auxDir->dir.string());

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
                    else if (!effectiveModulePaths.empty())
                    {
                        if (!generatesIRWithError(testFile.source,
                                                  expectedError,
                                                  effectiveModulePaths,
                                                  testFile.unusedValueDetection))
                        {
                            allFound = false;
                            result.failureMessage =
                                std::format("Expected error containing \"{}\" not found", expectedError);
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

            // Check output if expected (trailing newlines are normalized)
            if (!testFile.expectedOutput.empty())
            {
                auto const expected = joinExpectedOutput(testFile.expectedOutput);
                if (rtrimNewlines(result.actualOutput) != rtrimNewlines(expected))
                {
                    result.outcome = TestOutcome::Fail;
                    result.failureMessage =
                        std::format("Output mismatch:\n        Expected: \"{}\"\n        Actual:   \"{}\"",
                                    escapeForDisplay(rtrimNewlines(expected)),
                                    escapeForDisplay(rtrimNewlines(result.actualOutput)));
                    return result;
                }
            }

            // Check expect-expr assertion
            if (testFile.expectExpr.has_value())
            {
                if (auto msg = evaluateExpectExpr(*testFile.expectExpr, result.actualOutput))
                {
                    result.outcome = TestOutcome::Fail;
                    result.failureMessage = std::move(*msg);
                    return result;
                }
            }

            // Clean up mocks
            if (!testFile.mockEnv.empty() || !testFile.mockWhichPaths.empty())
            {
                testRuntime.clearMockEnvVars();
                testRuntime.clearMockWhichPaths();
            }
            if (testFile.mockCwd.has_value())
                testRuntime.clearMockCwd();

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
                    else if (!effectiveModulePaths.empty())
                    {
                        if (!generatesIRWithError(testFile.source,
                                                  expectedError,
                                                  effectiveModulePaths,
                                                  testFile.unusedValueDetection))
                        {
                            allFound = false;
                            result.failureMessage =
                                std::format("Expected error containing \"{}\" not found", expectedError);
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
            auto modulePaths = effectiveModulePaths;
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

            // Check output if expected (trailing newlines are normalized)
            if (!testFile.expectedOutput.empty())
            {
                auto const expected = joinExpectedOutput(testFile.expectedOutput);
                if (rtrimNewlines(result.actualOutput) != rtrimNewlines(expected))
                {
                    result.outcome = TestOutcome::Fail;
                    result.failureMessage =
                        std::format("Output mismatch:\n        Expected: \"{}\"\n        Actual:   \"{}\"",
                                    escapeForDisplay(rtrimNewlines(expected)),
                                    escapeForDisplay(rtrimNewlines(result.actualOutput)));
                    return result;
                }
            }

            // Check expect-expr assertion
            if (testFile.expectExpr.has_value())
            {
                if (auto msg = evaluateExpectExpr(*testFile.expectExpr, result.actualOutput))
                {
                    result.outcome = TestOutcome::Fail;
                    result.failureMessage = std::move(*msg);
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
            if (testFile.mockCwd.has_value())
                testRuntime.clearMockCwd();

            result.outcome = TestOutcome::Pass;
            return result;
        }

        case TestMode::Shell: {
            // Create in-memory filesystem and populate with auxiliary files
            InMemoryFileSystem fs;
            fs.addDirectory("/test");
            fs.setCurrentPath("/test");

            if (!testFile.auxiliaryFiles.empty())
            {
                for (auto const& [filename, content]: testFile.auxiliaryFiles)
                {
                    auto filePath = std::filesystem::path("/test") / filename;
                    // Create parent directories for nested paths
                    if (filePath.has_parent_path() && filePath.parent_path() != "/test")
                        fs.addDirectory(filePath.parent_path());
                    fs.addFile(filePath, content);
                }
            }

            // Create test shell with isolated environment
            TestPTY pty;
            auto const initialCwd = testFile.mockCwd.value_or("/test");
            TestEnvironment env(initialCwd);

            // Seed essential variables from real environment
            if (auto const* path = std::getenv("PATH"))
                env.set("PATH", path);
            if (auto const* home = std::getenv("HOME"))
                env.set("HOME", home);
            env.set("PWD", initialCwd);

            Shell shell(pty, env, fs);
            shell.addModuleSearchPath("/test");

            // Execute the test source through the shell
            auto const exitCode = shell.execute(testFile.source);

            auto const endTime = std::chrono::steady_clock::now();
            result.duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);

            result.actualOutput = std::string(pty.output());
            result.actualExitCode = exitCode;

            // Check exit code
            if (result.actualExitCode != testFile.expectedExitCode)
            {
                result.outcome = TestOutcome::Fail;
                result.failureMessage =
                    std::format("Exit code mismatch: expected {}, got {}\n        Output: \"{}\"",
                                testFile.expectedExitCode,
                                result.actualExitCode,
                                escapeForDisplay(result.actualOutput));
                return result;
            }

            // Check non-empty output assertion
            if (testFile.expectNonEmptyOutput && result.actualOutput.empty())
            {
                result.outcome = TestOutcome::Fail;
                result.failureMessage = "Expected non-empty output, but output was empty";
                return result;
            }

            // Check output if expected (trailing newlines are normalized)
            if (!testFile.expectedOutput.empty())
            {
                auto const expected = joinExpectedOutput(testFile.expectedOutput);
                if (rtrimNewlines(result.actualOutput) != rtrimNewlines(expected))
                {
                    result.outcome = TestOutcome::Fail;
                    result.failureMessage =
                        std::format("Output mismatch:\n        Expected: \"{}\"\n        Actual:   \"{}\"",
                                    escapeForDisplay(rtrimNewlines(expected)),
                                    escapeForDisplay(rtrimNewlines(result.actualOutput)));
                    return result;
                }
            }

            // Check expect-expr assertion
            if (testFile.expectExpr.has_value())
            {
                if (auto msg = evaluateExpectExpr(*testFile.expectExpr, result.actualOutput))
                {
                    result.outcome = TestOutcome::Fail;
                    result.failureMessage = std::move(*msg);
                    return result;
                }
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
