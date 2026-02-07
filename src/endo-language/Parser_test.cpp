// SPDX-License-Identifier: Apache-2.0

#include <CoreVM/CoreVM.hpp>

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <string_view>

#include "AST.hpp"
#include "ASTPrinter.hpp"
#include "Lexer.hpp"
#include "Parser.hpp"

using namespace std::string_view_literals;

namespace
{

// Test runtime holder for parser tests
struct TestRuntime
{
    CoreVM::Runtime runtime;
    CoreVM::diagnostics::ConsoleReport report;

    TestRuntime()
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

    void dummyCallProc(CoreVM::Params&) {}

    void dummyCallProcPiped(CoreVM::Params&) {}
};

// Helper to create a parser from source code
std::unique_ptr<endo::ast::Statement> parse(std::string const& source)
{
    static TestRuntime testRuntime;

    endo::Parser parser(
        testRuntime.runtime, testRuntime.report, std::make_unique<endo::StringSource>(source));
    return parser.parse();
}

// Helper to get the first statement from a compound statement
endo::ast::Statement* getFirstStatement(endo::ast::Statement* stmt)
{
    if (auto* compound = dynamic_cast<endo::ast::CompoundStmt*>(stmt))
    {
        if (!compound->statements.empty())
        {
            return dynamic_cast<endo::ast::Statement*>(compound->statements[0].get());
        }
    }
    return nullptr;
}

} // namespace

// =============================================================================
// F# Let Binding Tests
// =============================================================================

TEST_CASE("Parser.FSharp.let_simple_int")
{
    auto ast = parse("let x = 42");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    REQUIRE(firstStmt != nullptr);

    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(firstStmt);
    REQUIRE(letStmt != nullptr);

    CHECK(letStmt->name == "x");
    CHECK(letStmt->isMutable == false);
    CHECK(letStmt->parameters.empty());

    auto* intLit = dynamic_cast<endo::ast::IntLiteralExpr*>(letStmt->value.get());
    REQUIRE(intLit != nullptr);
    CHECK(intLit->value == 42);
}

TEST_CASE("Parser.FSharp.let_mutable")
{
    auto ast = parse("let mut counter = 0");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    REQUIRE(firstStmt != nullptr);

    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(firstStmt);
    REQUIRE(letStmt != nullptr);

    CHECK(letStmt->name == "counter");
    CHECK(letStmt->isMutable == true);
    CHECK(letStmt->parameters.empty());

    auto* intLit = dynamic_cast<endo::ast::IntLiteralExpr*>(letStmt->value.get());
    REQUIRE(intLit != nullptr);
    CHECK(intLit->value == 0);
}

TEST_CASE("Parser.FSharp.let_function_single_param")
{
    auto ast = parse("let double x = x");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    REQUIRE(firstStmt != nullptr);

    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(firstStmt);
    REQUIRE(letStmt != nullptr);

    CHECK(letStmt->name == "double");
    CHECK(letStmt->isMutable == false);
    REQUIRE(letStmt->parameters.size() == 1);
    CHECK(letStmt->parameters[0] == "x");
    CHECK(letStmt->isFunction() == true);

    auto* body = dynamic_cast<endo::ast::IdentifierExpr*>(letStmt->value.get());
    REQUIRE(body != nullptr);
    CHECK(body->name == "x");
}

TEST_CASE("Parser.FSharp.let_function_multiple_params")
{
    auto ast = parse("let add x y = x");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    REQUIRE(firstStmt != nullptr);

    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(firstStmt);
    REQUIRE(letStmt != nullptr);

    CHECK(letStmt->name == "add");
    REQUIRE(letStmt->parameters.size() == 2);
    CHECK(letStmt->parameters[0] == "x");
    CHECK(letStmt->parameters[1] == "y");
}

// =============================================================================
// F# Expression Tests - Literals
// =============================================================================

TEST_CASE("Parser.FSharp.bool_literal_true")
{
    auto ast = parse("let b = true");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(firstStmt);
    REQUIRE(letStmt != nullptr);

    auto* boolLit = dynamic_cast<endo::ast::BoolLiteralExpr*>(letStmt->value.get());
    REQUIRE(boolLit != nullptr);
    CHECK(boolLit->value == true);
}

TEST_CASE("Parser.FSharp.bool_literal_false")
{
    auto ast = parse("let b = false");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(firstStmt);
    REQUIRE(letStmt != nullptr);

    auto* boolLit = dynamic_cast<endo::ast::BoolLiteralExpr*>(letStmt->value.get());
    REQUIRE(boolLit != nullptr);
    CHECK(boolLit->value == false);
}

TEST_CASE("Parser.FSharp.identifier_expr")
{
    auto ast = parse("let y = x");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(firstStmt);
    REQUIRE(letStmt != nullptr);

    auto* ident = dynamic_cast<endo::ast::IdentifierExpr*>(letStmt->value.get());
    REQUIRE(ident != nullptr);
    CHECK(ident->name == "x");
}

// =============================================================================
// F# Expression Tests - Parentheses
// =============================================================================

TEST_CASE("Parser.FSharp.parenthesized_expr")
{
    auto ast = parse("let x = (42)");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(firstStmt);
    REQUIRE(letStmt != nullptr);

    auto* parenExpr = dynamic_cast<endo::ast::ParenExpr*>(letStmt->value.get());
    REQUIRE(parenExpr != nullptr);

    auto* intLit = dynamic_cast<endo::ast::IntLiteralExpr*>(parenExpr->inner.get());
    REQUIRE(intLit != nullptr);
    CHECK(intLit->value == 42);
}

// =============================================================================
// F# Expression Tests - Function Application
// =============================================================================

TEST_CASE("Parser.FSharp.function_application_single")
{
    auto ast = parse("let y = f 1");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(firstStmt);
    REQUIRE(letStmt != nullptr);

    auto* appExpr = dynamic_cast<endo::ast::ApplicationExpr*>(letStmt->value.get());
    REQUIRE(appExpr != nullptr);

    auto* func = dynamic_cast<endo::ast::IdentifierExpr*>(appExpr->function.get());
    REQUIRE(func != nullptr);
    CHECK(func->name == "f");

    auto* arg = dynamic_cast<endo::ast::IntLiteralExpr*>(appExpr->argument.get());
    REQUIRE(arg != nullptr);
    CHECK(arg->value == 1);
}

TEST_CASE("Parser.FSharp.function_application_curried")
{
    auto ast = parse("let z = f 1 2");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(firstStmt);
    REQUIRE(letStmt != nullptr);

    // f 1 2 is parsed as ((f 1) 2)
    auto* outerApp = dynamic_cast<endo::ast::ApplicationExpr*>(letStmt->value.get());
    REQUIRE(outerApp != nullptr);

    auto* innerApp = dynamic_cast<endo::ast::ApplicationExpr*>(outerApp->function.get());
    REQUIRE(innerApp != nullptr);

    auto* func = dynamic_cast<endo::ast::IdentifierExpr*>(innerApp->function.get());
    REQUIRE(func != nullptr);
    CHECK(func->name == "f");

    auto* arg1 = dynamic_cast<endo::ast::IntLiteralExpr*>(innerApp->argument.get());
    REQUIRE(arg1 != nullptr);
    CHECK(arg1->value == 1);

    auto* arg2 = dynamic_cast<endo::ast::IntLiteralExpr*>(outerApp->argument.get());
    REQUIRE(arg2 != nullptr);
    CHECK(arg2->value == 2);
}

// =============================================================================
// F# ASTPrinter Tests
// =============================================================================

TEST_CASE("Parser.FSharp.ASTPrinter.let_simple")
{
    auto ast = parse("let x = 42");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    REQUIRE(firstStmt != nullptr);

    std::string printed = endo::ast::ASTPrinter::print(*firstStmt);
    CHECK(printed == "let x = 42");
}

TEST_CASE("Parser.FSharp.ASTPrinter.let_mutable")
{
    auto ast = parse("let mut x = 0");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    REQUIRE(firstStmt != nullptr);

    std::string printed = endo::ast::ASTPrinter::print(*firstStmt);
    CHECK(printed == "let mut x = 0");
}

TEST_CASE("Parser.FSharp.ASTPrinter.let_function")
{
    auto ast = parse("let add x y = x");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    REQUIRE(firstStmt != nullptr);

    std::string printed = endo::ast::ASTPrinter::print(*firstStmt);
    CHECK(printed == "let add x y = x");
}

TEST_CASE("Parser.FSharp.ASTPrinter.bool_literal")
{
    auto ast = parse("let b = true");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    REQUIRE(firstStmt != nullptr);

    std::string printed = endo::ast::ASTPrinter::print(*firstStmt);
    CHECK(printed == "let b = true");
}

// =============================================================================
// F# Lambda Expression Tests
// =============================================================================

TEST_CASE("Parser.FSharp.lambda_simple")
{
    auto ast = parse("let f = fun x -> x");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    REQUIRE(firstStmt != nullptr);

    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(firstStmt);
    REQUIRE(letStmt != nullptr);

    CHECK(letStmt->name == "f");
    CHECK(letStmt->parameters.empty());

    auto* lambda = dynamic_cast<endo::ast::LambdaExpr*>(letStmt->value.get());
    REQUIRE(lambda != nullptr);

    REQUIRE(lambda->parameters.size() == 1);
    CHECK(lambda->parameters[0] == "x");

    auto* body = dynamic_cast<endo::ast::IdentifierExpr*>(lambda->body.get());
    REQUIRE(body != nullptr);
    CHECK(body->name == "x");
}

TEST_CASE("Parser.FSharp.lambda_multiple_params")
{
    auto ast = parse("let add = fun x y -> x");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(firstStmt);
    REQUIRE(letStmt != nullptr);

    auto* lambda = dynamic_cast<endo::ast::LambdaExpr*>(letStmt->value.get());
    REQUIRE(lambda != nullptr);

    REQUIRE(lambda->parameters.size() == 2);
    CHECK(lambda->parameters[0] == "x");
    CHECK(lambda->parameters[1] == "y");
}

TEST_CASE("Parser.FSharp.lambda_with_binary_expr")
{
    auto ast = parse("let double = fun x -> x");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(firstStmt);
    REQUIRE(letStmt != nullptr);

    auto* lambda = dynamic_cast<endo::ast::LambdaExpr*>(letStmt->value.get());
    REQUIRE(lambda != nullptr);

    REQUIRE(lambda->parameters.size() == 1);
    CHECK(lambda->parameters[0] == "x");
}

TEST_CASE("Parser.FSharp.lambda_nested")
{
    // fun x -> fun y -> x is equivalent to fun x y -> x
    auto ast = parse("let f = fun x -> fun y -> x");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(firstStmt);
    REQUIRE(letStmt != nullptr);

    auto* outerLambda = dynamic_cast<endo::ast::LambdaExpr*>(letStmt->value.get());
    REQUIRE(outerLambda != nullptr);
    REQUIRE(outerLambda->parameters.size() == 1);
    CHECK(outerLambda->parameters[0] == "x");

    auto* innerLambda = dynamic_cast<endo::ast::LambdaExpr*>(outerLambda->body.get());
    REQUIRE(innerLambda != nullptr);
    REQUIRE(innerLambda->parameters.size() == 1);
    CHECK(innerLambda->parameters[0] == "y");

    auto* body = dynamic_cast<endo::ast::IdentifierExpr*>(innerLambda->body.get());
    REQUIRE(body != nullptr);
    CHECK(body->name == "x");
}

TEST_CASE("Parser.FSharp.lambda_in_pipeline")
{
    // Lambda used in a pipeline
    auto ast = parse("let mapped = x |> fun n -> n");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(firstStmt);
    REQUIRE(letStmt != nullptr);

    auto* pipeline = dynamic_cast<endo::ast::PipelineExpr*>(letStmt->value.get());
    REQUIRE(pipeline != nullptr);

    auto* value = dynamic_cast<endo::ast::IdentifierExpr*>(pipeline->value.get());
    REQUIRE(value != nullptr);
    CHECK(value->name == "x");

    auto* lambda = dynamic_cast<endo::ast::LambdaExpr*>(pipeline->function.get());
    REQUIRE(lambda != nullptr);
    REQUIRE(lambda->parameters.size() == 1);
    CHECK(lambda->parameters[0] == "n");
}

TEST_CASE("Parser.FSharp.lambda_parenthesized")
{
    auto ast = parse("let f = (fun x -> x)");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(firstStmt);
    REQUIRE(letStmt != nullptr);

    auto* parenExpr = dynamic_cast<endo::ast::ParenExpr*>(letStmt->value.get());
    REQUIRE(parenExpr != nullptr);

    auto* lambda = dynamic_cast<endo::ast::LambdaExpr*>(parenExpr->inner.get());
    REQUIRE(lambda != nullptr);
    REQUIRE(lambda->parameters.size() == 1);
    CHECK(lambda->parameters[0] == "x");
}

TEST_CASE("Parser.FSharp.ASTPrinter.lambda_simple")
{
    auto ast = parse("let f = fun x -> x");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    REQUIRE(firstStmt != nullptr);

    std::string printed = endo::ast::ASTPrinter::print(*firstStmt);
    CHECK(printed == "let f = fun x -> x");
}

TEST_CASE("Parser.FSharp.ASTPrinter.lambda_multiple_params")
{
    auto ast = parse("let add = fun x y -> x");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    REQUIRE(firstStmt != nullptr);

    std::string printed = endo::ast::ASTPrinter::print(*firstStmt);
    CHECK(printed == "let add = fun x y -> x");
}
