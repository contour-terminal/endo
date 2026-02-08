// SPDX-License-Identifier: Apache-2.0
#include "TestHelper.hpp"

#include <vector>

#include "AST.hpp"
#include "ASTPrinter.hpp"
#include "IRGenerator.hpp"
#include "Lexer.hpp"
#include "Parser.hpp"

namespace endo::test
{

TestRuntime::TestRuntime()
{
    // Register minimal builtins for the parser to work
    runtime.registerFunction("callproc")
        .param<std::vector<std::string>>("args")
        .returnType(CoreVM::LiteralType::Number)
        .bind(&TestRuntime::dummyCallProc, this);

    runtime.registerFunction("callproc")
        .param<bool>("last_in_chain")
        .param<std::vector<std::string>>("args")
        .returnType(CoreVM::LiteralType::Number)
        .bind(&TestRuntime::dummyCallProcPiped, this);
}

void TestRuntime::dummyCallProc(CoreVM::Params&)
{
}

void TestRuntime::dummyCallProcPiped(CoreVM::Params&)
{
}

void TestRuntime::clearErrors()
{
    report.clear();
}

bool TestRuntime::hasErrors() const
{
    return report.containsFailures();
}

TestRuntime& TestRuntime::instance()
{
    static TestRuntime instance;
    return instance;
}

std::unique_ptr<ast::Statement> parse(std::string const& source)
{
    auto& testRuntime = TestRuntime::instance();
    testRuntime.clearErrors();

    Parser parser(testRuntime.runtime, testRuntime.report, std::make_unique<StringSource>(source));
    auto result = parser.parse();

    if (testRuntime.hasErrors())
    {
        testRuntime.report.log(); // Print errors for debugging
        return nullptr;
    }

    return result;
}

std::unique_ptr<CoreVM::IRProgram> generateIR(std::string const& source)
{
    auto& testRuntime = TestRuntime::instance();
    testRuntime.clearErrors();

    Parser parser(testRuntime.runtime, testRuntime.report, std::make_unique<StringSource>(source));
    auto ast = parser.parse();

    if (!ast || testRuntime.hasErrors())
    {
        testRuntime.report.log(); // Print errors for debugging
        return nullptr;
    }

    auto ir = IRGenerator::generate(*ast, testRuntime.report, testRuntime.runtime);

    if (testRuntime.hasErrors())
    {
        testRuntime.report.log(); // Print errors for debugging
        return nullptr;
    }

    return ir;
}

bool generatesIRSuccessfully(std::string const& source)
{
    auto ir = generateIR(source);
    return ir != nullptr;
}

ast::Statement* getFirstStatement(ast::Statement* stmt)
{
    if (auto* compound = dynamic_cast<ast::CompoundStmt*>(stmt))
    {
        if (!compound->statements.empty())
        {
            return dynamic_cast<ast::Statement*>(compound->statements[0].get());
        }
    }
    return nullptr;
}

std::string parseAndPrintAST(std::string const& source)
{
    auto ast = parse(source);
    if (!ast)
    {
        // parse() already logs errors via testRuntime.report.log()
        throw ParseError("Parse failed for: \"" + source + "\"");
    }
    return ast::ASTPrinter::print(*ast);
}

} // namespace endo::test
