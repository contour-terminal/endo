// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <CoreVM/CoreVM.hpp>

#include <expected>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>

namespace endo::ast
{
struct Statement;
}

namespace endo
{
struct FSharpPersistentState;
}

namespace endo::test
{

/// Error type for test execution failures
enum class TestError
{
    ParseFailed,
    IRGenerationFailed,
    CodeGenerationFailed,
    LinkFailed,
    FunctionNotFound,
    ExecutionFailed,
};

/// Converts a TestError to a human-readable string
[[nodiscard]] constexpr std::string_view toString(TestError error) noexcept
{
    switch (error)
    {
        case TestError::ParseFailed: return "parse failed";
        case TestError::IRGenerationFailed: return "IR generation failed";
        case TestError::CodeGenerationFailed: return "code generation failed";
        case TestError::LinkFailed: return "link failed";
        case TestError::FunctionNotFound: return "function not found";
        case TestError::ExecutionFailed: return "execution failed";
    }
    return "unknown error";
}

/// Success result from test execution
struct TestExecutionSuccess
{
    int64_t exitCode;   ///< Exit code from the program (0 = success)
    std::string output; ///< Captured output from print/println calls
};

/// Result of executing generated IR
using ExecutionResult = std::expected<TestExecutionSuccess, TestError>;

/// Test runtime holder that provides minimal CoreVM setup for parser and IR generator tests.
/// This includes dummy callproc functions required by the parser and print builtins for output capture.
struct TestRuntime
{
    CoreVM::Runtime runtime;
    CoreVM::diagnostics::BufferedReport report;
    std::string capturedOutput;                           ///< Buffer for captured output from print/println
    std::unordered_map<std::string, std::string> mockEnv; ///< Mock environment variables for env builtin
    std::unordered_map<std::string, std::string> mockWhichPaths; ///< Mock program paths for which builtin
    std::string mockCmdName;                                     ///< Current shell command being built
    std::vector<std::string> mockCmdArgs;                        ///< Arguments for the current shell command
    bool mockSubstActive = false; ///< True when in subst_start/subst_end capture mode
    std::string mockSubstBuffer;  ///< Buffer for captured output during subst mode

    TestRuntime();

    // Dummy callbacks for shell command execution
    void dummyCallProc(CoreVM::Params&);
    void dummyCallProcPiped(CoreVM::Params&);

    // Print builtins for output capture
    void builtinPrint(CoreVM::Params& params);   ///< print without newline
    void builtinPrintln(CoreVM::Params& params); ///< print with newline

    /// Sets a mock environment variable for the env builtin.
    void setMockEnvVar(std::string const& key, std::string const& value);

    /// Clears all mock environment variables.
    void clearMockEnvVars();

    /// Sets a mock program path for the which builtin.
    void setMockWhichPath(std::string const& program, std::string const& path);

    /// Clears all mock which paths.
    void clearMockWhichPaths();

    /// Returns the mock environment map (for verifying export behavior in tests).
    [[nodiscard]] std::unordered_map<std::string, std::string> const& env() const { return mockEnv; }

    /// Clears any accumulated errors before a new test.
    void clearErrors();

    /// Clears the captured output buffer.
    void clearOutput();

    /// Returns true if any errors were reported.
    [[nodiscard]] bool hasErrors() const;

    /// Returns the captured output.
    [[nodiscard]] std::string const& output() const;

    /// Returns the singleton instance of TestRuntime.
    static TestRuntime& instance();
};

/// Parses source code and returns the AST.
/// Uses the shared TestRuntime singleton.
std::unique_ptr<ast::Statement> parse(std::string const& source);

/// Parses source code and generates IR.
/// Returns nullptr if parsing or IR generation fails.
std::unique_ptr<CoreVM::IRProgram> generateIR(std::string const& source);

/// Returns true if IR generation succeeds for the given source code.
bool generatesIRSuccessfully(std::string const& source);

/// Attempts IR generation and checks that it fails with an error containing the expected substring.
/// Returns true if IR generation fails AND at least one error message contains expectedErrorSubstring.
bool generatesIRWithError(std::string const& source, std::string_view expectedErrorSubstring);

/// Helper to get the first statement from a compound statement.
/// Returns nullptr if the statement is not a compound statement or is empty.
ast::Statement* getFirstStatement(ast::Statement* stmt);

/// Exception thrown when parsing fails in test helpers.
struct ParseError: std::runtime_error
{
    using std::runtime_error::runtime_error;
};

/// Exception thrown when execution fails in test helpers.
struct ExecutionError: std::runtime_error
{
    TestError error;

    explicit ExecutionError(TestError e): std::runtime_error(std::string(toString(e))), error(e) {}
};

/// Parses source code and returns the AST-printed representation.
/// Throws ParseError if parsing fails (errors are logged before throwing).
std::string parseAndPrintAST(std::string const& source);

/// Generates IR from source code, compiles to bytecode, and executes it.
/// Returns the execution result (exit code and captured output) or an error.
ExecutionResult executeSource(std::string const& source);

/// Executes source code and returns the captured output.
/// Throws ExecutionError if execution fails.
std::string executeSourceAndGetOutput(std::string const& source);

/// Returns true if the source code executes successfully with exit code 0.
bool executesSuccessfully(std::string const& source);

/// Returns true if the source code executes and returns the expected exit code.
bool executesWithExitCode(std::string const& source, int64_t expectedExitCode);

/// Returns true if execution succeeds with expected exit code AND output.
bool executesWithResult(std::string const& source, int64_t expectedExitCode, std::string_view expectedOutput);

// =============================================================================
// Multi-prompt (REPL session) test helpers
// =============================================================================

/// Simulates a REPL session: executes multiple prompts in sequence with
/// persistent F# state, as if the user typed them one after another.
///
/// @param prompts  The source strings to execute in order
/// @return The execution result of the **last** prompt
ExecutionResult executeSession(std::vector<std::string> const& prompts);

/// Executes a multi-prompt session and returns the captured output from the last prompt.
/// Throws ExecutionError if any prompt fails.
std::string executeSessionAndGetOutput(std::vector<std::string> const& prompts);

/// Returns true if the last prompt in a session produces the expected output.
bool sessionProducesOutput(std::vector<std::string> const& prompts, std::string_view expectedOutput);

// =============================================================================
// Structured pipeline test helpers (Output Recognition Files)
// =============================================================================

/// Creates a FSharpPersistentState pre-populated with mock structured command metadata.
/// Includes: DockerPsRecord, DockerImagesRecord, GitLogRecord, GitStatusRecord.
FSharpPersistentState createMockStructuredState();

/// Executes source code with pre-populated structured command state.
/// Uses the same parse->IR->codegen->run pipeline but passes structured command persistent state.
ExecutionResult executeSourceWithStructuredState(std::string const& source);

/// Returns true if source with structured state produces expected output.
bool structuredExecutesWithOutput(std::string const& source, std::string_view expectedOutput);

} // namespace endo::test
