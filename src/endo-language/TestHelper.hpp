// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <CoreVM/CoreVM.hpp>

#include <memory>
#include <stdexcept>
#include <string>

namespace endo::ast
{
class Statement;
}

namespace endo::test
{

/// Test runtime holder that provides minimal CoreVM setup for parser and IR generator tests.
/// This includes dummy callproc functions required by the parser.
struct TestRuntime
{
    CoreVM::Runtime runtime;
    CoreVM::diagnostics::BufferedReport report; // Buffered to track errors

    TestRuntime();

    void dummyCallProc(CoreVM::Params&);
    void dummyCallProcPiped(CoreVM::Params&);

    /// Clears any accumulated errors before a new test.
    void clearErrors();

    /// Returns true if any errors were reported.
    [[nodiscard]] bool hasErrors() const;

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

/// Helper to get the first statement from a compound statement.
/// Returns nullptr if the statement is not a compound statement or is empty.
ast::Statement* getFirstStatement(ast::Statement* stmt);

/// Exception thrown when parsing fails in test helpers.
struct ParseError: std::runtime_error
{
    using std::runtime_error::runtime_error;
};

/// Parses source code and returns the AST-printed representation.
/// Throws ParseError if parsing fails (errors are logged before throwing).
std::string parseAndPrintAST(std::string const& source);

} // namespace endo::test
