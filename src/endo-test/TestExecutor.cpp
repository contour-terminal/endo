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
#include <memory>

#include <platform/testing/InMemoryFileSystem.hpp>
#include <platform/testing/TestEnvironmentProvider.hpp>

#if defined(ENDO_HAS_WASM) && !defined(_WIN32)
    #include <endo-language/CompileToWasm.hpp>

    #include <algorithm>
    #include <array>
    #include <cstdio>
    #include <cstdlib>
    #include <optional>
    #include <utility>

    #include <sys/wait.h>
#endif

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

#if defined(ENDO_HAS_WASM) && !defined(_WIN32)
    /// Locates the wasmtime binary: $ENDO_WASMTIME overrides, then PATH,
    /// then ~/.cargo/bin/wasmtime.
    [[nodiscard]] std::optional<std::filesystem::path> findWasmtimeBinary()
    {
        auto const executable = [](std::filesystem::path const& path) {
            auto ec = std::error_code {};
            return std::filesystem::is_regular_file(path, ec);
        };

        if (auto const* env = std::getenv("ENDO_WASMTIME"); env != nullptr && *env != '\0')
        {
            if (auto candidate = std::filesystem::path(env); executable(candidate))
                return candidate;
            return std::nullopt; // explicit but unusable override: skip rather than fail
        }

        if (auto const* path = std::getenv("PATH"); path != nullptr)
        {
            auto remaining = std::string_view(path);
            while (!remaining.empty())
            {
                auto const colon = remaining.find(':');
                auto const dir = remaining.substr(0, colon);
                if (!dir.empty())
                    if (auto candidate = std::filesystem::path(dir) / "wasmtime"; executable(candidate))
                        return candidate;
                remaining =
                    colon == std::string_view::npos ? std::string_view {} : remaining.substr(colon + 1);
            }
        }

        if (auto const* home = std::getenv("HOME"); home != nullptr)
            if (auto candidate = std::filesystem::path(home) / ".cargo" / "bin" / "wasmtime";
                executable(candidate))
                return candidate;

        return std::nullopt;
    }

    /// Runs a command via popen, capturing stdout (stderr is discarded).
    /// @return captured output and process exit code, or nullopt if spawning failed
    [[nodiscard]] std::optional<std::pair<std::string, int>> runCapture(std::string const& command)
    {
        auto* pipe = ::popen((command + " 2>/dev/null").c_str(), "r");
        if (pipe == nullptr)
            return std::nullopt;

        auto output = std::string {};
        auto buffer = std::array<char, 4096> {};
        while (auto const n = ::fread(buffer.data(), 1, buffer.size(), pipe))
            output.append(buffer.data(), n);

        auto const status = ::pclose(pipe);
        auto const exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : 128;
        return std::pair { std::move(output), exitCode };
    }
#endif

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

    /// Rewrites standalone `_` tokens in an expect-expr expression to reference
    /// an internal binding name. In Endo, `_` is the wildcard pattern (and in
    /// expression position a partial-application placeholder), so it cannot be
    /// used as a real variable. To let users keep writing `_` in expect-expr,
    /// we textually replace each standalone `_` (not embedded in a longer
    /// identifier, not inside a string literal) with the internal name before
    /// compiling. Escapes inside string literals (`\"`, `\\`) are respected.
    [[nodiscard]] std::string rewriteUnderscoreTokens(std::string_view expr, std::string_view replacement)
    {
        std::string result;
        result.reserve(expr.size() + replacement.size());

        auto const isIdentChar = [](char c) {
            return static_cast<bool>(std::isalnum(static_cast<unsigned char>(c))) || c == '_';
        };

        bool inString = false;
        char stringQuote = '\0';

        for (size_t i = 0; i < expr.size(); ++i)
        {
            auto const ch = expr[i];

            if (inString)
            {
                result += ch;
                if (ch == '\\' && i + 1 < expr.size())
                {
                    result += expr[i + 1];
                    ++i;
                    continue;
                }
                if (ch == stringQuote)
                    inString = false;
                continue;
            }

            if (ch == '"' || ch == '\'')
            {
                inString = true;
                stringQuote = ch;
                result += ch;
                continue;
            }

            if (ch == '_')
            {
                bool const prevOk = i == 0 || !isIdentChar(expr[i - 1]);
                bool const nextOk = i + 1 == expr.size() || !isIdentChar(expr[i + 1]);
                if (prevOk && nextOk)
                {
                    result += replacement;
                    continue;
                }
            }

            result += ch;
        }
        return result;
    }

    /// Evaluates an expect-expr directive against actual output.
    /// Constructs and runs an endo program that binds the trimmed output to an
    /// internal name, rewrites standalone `_` in the expression to reference
    /// that name, and prints the boolean result of the expression.
    /// @return std::nullopt on success, or a failure message string.
    [[nodiscard]] std::optional<std::string> evaluateExpectExpr(std::string_view expr,
                                                                std::string_view actualOutput)
    {
        constexpr std::string_view OutputVar = "__endoTestActual";
        auto const trimmedOutput = rtrimNewlines(actualOutput);
        auto const escaped = escapeForEndoString(trimmedOutput);
        auto const rewritten = rewriteUnderscoreTokens(expr, OutputVar);
        auto const program = std::format("let {} = \"{}\"\nprint ({})", OutputVar, escaped, rewritten);

        TestExecutionSuccess evalExec;
        try
        {
            auto evalResult = executeSource(program);
            if (!evalResult.has_value())
                return std::format("expect-expr evaluation failed: {}", toString(evalResult.error()));
            evalExec = std::move(*evalResult);
        }
        catch (std::exception const& ex)
        {
            return std::format("expect-expr evaluation threw `{}`: `{}` (output was \"{}\")",
                               ex.what(),
                               expr,
                               escapeForDisplay(trimmedOutput));
        }

        auto const evalOutput = rtrimNewlines(evalExec.output);
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

        case TestMode::Wasm: {
#if !defined(ENDO_HAS_WASM) || defined(_WIN32)
            result.outcome = TestOutcome::Skip;
            result.failureMessage = "built without the WebAssembly backend";
            return result;
#else
            auto const finish = [&](TestOutcome outcome, std::string message = {}) {
                auto const endTime = std::chrono::steady_clock::now();
                result.duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
                result.outcome = outcome;
                if (!message.empty())
                    result.failureMessage = std::move(message);
                return result;
            };

            // The shared source-to-wasm pipeline — identical to `endo -o`.
            auto report = CoreVM::diagnostics::BufferedReport {};
            auto const wasmOutput = compileSourceToWasm(
                testFile.source, testFile.relativePath, CoreVM::wasm::WasmOptions {}, report);

            // Error tests: expect WASM compilation failure with matching diagnostics
            if (!testFile.expectedErrors.empty())
            {
                if (wasmOutput.has_value())
                    return finish(TestOutcome::Fail, "Expected WASM compilation failure but it succeeded");
                for (auto const& expectedError: testFile.expectedErrors)
                {
                    if (expectedError.empty())
                        continue; // wildcard: any failure suffices
                    auto const found = std::ranges::any_of(report, [&](auto const& message) {
                        return message.text.find(expectedError) != std::string::npos;
                    });
                    if (!found)
                        return finish(
                            TestOutcome::Fail,
                            std::format("Expected error containing \"{}\" not found", expectedError));
                }
                return finish(TestOutcome::Pass);
            }

            if (!wasmOutput.has_value())
                return finish(TestOutcome::Fail,
                              std::format("WASM compilation failed: {}",
                                          report.size() > 0 ? report[0].text : "unknown error"));

            static auto const wasmtime = findWasmtimeBinary();
            if (!wasmtime.has_value())
                return finish(TestOutcome::Skip, "wasmtime not found (set ENDO_WASMTIME or install it)");

            TempTestDir tempDir;
            auto const wasmPath = tempDir.dir / "test.wasm";
            {
                auto stream = std::ofstream(wasmPath, std::ios::binary | std::ios::trunc);
                stream.write(reinterpret_cast<char const*>(wasmOutput->binary.data()),
                             static_cast<std::streamsize>(wasmOutput->binary.size()));
                if (!stream.good())
                    return finish(TestOutcome::Fail, "failed to write the .wasm module");
            }

            auto const run = runCapture(std::format("'{}' '{}'", wasmtime->string(), wasmPath.string()));
            if (!run.has_value())
                return finish(TestOutcome::Fail, "failed to launch wasmtime");

            result.actualOutput = run->first;
            result.actualExitCode = run->second;

            if (result.actualExitCode != testFile.expectedExitCode)
                return finish(TestOutcome::Fail,
                              std::format("Exit code mismatch: expected {}, got {}",
                                          testFile.expectedExitCode,
                                          result.actualExitCode));

            if (testFile.expectNonEmptyOutput && result.actualOutput.empty())
                return finish(TestOutcome::Fail, "Expected non-empty output, but output was empty");

            if (!testFile.expectedOutput.empty())
            {
                auto const expected = joinExpectedOutput(testFile.expectedOutput);
                if (rtrimNewlines(result.actualOutput) != rtrimNewlines(expected))
                    return finish(
                        TestOutcome::Fail,
                        std::format("Output mismatch:\n        Expected: \"{}\"\n        Actual:   \"{}\"",
                                    escapeForDisplay(rtrimNewlines(expected)),
                                    escapeForDisplay(rtrimNewlines(result.actualOutput))));
            }

            return finish(TestOutcome::Pass);
#endif
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

            // Check expect-expr assertions (every expression must evaluate to true)
            for (auto const& expr: testFile.expectExprs)
            {
                if (auto msg = evaluateExpectExpr(expr, result.actualOutput))
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

            // Check expect-expr assertions (every expression must evaluate to true)
            for (auto const& expr: testFile.expectExprs)
            {
                if (auto msg = evaluateExpectExpr(expr, result.actualOutput))
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
            // Never probe the test PTY for Sixel support: the DA1 query would
            // leak escape bytes into the captured output and stall on timeout.
            shell.setSixelCapability(std::make_unique<StaticSixelCapability>(false));
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

            // Check expect-expr assertions (every expression must evaluate to true)
            for (auto const& expr: testFile.expectExprs)
            {
                if (auto msg = evaluateExpectExpr(expr, result.actualOutput))
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
