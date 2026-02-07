// SPDX-License-Identifier: Apache-2.0
#include "TestHelper.hpp"

#include <vector>

#include "AST.hpp"
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

TestRuntime& TestRuntime::instance()
{
    static TestRuntime instance;
    return instance;
}

std::unique_ptr<ast::Statement> parse(std::string const& source)
{
    auto& testRuntime = TestRuntime::instance();

    Parser parser(testRuntime.runtime, testRuntime.report, std::make_unique<StringSource>(source));
    return parser.parse();
}

std::unique_ptr<CoreVM::IRProgram> generateIR(std::string const& source)
{
    auto& testRuntime = TestRuntime::instance();

    Parser parser(testRuntime.runtime, testRuntime.report, std::make_unique<StringSource>(source));
    auto ast = parser.parse();
    if (!ast)
        return nullptr;

    return IRGenerator::generate(*ast, testRuntime.report, testRuntime.runtime);
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

} // namespace endo::test
