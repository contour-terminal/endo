// SPDX-License-Identifier: Apache-2.0

#include <endo-language/TestHelper.hpp>
#include <endo-language/ast/AST.hpp>
#include <endo-language/types/Type.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace endo::test;

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

TEST_CASE("Parser.FSharp.let_export")
{
    auto ast = parse("let export X = 42");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    REQUIRE(firstStmt != nullptr);

    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(firstStmt);
    REQUIRE(letStmt != nullptr);

    CHECK(letStmt->name == "X");
    CHECK(letStmt->isExported == true);
    CHECK(letStmt->isMutable == false);
    CHECK(letStmt->isRecursive == false);
    CHECK(letStmt->parameters.empty());
}

TEST_CASE("Parser.FSharp.let_export_mut")
{
    auto ast = parse("let export mut X = 42");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    REQUIRE(firstStmt != nullptr);

    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(firstStmt);
    REQUIRE(letStmt != nullptr);

    CHECK(letStmt->name == "X");
    CHECK(letStmt->isExported == true);
    CHECK(letStmt->isMutable == true);
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
    CHECK(letStmt->parameters[0].name == "x");
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
    CHECK(letStmt->parameters[0].name == "x");
    CHECK(letStmt->parameters[1].name == "y");
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
    CHECK(parseAndPrintAST("let x = 42") == "let x = 42");
}

TEST_CASE("Parser.FSharp.ASTPrinter.let_mutable")
{
    CHECK(parseAndPrintAST("let mut x = 0") == "let mut x = 0");
}

TEST_CASE("Parser.FSharp.ASTPrinter.let_export")
{
    CHECK(parseAndPrintAST("let export X = 42") == "let export X = 42");
}

TEST_CASE("Parser.FSharp.ASTPrinter.let_export_mut")
{
    CHECK(parseAndPrintAST("let export mut X = 0") == "let export mut X = 0");
}

TEST_CASE("Parser.FSharp.ASTPrinter.let_function")
{
    CHECK(parseAndPrintAST("let add x y = x") == "let add x y = x");
}

TEST_CASE("Parser.FSharp.ASTPrinter.bool_literal")
{
    CHECK(parseAndPrintAST("let b = true") == "let b = true");
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
    CHECK(lambda->parameters[0].name == "x");

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
    CHECK(lambda->parameters[0].name == "x");
    CHECK(lambda->parameters[1].name == "y");
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
    CHECK(lambda->parameters[0].name == "x");
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
    CHECK(outerLambda->parameters[0].name == "x");

    auto* innerLambda = dynamic_cast<endo::ast::LambdaExpr*>(outerLambda->body.get());
    REQUIRE(innerLambda != nullptr);
    REQUIRE(innerLambda->parameters.size() == 1);
    CHECK(innerLambda->parameters[0].name == "y");

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
    CHECK(lambda->parameters[0].name == "n");
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
    CHECK(lambda->parameters[0].name == "x");
}

TEST_CASE("Parser.FSharp.ASTPrinter.lambda_simple")
{
    CHECK(parseAndPrintAST("let f = fun x -> x") == "let f = fun x -> x");
}

TEST_CASE("Parser.FSharp.ASTPrinter.lambda_multiple_params")
{
    CHECK(parseAndPrintAST("let add = fun x y -> x") == "let add = fun x y -> x");
}

// =============================================================================
// F# Match Expression Tests
// =============================================================================

TEST_CASE("Parser.FSharp.match_literal_patterns")
{
    auto ast = parse("let r = match x with | 0 -> true | 1 -> false");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    REQUIRE(firstStmt != nullptr);

    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(firstStmt);
    REQUIRE(letStmt != nullptr);
    CHECK(letStmt->name == "r");

    auto* matchExpr = dynamic_cast<endo::ast::MatchExpr*>(letStmt->value.get());
    REQUIRE(matchExpr != nullptr);

    // Check scrutinee
    auto* scrutinee = dynamic_cast<endo::ast::IdentifierExpr*>(matchExpr->scrutinee.get());
    REQUIRE(scrutinee != nullptr);
    CHECK(scrutinee->name == "x");

    // Check arms count
    REQUIRE(matchExpr->arms.size() == 2);

    // First arm: | 0 -> true
    auto const& arm1 = matchExpr->arms[0];
    REQUIRE(arm1.guard == nullptr); // No guard
    auto* body1 = dynamic_cast<endo::ast::BoolLiteralExpr*>(arm1.body.get());
    REQUIRE(body1 != nullptr);
    CHECK(body1->value == true);

    // Second arm: | 1 -> false
    auto const& arm2 = matchExpr->arms[1];
    REQUIRE(arm2.guard == nullptr);
    auto* body2 = dynamic_cast<endo::ast::BoolLiteralExpr*>(arm2.body.get());
    REQUIRE(body2 != nullptr);
    CHECK(body2->value == false);
}

TEST_CASE("Parser.FSharp.match_wildcard_pattern")
{
    auto ast = parse("let r = match n with | _ -> 42");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(firstStmt);
    REQUIRE(letStmt != nullptr);

    auto* matchExpr = dynamic_cast<endo::ast::MatchExpr*>(letStmt->value.get());
    REQUIRE(matchExpr != nullptr);
    REQUIRE(matchExpr->arms.size() == 1);

    // Check wildcard pattern
    auto const& arm = matchExpr->arms[0];
    auto* wildcardPattern = dynamic_cast<endo::pattern::WildcardPattern*>(arm.pattern.get());
    REQUIRE(wildcardPattern != nullptr);

    auto* body = dynamic_cast<endo::ast::IntLiteralExpr*>(arm.body.get());
    REQUIRE(body != nullptr);
    CHECK(body->value == 42);
}

TEST_CASE("Parser.FSharp.match_variable_pattern")
{
    auto ast = parse("let r = match x with | n -> n");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(firstStmt);
    REQUIRE(letStmt != nullptr);

    auto* matchExpr = dynamic_cast<endo::ast::MatchExpr*>(letStmt->value.get());
    REQUIRE(matchExpr != nullptr);
    REQUIRE(matchExpr->arms.size() == 1);

    // Check variable pattern
    auto const& arm = matchExpr->arms[0];
    auto* varPattern = dynamic_cast<endo::pattern::VariablePattern*>(arm.pattern.get());
    REQUIRE(varPattern != nullptr);
    CHECK(varPattern->name == "n");

    // Body should reference the bound variable
    auto* body = dynamic_cast<endo::ast::IdentifierExpr*>(arm.body.get());
    REQUIRE(body != nullptr);
    CHECK(body->name == "n");
}

TEST_CASE("Parser.FSharp.match_with_guard")
{
    auto ast = parse("let r = match n with | x when x < 0 -> true | _ -> false");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(firstStmt);
    REQUIRE(letStmt != nullptr);

    auto* matchExpr = dynamic_cast<endo::ast::MatchExpr*>(letStmt->value.get());
    REQUIRE(matchExpr != nullptr);
    REQUIRE(matchExpr->arms.size() == 2);

    // First arm has a guard: x when x < 0
    auto const& arm1 = matchExpr->arms[0];
    auto* varPattern = dynamic_cast<endo::pattern::VariablePattern*>(arm1.pattern.get());
    REQUIRE(varPattern != nullptr);
    CHECK(varPattern->name == "x");

    // Guard expression should be: x < 0
    REQUIRE(arm1.guard != nullptr);
    auto* guardExpr = dynamic_cast<endo::ast::BinaryExpr*>(arm1.guard.get());
    REQUIRE(guardExpr != nullptr);
    CHECK(guardExpr->op == endo::ast::BinaryOp::Lt);

    // Second arm has no guard
    auto const& arm2 = matchExpr->arms[1];
    REQUIRE(arm2.guard == nullptr);
}

TEST_CASE("Parser.FSharp.match_constructor_pattern")
{
    auto ast = parse("let r = match opt with | Some x -> x | None -> 0");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(firstStmt);
    REQUIRE(letStmt != nullptr);

    auto* matchExpr = dynamic_cast<endo::ast::MatchExpr*>(letStmt->value.get());
    REQUIRE(matchExpr != nullptr);
    REQUIRE(matchExpr->arms.size() == 2);

    // First arm: Some x
    auto const& arm1 = matchExpr->arms[0];
    auto* ctorPattern1 = dynamic_cast<endo::pattern::ConstructorPattern*>(arm1.pattern.get());
    REQUIRE(ctorPattern1 != nullptr);
    CHECK(ctorPattern1->name == "Some");
    REQUIRE(ctorPattern1->payload.has_value());
    auto* argPattern = dynamic_cast<endo::pattern::VariablePattern*>(ctorPattern1->payload->get());
    REQUIRE(argPattern != nullptr);
    CHECK(argPattern->name == "x");

    // Second arm: None
    auto const& arm2 = matchExpr->arms[1];
    auto* ctorPattern2 = dynamic_cast<endo::pattern::ConstructorPattern*>(arm2.pattern.get());
    REQUIRE(ctorPattern2 != nullptr);
    CHECK(ctorPattern2->name == "None");
    CHECK(!ctorPattern2->payload.has_value());
}

TEST_CASE("Parser.FSharp.match_tuple_pattern_simple")
{
    // Test simple tuple pattern first
    auto ast = parse("let r = match p with | (0, 0) -> true");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(firstStmt);
    REQUIRE(letStmt != nullptr);

    auto* matchExpr = dynamic_cast<endo::ast::MatchExpr*>(letStmt->value.get());
    REQUIRE(matchExpr != nullptr);
    REQUIRE(matchExpr->arms.size() == 1);

    auto const& arm = matchExpr->arms[0];
    auto* tuplePattern = dynamic_cast<endo::pattern::TuplePattern*>(arm.pattern.get());
    REQUIRE(tuplePattern != nullptr);
    REQUIRE(tuplePattern->elements.size() == 2);
}

TEST_CASE("Parser.FSharp.match_tuple_pattern")
{
    auto ast = parse("let r = match p with | (0, 0) -> true | (x, y) -> false");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(firstStmt);
    REQUIRE(letStmt != nullptr);

    auto* matchExpr = dynamic_cast<endo::ast::MatchExpr*>(letStmt->value.get());
    REQUIRE(matchExpr != nullptr);
    REQUIRE(matchExpr->arms.size() == 2);

    // First arm: (0, 0)
    auto const& arm1 = matchExpr->arms[0];
    auto* tuplePattern1 = dynamic_cast<endo::pattern::TuplePattern*>(arm1.pattern.get());
    REQUIRE(tuplePattern1 != nullptr);
    REQUIRE(tuplePattern1->elements.size() == 2);

    // Both elements should be literal patterns with value 0
    auto* elem1 = dynamic_cast<endo::pattern::LiteralPattern*>(tuplePattern1->elements[0].get());
    REQUIRE(elem1 != nullptr);
    CHECK(std::get<int64_t>(elem1->value) == 0);

    auto* elem2 = dynamic_cast<endo::pattern::LiteralPattern*>(tuplePattern1->elements[1].get());
    REQUIRE(elem2 != nullptr);
    CHECK(std::get<int64_t>(elem2->value) == 0);

    // Second arm: (x, y)
    auto const& arm2 = matchExpr->arms[1];
    auto* tuplePattern2 = dynamic_cast<endo::pattern::TuplePattern*>(arm2.pattern.get());
    REQUIRE(tuplePattern2 != nullptr);
    REQUIRE(tuplePattern2->elements.size() == 2);

    auto* xPattern = dynamic_cast<endo::pattern::VariablePattern*>(tuplePattern2->elements[0].get());
    REQUIRE(xPattern != nullptr);
    CHECK(xPattern->name == "x");

    auto* yPattern = dynamic_cast<endo::pattern::VariablePattern*>(tuplePattern2->elements[1].get());
    REQUIRE(yPattern != nullptr);
    CHECK(yPattern->name == "y");
}

TEST_CASE("Parser.FSharp.match_bare_tuple_scrutinee")
{
    // Bare tuple scrutinee: match a, b with | (x, y) -> x + y
    auto ast = parse("let r = match a, b with | (x, y) -> x + y");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(firstStmt);
    REQUIRE(letStmt != nullptr);

    auto* matchExpr = dynamic_cast<endo::ast::MatchExpr*>(letStmt->value.get());
    REQUIRE(matchExpr != nullptr);

    // Scrutinee should be a TupleExpr
    auto* tupleScrutinee = dynamic_cast<endo::ast::TupleExpr*>(matchExpr->scrutinee.get());
    REQUIRE(tupleScrutinee != nullptr);
    CHECK(tupleScrutinee->elements.size() == 2);

    REQUIRE(matchExpr->arms.size() == 1);
    auto* tuplePattern = dynamic_cast<endo::pattern::TuplePattern*>(matchExpr->arms[0].pattern.get());
    REQUIRE(tuplePattern != nullptr);
    CHECK(tuplePattern->elements.size() == 2);
}

TEST_CASE("Parser.FSharp.match_bare_tuple_pattern")
{
    // Bare tuple pattern: match p with | 0, 0 -> true | x, y -> false
    auto ast = parse("let r = match p with | 0, 0 -> true | x, y -> false");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(firstStmt);
    REQUIRE(letStmt != nullptr);

    auto* matchExpr = dynamic_cast<endo::ast::MatchExpr*>(letStmt->value.get());
    REQUIRE(matchExpr != nullptr);
    REQUIRE(matchExpr->arms.size() == 2);

    // First arm: 0, 0 -> parsed as TuplePattern
    auto* tp1 = dynamic_cast<endo::pattern::TuplePattern*>(matchExpr->arms[0].pattern.get());
    REQUIRE(tp1 != nullptr);
    CHECK(tp1->elements.size() == 2);

    // Second arm: x, y -> parsed as TuplePattern
    auto* tp2 = dynamic_cast<endo::pattern::TuplePattern*>(matchExpr->arms[1].pattern.get());
    REQUIRE(tp2 != nullptr);
    CHECK(tp2->elements.size() == 2);
}

TEST_CASE("Parser.FSharp.match_bare_tuple_scrutinee_and_pattern")
{
    // Both scrutinee and pattern bare: match 3, 4 with | a, b -> a + b
    auto ast = parse("let r = match 3, 4 with | a, b -> a + b");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(firstStmt);
    REQUIRE(letStmt != nullptr);

    auto* matchExpr = dynamic_cast<endo::ast::MatchExpr*>(letStmt->value.get());
    REQUIRE(matchExpr != nullptr);

    auto* tupleScrutinee = dynamic_cast<endo::ast::TupleExpr*>(matchExpr->scrutinee.get());
    REQUIRE(tupleScrutinee != nullptr);
    CHECK(tupleScrutinee->elements.size() == 2);

    REQUIRE(matchExpr->arms.size() == 1);
    auto* tp = dynamic_cast<endo::pattern::TuplePattern*>(matchExpr->arms[0].pattern.get());
    REQUIRE(tp != nullptr);
    CHECK(tp->elements.size() == 2);
}

TEST_CASE("Parser.FSharp.match_bare_tuple_constructor_pattern")
{
    // Constructor patterns inside bare tuple: | Some f, Some l -> ...
    auto ast = parse("let r = match p with | Some f, Some l -> f + l");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(firstStmt);
    REQUIRE(letStmt != nullptr);

    auto* matchExpr = dynamic_cast<endo::ast::MatchExpr*>(letStmt->value.get());
    REQUIRE(matchExpr != nullptr);
    REQUIRE(matchExpr->arms.size() == 1);

    auto* tp = dynamic_cast<endo::pattern::TuplePattern*>(matchExpr->arms[0].pattern.get());
    REQUIRE(tp != nullptr);
    CHECK(tp->elements.size() == 2);

    // Each element should be a ConstructorPattern
    auto* ctor1 = dynamic_cast<endo::pattern::ConstructorPattern*>(tp->elements[0].get());
    REQUIRE(ctor1 != nullptr);
    CHECK(ctor1->name == "Some");

    auto* ctor2 = dynamic_cast<endo::pattern::ConstructorPattern*>(tp->elements[1].get());
    REQUIRE(ctor2 != nullptr);
    CHECK(ctor2->name == "Some");
}

TEST_CASE("Parser.FSharp.match_bare_tuple_or_pattern")
{
    // Or-pattern with bare tuples: | None, _ | _, None -> 0 | Some a, Some b -> a + b
    auto ast = parse("let r = match p with | None, _ | _, None -> 0 | Some a, Some b -> a + b");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(firstStmt);
    REQUIRE(letStmt != nullptr);

    auto* matchExpr = dynamic_cast<endo::ast::MatchExpr*>(letStmt->value.get());
    REQUIRE(matchExpr != nullptr);
    REQUIRE(matchExpr->arms.size() == 2);

    // First arm should be an OrPattern
    auto* orPat = dynamic_cast<endo::pattern::OrPattern*>(matchExpr->arms[0].pattern.get());
    REQUIRE(orPat != nullptr);
    CHECK(orPat->alternatives.size() == 2);

    // Each alternative should be a TuplePattern
    auto* alt1 = dynamic_cast<endo::pattern::TuplePattern*>(orPat->alternatives[0].get());
    REQUIRE(alt1 != nullptr);
    CHECK(alt1->elements.size() == 2);

    auto* alt2 = dynamic_cast<endo::pattern::TuplePattern*>(orPat->alternatives[1].get());
    REQUIRE(alt2 != nullptr);
    CHECK(alt2->elements.size() == 2);

    // Second arm: Some a, Some b -> TuplePattern
    auto* tp = dynamic_cast<endo::pattern::TuplePattern*>(matchExpr->arms[1].pattern.get());
    REQUIRE(tp != nullptr);
    CHECK(tp->elements.size() == 2);
}

TEST_CASE("Parser.FSharp.match_bare_tuple_3_elements")
{
    // 3-element bare tuple: match a, b, c with | x, y, z -> x + y + z
    auto ast = parse("let r = match a, b, c with | x, y, z -> x + y + z");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(firstStmt);
    REQUIRE(letStmt != nullptr);

    auto* matchExpr = dynamic_cast<endo::ast::MatchExpr*>(letStmt->value.get());
    REQUIRE(matchExpr != nullptr);

    auto* tupleScrutinee = dynamic_cast<endo::ast::TupleExpr*>(matchExpr->scrutinee.get());
    REQUIRE(tupleScrutinee != nullptr);
    CHECK(tupleScrutinee->elements.size() == 3);

    REQUIRE(matchExpr->arms.size() == 1);
    auto* tp = dynamic_cast<endo::pattern::TuplePattern*>(matchExpr->arms[0].pattern.get());
    REQUIRE(tp != nullptr);
    CHECK(tp->elements.size() == 3);
}

TEST_CASE("Parser.FSharp.match_as_pattern")
{
    auto ast = parse("let r = match p with | x as point -> point");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(firstStmt);
    REQUIRE(letStmt != nullptr);

    auto* matchExpr = dynamic_cast<endo::ast::MatchExpr*>(letStmt->value.get());
    REQUIRE(matchExpr != nullptr);
    REQUIRE(matchExpr->arms.size() == 1);

    // Check as pattern: x as point
    auto const& arm = matchExpr->arms[0];
    auto* asPattern = dynamic_cast<endo::pattern::AsPattern*>(arm.pattern.get());
    REQUIRE(asPattern != nullptr);
    CHECK(asPattern->name == "point");

    auto* innerPattern = dynamic_cast<endo::pattern::VariablePattern*>(asPattern->inner.get());
    REQUIRE(innerPattern != nullptr);
    CHECK(innerPattern->name == "x");

    // Body references the alias
    auto* body = dynamic_cast<endo::ast::IdentifierExpr*>(arm.body.get());
    REQUIRE(body != nullptr);
    CHECK(body->name == "point");
}

TEST_CASE("Parser.FSharp.match_multiple_arms")
{
    auto ast = parse("let r = match n with | 0 -> 10 | 1 -> 20 | 2 -> 30 | _ -> 40");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(firstStmt);
    REQUIRE(letStmt != nullptr);

    auto* matchExpr = dynamic_cast<endo::ast::MatchExpr*>(letStmt->value.get());
    REQUIRE(matchExpr != nullptr);
    REQUIRE(matchExpr->arms.size() == 4);

    // Verify the bodies have correct values
    auto* body0 = dynamic_cast<endo::ast::IntLiteralExpr*>(matchExpr->arms[0].body.get());
    REQUIRE(body0 != nullptr);
    CHECK(body0->value == 10);

    auto* body1 = dynamic_cast<endo::ast::IntLiteralExpr*>(matchExpr->arms[1].body.get());
    REQUIRE(body1 != nullptr);
    CHECK(body1->value == 20);

    auto* body2 = dynamic_cast<endo::ast::IntLiteralExpr*>(matchExpr->arms[2].body.get());
    REQUIRE(body2 != nullptr);
    CHECK(body2->value == 30);

    auto* body3 = dynamic_cast<endo::ast::IntLiteralExpr*>(matchExpr->arms[3].body.get());
    REQUIRE(body3 != nullptr);
    CHECK(body3->value == 40);
}

TEST_CASE("Parser.FSharp.ASTPrinter.match_simple")
{
    CHECK(parseAndPrintAST("let r = match x with | 0 -> true | _ -> false")
          == "let r = match x with | 0 -> true | _ -> false");
}

TEST_CASE("Parser.FSharp.ASTPrinter.match_with_guard")
{
    CHECK(parseAndPrintAST("let r = match n with | x when x < 0 -> true | _ -> false")
          == "let r = match n with | x when (x < 0) -> true | _ -> false");
}

TEST_CASE("Parser.FSharp.ASTPrinter.match_constructor")
{
    CHECK(parseAndPrintAST("let r = match opt with | Some x -> x | None -> 0")
          == "let r = match opt with | Some x -> x | None -> 0");
}

// ============================================================================
// List Literal Tests
// ============================================================================

TEST_CASE("Parser.FSharp.list_empty")
{
    auto ast = parse("let xs = []");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(firstStmt);
    REQUIRE(letStmt != nullptr);
    CHECK(letStmt->name == "xs");

    auto* listExpr = dynamic_cast<endo::ast::ListExpr*>(letStmt->value.get());
    REQUIRE(listExpr != nullptr);
    CHECK(listExpr->elements.empty());
}

TEST_CASE("Parser.FSharp.list_single_element")
{
    auto ast = parse("let xs = [42]");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(firstStmt);
    REQUIRE(letStmt != nullptr);

    auto* listExpr = dynamic_cast<endo::ast::ListExpr*>(letStmt->value.get());
    REQUIRE(listExpr != nullptr);
    REQUIRE(listExpr->elements.size() == 1);

    auto* elem = dynamic_cast<endo::ast::IntLiteralExpr*>(listExpr->elements[0].get());
    REQUIRE(elem != nullptr);
    CHECK(elem->value == 42);
}

TEST_CASE("Parser.FSharp.list_multiple_integers")
{
    auto ast = parse("let nums = [1;2;3]");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(firstStmt);
    REQUIRE(letStmt != nullptr);
    CHECK(letStmt->name == "nums");

    auto* listExpr = dynamic_cast<endo::ast::ListExpr*>(letStmt->value.get());
    REQUIRE(listExpr != nullptr);
    REQUIRE(listExpr->elements.size() == 3);

    auto* elem0 = dynamic_cast<endo::ast::IntLiteralExpr*>(listExpr->elements[0].get());
    auto* elem1 = dynamic_cast<endo::ast::IntLiteralExpr*>(listExpr->elements[1].get());
    auto* elem2 = dynamic_cast<endo::ast::IntLiteralExpr*>(listExpr->elements[2].get());
    REQUIRE(elem0 != nullptr);
    REQUIRE(elem1 != nullptr);
    REQUIRE(elem2 != nullptr);
    CHECK(elem0->value == 1);
    CHECK(elem1->value == 2);
    CHECK(elem2->value == 3);
}

TEST_CASE("Parser.FSharp.list_with_spaces")
{
    auto ast = parse("let nums = [1; 2; 3]");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(firstStmt);
    REQUIRE(letStmt != nullptr);

    auto* listExpr = dynamic_cast<endo::ast::ListExpr*>(letStmt->value.get());
    REQUIRE(listExpr != nullptr);
    REQUIRE(listExpr->elements.size() == 3);

    auto* elem0 = dynamic_cast<endo::ast::IntLiteralExpr*>(listExpr->elements[0].get());
    auto* elem1 = dynamic_cast<endo::ast::IntLiteralExpr*>(listExpr->elements[1].get());
    auto* elem2 = dynamic_cast<endo::ast::IntLiteralExpr*>(listExpr->elements[2].get());
    REQUIRE(elem0 != nullptr);
    REQUIRE(elem1 != nullptr);
    REQUIRE(elem2 != nullptr);
    CHECK(elem0->value == 1);
    CHECK(elem1->value == 2);
    CHECK(elem2->value == 3);
}

TEST_CASE("Parser.FSharp.list_booleans")
{
    auto ast = parse("let flags = [true;false;true]");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(firstStmt);
    REQUIRE(letStmt != nullptr);

    auto* listExpr = dynamic_cast<endo::ast::ListExpr*>(letStmt->value.get());
    REQUIRE(listExpr != nullptr);
    REQUIRE(listExpr->elements.size() == 3);

    auto* elem0 = dynamic_cast<endo::ast::BoolLiteralExpr*>(listExpr->elements[0].get());
    auto* elem1 = dynamic_cast<endo::ast::BoolLiteralExpr*>(listExpr->elements[1].get());
    auto* elem2 = dynamic_cast<endo::ast::BoolLiteralExpr*>(listExpr->elements[2].get());
    REQUIRE(elem0 != nullptr);
    REQUIRE(elem1 != nullptr);
    REQUIRE(elem2 != nullptr);
    CHECK(elem0->value == true);
    CHECK(elem1->value == false);
    CHECK(elem2->value == true);
}

TEST_CASE("Parser.FSharp.list_floats")
{
    auto ast = parse("let ratios = [1.0;2.5;3.14]");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(firstStmt);
    REQUIRE(letStmt != nullptr);

    auto* listExpr = dynamic_cast<endo::ast::ListExpr*>(letStmt->value.get());
    REQUIRE(listExpr != nullptr);
    REQUIRE(listExpr->elements.size() == 3);

    auto* elem0 = dynamic_cast<endo::ast::FloatLiteralExpr*>(listExpr->elements[0].get());
    auto* elem1 = dynamic_cast<endo::ast::FloatLiteralExpr*>(listExpr->elements[1].get());
    auto* elem2 = dynamic_cast<endo::ast::FloatLiteralExpr*>(listExpr->elements[2].get());
    REQUIRE(elem0 != nullptr);
    REQUIRE(elem1 != nullptr);
    REQUIRE(elem2 != nullptr);
    CHECK(elem0->value == 1.0);
    CHECK(elem1->value == 2.5);
    CHECK(elem2->value == 3.14);
}

TEST_CASE("Parser.FSharp.list_identifiers")
{
    auto ast = parse("let vars = [x;y;z]");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(firstStmt);
    REQUIRE(letStmt != nullptr);

    auto* listExpr = dynamic_cast<endo::ast::ListExpr*>(letStmt->value.get());
    REQUIRE(listExpr != nullptr);
    REQUIRE(listExpr->elements.size() == 3);

    auto* elem0 = dynamic_cast<endo::ast::IdentifierExpr*>(listExpr->elements[0].get());
    auto* elem1 = dynamic_cast<endo::ast::IdentifierExpr*>(listExpr->elements[1].get());
    auto* elem2 = dynamic_cast<endo::ast::IdentifierExpr*>(listExpr->elements[2].get());
    REQUIRE(elem0 != nullptr);
    REQUIRE(elem1 != nullptr);
    REQUIRE(elem2 != nullptr);
    CHECK(elem0->name == "x");
    CHECK(elem1->name == "y");
    CHECK(elem2->name == "z");
}

TEST_CASE("Parser.FSharp.list_multiline")
{
    auto ast = parse("let xs = [\n  1;\n  2;\n  3\n]");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(firstStmt);
    REQUIRE(letStmt != nullptr);
    CHECK(letStmt->name == "xs");

    auto* listExpr = dynamic_cast<endo::ast::ListExpr*>(letStmt->value.get());
    REQUIRE(listExpr != nullptr);
    REQUIRE(listExpr->elements.size() == 3);
}

// ============================================================================
// List Range Tests
// ============================================================================

TEST_CASE("Parser.FSharp.list_range_simple")
{
    auto ast = parse("let nums = [1..10]");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(firstStmt);
    REQUIRE(letStmt != nullptr);
    CHECK(letStmt->name == "nums");

    auto* rangeExpr = dynamic_cast<endo::ast::ListRangeExpr*>(letStmt->value.get());
    REQUIRE(rangeExpr != nullptr);

    auto* start = dynamic_cast<endo::ast::IntLiteralExpr*>(rangeExpr->start.get());
    REQUIRE(start != nullptr);
    CHECK(start->value == 1);

    auto* end = dynamic_cast<endo::ast::IntLiteralExpr*>(rangeExpr->end.get());
    REQUIRE(end != nullptr);
    CHECK(end->value == 10);

    // No step specified
    CHECK(rangeExpr->step == nullptr);
}

TEST_CASE("Parser.FSharp.list_range_with_step")
{
    auto ast = parse("let evens = [2..2..20]");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(firstStmt);
    REQUIRE(letStmt != nullptr);

    auto* rangeExpr = dynamic_cast<endo::ast::ListRangeExpr*>(letStmt->value.get());
    REQUIRE(rangeExpr != nullptr);

    auto* start = dynamic_cast<endo::ast::IntLiteralExpr*>(rangeExpr->start.get());
    REQUIRE(start != nullptr);
    CHECK(start->value == 2);

    auto* step = dynamic_cast<endo::ast::IntLiteralExpr*>(rangeExpr->step.get());
    REQUIRE(step != nullptr);
    CHECK(step->value == 2);

    auto* end = dynamic_cast<endo::ast::IntLiteralExpr*>(rangeExpr->end.get());
    REQUIRE(end != nullptr);
    CHECK(end->value == 20);
}

TEST_CASE("Parser.FSharp.list_range_countdown")
{
    auto ast = parse("let countdown = [10..-1..0]");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(firstStmt);
    REQUIRE(letStmt != nullptr);

    auto* rangeExpr = dynamic_cast<endo::ast::ListRangeExpr*>(letStmt->value.get());
    REQUIRE(rangeExpr != nullptr);

    auto* start = dynamic_cast<endo::ast::IntLiteralExpr*>(rangeExpr->start.get());
    REQUIRE(start != nullptr);
    CHECK(start->value == 10);

    auto* step = dynamic_cast<endo::ast::IntLiteralExpr*>(rangeExpr->step.get());
    REQUIRE(step != nullptr);
    CHECK(step->value == -1);

    auto* end = dynamic_cast<endo::ast::IntLiteralExpr*>(rangeExpr->end.get());
    REQUIRE(end != nullptr);
    CHECK(end->value == 0);
}

// ============================================================================
// List ASTPrinter Tests
// ============================================================================

TEST_CASE("Parser.FSharp.ASTPrinter.list_empty")
{
    CHECK(parseAndPrintAST("let xs = []") == "let xs = []");
}

TEST_CASE("Parser.FSharp.ASTPrinter.list_elements")
{
    CHECK(parseAndPrintAST("let nums = [1;2;3]") == "let nums = [1; 2; 3]");
}

TEST_CASE("Parser.FSharp.ASTPrinter.list_range")
{
    CHECK(parseAndPrintAST("let nums = [1..10]") == "let nums = [1..10]");
}

TEST_CASE("Parser.FSharp.ASTPrinter.list_range_with_step")
{
    CHECK(parseAndPrintAST("let evens = [2..2..20]") == "let evens = [2..2..20]");
}

// ============================================================================
// List Comprehension Tests
// ============================================================================

TEST_CASE("Parser.FSharp.list_comprehension_simple")
{
    auto ast = parse("let squares = [for x in 1..10 -> x]");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(firstStmt);
    REQUIRE(letStmt != nullptr);
    CHECK(letStmt->name == "squares");

    auto* compExpr = dynamic_cast<endo::ast::ListComprehensionExpr*>(letStmt->value.get());
    REQUIRE(compExpr != nullptr);
    CHECK(compExpr->variable == "x");
    CHECK(compExpr->filter == nullptr);

    // Source should be a range expression
    auto* rangeExpr = dynamic_cast<endo::ast::ListRangeExpr*>(compExpr->source.get());
    REQUIRE(rangeExpr != nullptr);

    // Body should be an identifier "x"
    auto* bodyId = dynamic_cast<endo::ast::IdentifierExpr*>(compExpr->body.get());
    REQUIRE(bodyId != nullptr);
    CHECK(bodyId->name == "x");
}

TEST_CASE("Parser.FSharp.list_comprehension_with_binary_expr")
{
    auto ast = parse("let squares = [for x in 1..10 -> x * x]");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(firstStmt);
    REQUIRE(letStmt != nullptr);

    auto* compExpr = dynamic_cast<endo::ast::ListComprehensionExpr*>(letStmt->value.get());
    REQUIRE(compExpr != nullptr);
    CHECK(compExpr->variable == "x");

    // Body should be a binary expression x * x
    auto* binExpr = dynamic_cast<endo::ast::BinaryExpr*>(compExpr->body.get());
    REQUIRE(binExpr != nullptr);
    CHECK(binExpr->op == endo::ast::BinaryOp::Mul);

    auto* left = dynamic_cast<endo::ast::IdentifierExpr*>(binExpr->left.get());
    auto* right = dynamic_cast<endo::ast::IdentifierExpr*>(binExpr->right.get());
    REQUIRE(left != nullptr);
    REQUIRE(right != nullptr);
    CHECK(left->name == "x");
    CHECK(right->name == "x");
}

TEST_CASE("Parser.FSharp.list_comprehension_with_filter")
{
    auto ast = parse("let evens = [for x in 1..10 when x > 5 -> x]");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(firstStmt);
    REQUIRE(letStmt != nullptr);

    auto* compExpr = dynamic_cast<endo::ast::ListComprehensionExpr*>(letStmt->value.get());
    REQUIRE(compExpr != nullptr);
    CHECK(compExpr->variable == "x");

    // Filter should be x > 5
    REQUIRE(compExpr->filter != nullptr);
    auto* filterExpr = dynamic_cast<endo::ast::BinaryExpr*>(compExpr->filter.get());
    REQUIRE(filterExpr != nullptr);
    CHECK(filterExpr->op == endo::ast::BinaryOp::Gt);
}

TEST_CASE("Parser.FSharp.list_comprehension_with_identifier_source")
{
    auto ast = parse("let doubled = [for x in items -> x]");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(firstStmt);
    REQUIRE(letStmt != nullptr);

    auto* compExpr = dynamic_cast<endo::ast::ListComprehensionExpr*>(letStmt->value.get());
    REQUIRE(compExpr != nullptr);

    // Source should be identifier "items"
    auto* srcId = dynamic_cast<endo::ast::IdentifierExpr*>(compExpr->source.get());
    REQUIRE(srcId != nullptr);
    CHECK(srcId->name == "items");
}

TEST_CASE("Parser.FSharp.ASTPrinter.list_comprehension_simple")
{
    CHECK(parseAndPrintAST("let squares = [for x in 1..10 -> x]") == "let squares = [for x in [1..10] -> x]");
}

TEST_CASE("Parser.FSharp.ASTPrinter.list_comprehension_with_filter")
{
    CHECK(parseAndPrintAST("let evens = [for x in items when x > 5 -> x]")
          == "let evens = [for x in items when (x > 5) -> x]");
}

TEST_CASE("Parser.FSharp.list_comprehension_with_addition")
{
    auto ast = parse("let incremented = [for x in 1..5 -> x + 1]");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(firstStmt);
    REQUIRE(letStmt != nullptr);

    auto* compExpr = dynamic_cast<endo::ast::ListComprehensionExpr*>(letStmt->value.get());
    REQUIRE(compExpr != nullptr);

    // Body should be a binary expression x + 1
    auto* binExpr = dynamic_cast<endo::ast::BinaryExpr*>(compExpr->body.get());
    REQUIRE(binExpr != nullptr);
    CHECK(binExpr->op == endo::ast::BinaryOp::Add);
}

TEST_CASE("Parser.FSharp.list_comprehension_with_step_range")
{
    auto ast = parse("let evens = [for x in 2..2..10 -> x]");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(firstStmt);
    REQUIRE(letStmt != nullptr);

    auto* compExpr = dynamic_cast<endo::ast::ListComprehensionExpr*>(letStmt->value.get());
    REQUIRE(compExpr != nullptr);

    // Source should be a range with step
    auto* rangeExpr = dynamic_cast<endo::ast::ListRangeExpr*>(compExpr->source.get());
    REQUIRE(rangeExpr != nullptr);
    REQUIRE(rangeExpr->step != nullptr); // Should have a step

    auto* start = dynamic_cast<endo::ast::IntLiteralExpr*>(rangeExpr->start.get());
    auto* step = dynamic_cast<endo::ast::IntLiteralExpr*>(rangeExpr->step.get());
    auto* end = dynamic_cast<endo::ast::IntLiteralExpr*>(rangeExpr->end.get());
    REQUIRE(start != nullptr);
    REQUIRE(step != nullptr);
    REQUIRE(end != nullptr);
    CHECK(start->value == 2);
    CHECK(step->value == 2);
    CHECK(end->value == 10);
}

// ============================================================================
// Shell Command Expression Tests (& prefix)
// ============================================================================

TEST_CASE("Parser.FSharp.shell_command_simple")
{
    CHECK(parseAndPrintAST("let output = & echo hello") == "let output = & echo hello");
}

TEST_CASE("Parser.FSharp.shell_command_with_args")
{
    CHECK(parseAndPrintAST("let files = & ls -la /tmp") == "let files = & ls -la /tmp");
}

TEST_CASE("Parser.FSharp.shell_command_git_diff")
{
    // Verifies that .. is not tokenized as F# range operator
    CHECK(parseAndPrintAST("let diff = & git diff HEAD..master") == "let diff = & git diff HEAD..master");
}

TEST_CASE("Parser.FSharp.shell_command_with_redirect")
{
    CHECK(parseAndPrintAST("let output = & cat < input.txt") == "let output = & cat <input.txt");
}

TEST_CASE("Parser.FSharp.shell_command_with_shell_pipe")
{
    CHECK(parseAndPrintAST("let lines = & cat file.txt | grep pattern")
          == "let lines = & cat file.txt | grep pattern");
}

// ============================================================================
// Option/Result Expression Tests
// ============================================================================

TEST_CASE("Parser.FSharp.option_some")
{
    auto ast = parse("let x = Some 42");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(firstStmt);
    REQUIRE(letStmt != nullptr);
    CHECK(letStmt->name == "x");

    auto* optExpr = dynamic_cast<endo::ast::OptionExpr*>(letStmt->value.get());
    REQUIRE(optExpr != nullptr);
    CHECK(optExpr->isSome == true);
    REQUIRE(optExpr->value != nullptr);

    auto* intLit = dynamic_cast<endo::ast::IntLiteralExpr*>(optExpr->value.get());
    REQUIRE(intLit != nullptr);
    CHECK(intLit->value == 42);
}

TEST_CASE("Parser.FSharp.option_none")
{
    auto ast = parse("let x = None");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(firstStmt);
    REQUIRE(letStmt != nullptr);
    CHECK(letStmt->name == "x");

    auto* optExpr = dynamic_cast<endo::ast::OptionExpr*>(letStmt->value.get());
    REQUIRE(optExpr != nullptr);
    CHECK(optExpr->isSome == false);
    CHECK(optExpr->value == nullptr);
}

TEST_CASE("Parser.FSharp.result_ok")
{
    auto ast = parse("let r = Ok 100");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(firstStmt);
    REQUIRE(letStmt != nullptr);
    CHECK(letStmt->name == "r");

    auto* resExpr = dynamic_cast<endo::ast::ResultExpr*>(letStmt->value.get());
    REQUIRE(resExpr != nullptr);
    CHECK(resExpr->isOk == true);
    REQUIRE(resExpr->payload != nullptr);

    auto* intLit = dynamic_cast<endo::ast::IntLiteralExpr*>(resExpr->payload.get());
    REQUIRE(intLit != nullptr);
    CHECK(intLit->value == 100);
}

TEST_CASE("Parser.FSharp.result_error")
{
    auto ast = parse("let r = Error 42");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(firstStmt);
    REQUIRE(letStmt != nullptr);
    CHECK(letStmt->name == "r");

    auto* resExpr = dynamic_cast<endo::ast::ResultExpr*>(letStmt->value.get());
    REQUIRE(resExpr != nullptr);
    CHECK(resExpr->isOk == false);
    REQUIRE(resExpr->payload != nullptr);

    auto* intLit = dynamic_cast<endo::ast::IntLiteralExpr*>(resExpr->payload.get());
    REQUIRE(intLit != nullptr);
    CHECK(intLit->value == 42);
}

// ============================================================================
// Try Expression Tests (? postfix operator)
// ============================================================================

TEST_CASE("Parser.FSharp.try_expr_simple")
{
    auto ast = parse("let f x = x?");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(firstStmt);
    REQUIRE(letStmt != nullptr);
    CHECK(letStmt->name == "f");
    REQUIRE(letStmt->parameters.size() == 1);
    CHECK(letStmt->parameters[0].name == "x");

    auto* tryExpr = dynamic_cast<endo::ast::TryExpr*>(letStmt->value.get());
    REQUIRE(tryExpr != nullptr);

    auto* innerIdent = dynamic_cast<endo::ast::IdentifierExpr*>(tryExpr->operand.get());
    REQUIRE(innerIdent != nullptr);
    CHECK(innerIdent->name == "x");
}

TEST_CASE("Parser.FSharp.try_expr_chained")
{
    // x?? should parse as (x?)?
    auto ast = parse("let f x = x??");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(firstStmt);
    REQUIRE(letStmt != nullptr);

    auto* outerTry = dynamic_cast<endo::ast::TryExpr*>(letStmt->value.get());
    REQUIRE(outerTry != nullptr);

    auto* innerTry = dynamic_cast<endo::ast::TryExpr*>(outerTry->operand.get());
    REQUIRE(innerTry != nullptr);

    auto* innerIdent = dynamic_cast<endo::ast::IdentifierExpr*>(innerTry->operand.get());
    REQUIRE(innerIdent != nullptr);
    CHECK(innerIdent->name == "x");
}

TEST_CASE("Parser.FSharp.try_expr_with_function_call")
{
    auto ast = parse("let f = (getValue x)?");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(firstStmt);
    REQUIRE(letStmt != nullptr);

    auto* tryExpr = dynamic_cast<endo::ast::TryExpr*>(letStmt->value.get());
    REQUIRE(tryExpr != nullptr);

    auto* parenExpr = dynamic_cast<endo::ast::ParenExpr*>(tryExpr->operand.get());
    REQUIRE(parenExpr != nullptr);

    auto* appExpr = dynamic_cast<endo::ast::ApplicationExpr*>(parenExpr->inner.get());
    REQUIRE(appExpr != nullptr);
}

// ============================================================================
// Try-With Expression Tests
// ============================================================================

TEST_CASE("Parser.FSharp.try_with_simple")
{
    auto ast = parse("let r = try getValue x with | Error e -> 0");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(firstStmt);
    REQUIRE(letStmt != nullptr);
    CHECK(letStmt->name == "r");

    auto* tryWithExpr = dynamic_cast<endo::ast::TryWithExpr*>(letStmt->value.get());
    REQUIRE(tryWithExpr != nullptr);

    // Check body is an application
    auto* bodyApp = dynamic_cast<endo::ast::ApplicationExpr*>(tryWithExpr->body.get());
    REQUIRE(bodyApp != nullptr);

    // Check handlers
    REQUIRE(tryWithExpr->handlers.size() == 1);
    auto const& handler = tryWithExpr->handlers[0];

    // Handler pattern should be Error e
    auto* ctorPattern = dynamic_cast<endo::pattern::ConstructorPattern*>(handler.pattern.get());
    REQUIRE(ctorPattern != nullptr);
    CHECK(ctorPattern->name == "Error");

    // Handler body should be 0
    auto* handlerBody = dynamic_cast<endo::ast::IntLiteralExpr*>(handler.body.get());
    REQUIRE(handlerBody != nullptr);
    CHECK(handlerBody->value == 0);
}

TEST_CASE("Parser.FSharp.try_with_multiple_handlers")
{
    auto ast = parse("let r = try riskyOp x with | Error 1 -> 10 | Error _ -> 0");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(firstStmt);
    REQUIRE(letStmt != nullptr);

    auto* tryWithExpr = dynamic_cast<endo::ast::TryWithExpr*>(letStmt->value.get());
    REQUIRE(tryWithExpr != nullptr);

    REQUIRE(tryWithExpr->handlers.size() == 2);

    // First handler: Error 1 -> 10
    auto* body1 = dynamic_cast<endo::ast::IntLiteralExpr*>(tryWithExpr->handlers[0].body.get());
    REQUIRE(body1 != nullptr);
    CHECK(body1->value == 10);

    // Second handler: Error _ -> 0
    auto* body2 = dynamic_cast<endo::ast::IntLiteralExpr*>(tryWithExpr->handlers[1].body.get());
    REQUIRE(body2 != nullptr);
    CHECK(body2->value == 0);
}

// ============================================================================
// Option/Result ASTPrinter Tests
// ============================================================================

TEST_CASE("Parser.FSharp.ASTPrinter.option_some")
{
    CHECK(parseAndPrintAST("let x = Some 42") == "let x = Some 42");
}

TEST_CASE("Parser.FSharp.ASTPrinter.option_none")
{
    CHECK(parseAndPrintAST("let x = None") == "let x = None");
}

TEST_CASE("Parser.FSharp.ASTPrinter.result_ok")
{
    CHECK(parseAndPrintAST("let r = Ok 100") == "let r = Ok 100");
}

TEST_CASE("Parser.FSharp.ASTPrinter.result_error")
{
    CHECK(parseAndPrintAST("let r = Error 42") == "let r = Error 42");
}

TEST_CASE("Parser.FSharp.ASTPrinter.try_expr")
{
    CHECK(parseAndPrintAST("let f x = x?") == "let f x = x?");
}

TEST_CASE("Parser.FSharp.ASTPrinter.try_with")
{
    CHECK(parseAndPrintAST("let r = try getValue x with | Error e -> 0")
          == "let r = try (getValue x) with | Error e -> 0");
}

// =============================================================================
// Try-Finally Expression Tests
// =============================================================================

TEST_CASE("Parser.FSharp.try_finally_simple")
{
    auto ast = parse("let r = try 42 finally print 0");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(firstStmt);
    REQUIRE(letStmt != nullptr);
    CHECK(letStmt->name == "r");

    auto* tryFinallyExpr = dynamic_cast<endo::ast::TryFinallyExpr*>(letStmt->value.get());
    REQUIRE(tryFinallyExpr != nullptr);

    // Check body is an int literal
    auto* bodyLit = dynamic_cast<endo::ast::IntLiteralExpr*>(tryFinallyExpr->body.get());
    REQUIRE(bodyLit != nullptr);
    CHECK(bodyLit->value == 42);

    // Check finallyExpr is an application (print 0)
    auto* cleanupApp = dynamic_cast<endo::ast::ApplicationExpr*>(tryFinallyExpr->finallyExpr.get());
    REQUIRE(cleanupApp != nullptr);
}

TEST_CASE("Parser.FSharp.ASTPrinter.try_finally")
{
    CHECK(parseAndPrintAST("let r = try 42 finally print 0") == "let r = try 42 finally (print 0)");
}

// =============================================================================
// F# Let Rec Tests
// =============================================================================

TEST_CASE("Parser.FSharp.let_rec_basic")
{
    auto ast = parse("let rec countdown n = n");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    REQUIRE(firstStmt != nullptr);

    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(firstStmt);
    REQUIRE(letStmt != nullptr);

    CHECK(letStmt->name == "countdown");
    CHECK(letStmt->isMutable == false);
    CHECK(letStmt->isRecursive == true);
    CHECK(letStmt->isFunction() == true);
    REQUIRE(letStmt->parameters.size() == 1);
    CHECK(letStmt->parameters[0].name == "n");
}

TEST_CASE("Parser.FSharp.let_rec_multiple_params")
{
    auto ast = parse("let rec factorial n acc = n");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    REQUIRE(firstStmt != nullptr);

    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(firstStmt);
    REQUIRE(letStmt != nullptr);

    CHECK(letStmt->name == "factorial");
    CHECK(letStmt->isRecursive == true);
    REQUIRE(letStmt->parameters.size() == 2);
    CHECK(letStmt->parameters[0].name == "n");
    CHECK(letStmt->parameters[1].name == "acc");
}

TEST_CASE("Parser.FSharp.let_rec_no_params_error")
{
    // let rec x = 42 should fail (rec requires parameters)
    auto ast = parse("let rec x = 42");
    // The parser should fail and return nullptr (or a compound with no valid stmts)
    // Since parse() returns a statement, check if it's null or the inner is null
    if (ast != nullptr)
    {
        auto* firstStmt = getFirstStatement(ast.get());
        CHECK(firstStmt == nullptr);
    }
}

TEST_CASE("Parser.FSharp.let_mut_rec_error")
{
    // let mut rec should fail (mut and rec are mutually exclusive)
    auto ast = parse("let mut rec f x = x");
    if (ast != nullptr)
    {
        auto* firstStmt = getFirstStatement(ast.get());
        CHECK(firstStmt == nullptr);
    }
}

TEST_CASE("Parser.FSharp.ASTPrinter.let_rec")
{
    CHECK(parseAndPrintAST("let rec countdown n = n") == "let rec countdown n = n");
}

TEST_CASE("Parser.FSharp.ASTPrinter.let_rec_multiple_params")
{
    CHECK(parseAndPrintAST("let rec factorial n acc = n") == "let rec factorial n acc = n");
}

// =============================================================================
// Multi-line Expression Tests
// =============================================================================

TEST_CASE("Parser.FSharp.match_multiline_arms")
{
    auto ast = parse("let grade score = match score with\n"
                     "    | s when s >= 90 -> \"A\"\n"
                     "    | s when s >= 80 -> \"B\"\n"
                     "    | _ -> \"F\"");
    REQUIRE(ast != nullptr);
    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(getFirstStatement(ast.get()));
    REQUIRE(letStmt != nullptr);
    auto* matchExpr = dynamic_cast<endo::ast::MatchExpr*>(letStmt->value.get());
    REQUIRE(matchExpr != nullptr);
    REQUIRE(matchExpr->arms.size() == 3);
}

TEST_CASE("Parser.FSharp.match_multiline_followed_by_statement")
{
    // Multi-line match followed by another statement — ensures pushback works
    auto ast = parse("let grade score = match score with\n"
                     "    | s when s >= 90 -> \"A\"\n"
                     "    | _ -> \"F\"\n"
                     "let x = 42");
    REQUIRE(ast != nullptr);

    // Should parse as a compound statement with two let bindings
    auto* compound = dynamic_cast<endo::ast::CompoundStmt*>(ast.get());
    REQUIRE(compound != nullptr);
    REQUIRE(compound->statements.size() == 2);

    auto* letGrade = dynamic_cast<endo::ast::LetBindingStmt*>(compound->statements[0].get());
    REQUIRE(letGrade != nullptr);
    CHECK(letGrade->name == "grade");

    auto* letX = dynamic_cast<endo::ast::LetBindingStmt*>(compound->statements[1].get());
    REQUIRE(letX != nullptr);
    CHECK(letX->name == "x");
}

TEST_CASE("Parser.FSharp.match_singleline_still_works")
{
    // Regression guard: single-line match must still work
    auto ast = parse(R"(let r = match x with | 1 -> "one" | _ -> "other")");
    REQUIRE(ast != nullptr);
    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(getFirstStatement(ast.get()));
    REQUIRE(letStmt != nullptr);
    auto* matchExpr = dynamic_cast<endo::ast::MatchExpr*>(letStmt->value.get());
    REQUIRE(matchExpr != nullptr);
    REQUIRE(matchExpr->arms.size() == 2);
}

TEST_CASE("Parser.FSharp.if_then_else_multiline")
{
    auto ast = parse("let r = if true\n"
                     "    then 1\n"
                     "    else 2");
    REQUIRE(ast != nullptr);
    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(getFirstStatement(ast.get()));
    REQUIRE(letStmt != nullptr);
    auto* ifExpr = dynamic_cast<endo::ast::IfExpr*>(letStmt->value.get());
    REQUIRE(ifExpr != nullptr);
}

TEST_CASE("Parser.FSharp.elif_simple")
{
    // elif should produce nested IfExpr (same as else if)
    auto ast = parse("let r = if x > 0 then 1 elif x == 0 then 0 else -1");
    REQUIRE(ast != nullptr);
    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(getFirstStatement(ast.get()));
    REQUIRE(letStmt != nullptr);
    auto* ifExpr = dynamic_cast<endo::ast::IfExpr*>(letStmt->value.get());
    REQUIRE(ifExpr != nullptr);
    // The else branch should be a nested IfExpr
    auto* nestedIf = dynamic_cast<endo::ast::IfExpr*>(ifExpr->elseExpr.get());
    REQUIRE(nestedIf != nullptr);
    REQUIRE(nestedIf->elseExpr != nullptr);
}

TEST_CASE("Parser.FSharp.elif_multiline")
{
    auto ast = parse("let r =\n"
                     "    if x > 0 then\n"
                     "        1\n"
                     "    elif x == 0 then\n"
                     "        0\n"
                     "    else\n"
                     "        -1");
    REQUIRE(ast != nullptr);
    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(getFirstStatement(ast.get()));
    REQUIRE(letStmt != nullptr);
    auto* ifExpr = dynamic_cast<endo::ast::IfExpr*>(letStmt->value.get());
    REQUIRE(ifExpr != nullptr);
    auto* nestedIf = dynamic_cast<endo::ast::IfExpr*>(ifExpr->elseExpr.get());
    REQUIRE(nestedIf != nullptr);
}

TEST_CASE("Parser.FSharp.elif_chain")
{
    // Multiple elif branches
    auto ast = parse("let r = if x > 0 then 1 elif x == 0 then 0 elif x > -10 then -1 else -2");
    REQUIRE(ast != nullptr);
    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(getFirstStatement(ast.get()));
    REQUIRE(letStmt != nullptr);
    auto* ifExpr = dynamic_cast<endo::ast::IfExpr*>(letStmt->value.get());
    REQUIRE(ifExpr != nullptr);
    auto* elif1 = dynamic_cast<endo::ast::IfExpr*>(ifExpr->elseExpr.get());
    REQUIRE(elif1 != nullptr);
    auto* elif2 = dynamic_cast<endo::ast::IfExpr*>(elif1->elseExpr.get());
    REQUIRE(elif2 != nullptr);
    REQUIRE(elif2->elseExpr != nullptr);
}

TEST_CASE("Parser.FSharp.lambda_multiline_body")
{
    auto ast = parse("let f = fun x ->\n"
                     "    x");
    REQUIRE(ast != nullptr);
    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(getFirstStatement(ast.get()));
    REQUIRE(letStmt != nullptr);
    auto* lambda = dynamic_cast<endo::ast::LambdaExpr*>(letStmt->value.get());
    REQUIRE(lambda != nullptr);
    REQUIRE(lambda->parameters.size() == 1);
    CHECK(lambda->parameters[0].name == "x");
}

TEST_CASE("Parser.FSharp.let_in_multiline")
{
    auto ast = parse("let r = let x =\n"
                     "    5\n"
                     "    in\n"
                     "    x");
    REQUIRE(ast != nullptr);
    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(getFirstStatement(ast.get()));
    REQUIRE(letStmt != nullptr);
    auto* letIn = dynamic_cast<endo::ast::LetInExpr*>(letStmt->value.get());
    REQUIRE(letIn != nullptr);
    CHECK(letIn->name == "x");
}

TEST_CASE("Parser.FSharp.let_multiline_match_value")
{
    // Top-level let with multi-line match as value
    auto ast = parse("let grade score =\n"
                     "    match score with\n"
                     "    | s when s >= 90 -> \"A\"\n"
                     "    | _ -> \"F\"");
    REQUIRE(ast != nullptr);
    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(getFirstStatement(ast.get()));
    REQUIRE(letStmt != nullptr);
    CHECK(letStmt->name == "grade");
    auto* matchExpr = dynamic_cast<endo::ast::MatchExpr*>(letStmt->value.get());
    REQUIRE(matchExpr != nullptr);
    REQUIRE(matchExpr->arms.size() == 2);
}

// =============================================================================
// Numeric Base Literal Tests
// =============================================================================

TEST_CASE("Parser.FSharp.hex_literal")
{
    auto ast = parse("let x = 0xFF");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(firstStmt);
    REQUIRE(letStmt != nullptr);
    CHECK(letStmt->name == "x");

    auto* intLit = dynamic_cast<endo::ast::IntLiteralExpr*>(letStmt->value.get());
    REQUIRE(intLit != nullptr);
    CHECK(intLit->value == 255);
}

TEST_CASE("Parser.FSharp.octal_literal")
{
    auto ast = parse("let x = 0o755");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(firstStmt);
    REQUIRE(letStmt != nullptr);

    auto* intLit = dynamic_cast<endo::ast::IntLiteralExpr*>(letStmt->value.get());
    REQUIRE(intLit != nullptr);
    CHECK(intLit->value == 493);
}

TEST_CASE("Parser.FSharp.binary_literal")
{
    auto ast = parse("let x = 0b1010");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(firstStmt);
    REQUIRE(letStmt != nullptr);

    auto* intLit = dynamic_cast<endo::ast::IntLiteralExpr*>(letStmt->value.get());
    REQUIRE(intLit != nullptr);
    CHECK(intLit->value == 10);
}

TEST_CASE("Parser.FSharp.negative_hex_literal")
{
    auto ast = parse("let x = -0xFF");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(firstStmt);
    REQUIRE(letStmt != nullptr);

    auto* intLit = dynamic_cast<endo::ast::IntLiteralExpr*>(letStmt->value.get());
    REQUIRE(intLit != nullptr);
    CHECK(intLit->value == -255);
}

TEST_CASE("Parser.FSharp.scientific_notation")
{
    auto ast = parse("let x = 1e10");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(firstStmt);
    REQUIRE(letStmt != nullptr);

    auto* floatLit = dynamic_cast<endo::ast::FloatLiteralExpr*>(letStmt->value.get());
    REQUIRE(floatLit != nullptr);
    CHECK(floatLit->value == 1e10);
}

// =============================================================================
// F# Type Annotation Parser Tests
// =============================================================================

TEST_CASE("Parser.FSharp.TypeAnnotation.let_int")
{
    auto ast = parse("let x: int = 42");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(firstStmt);
    REQUIRE(letStmt != nullptr);

    CHECK(letStmt->name == "x");
    CHECK(letStmt->parameters.empty());
    REQUIRE(letStmt->returnType.has_value());
    CHECK(endo::toString(*letStmt->returnType) == "int");

    auto* intLit = dynamic_cast<endo::ast::IntLiteralExpr*>(letStmt->value.get());
    REQUIRE(intLit != nullptr);
    CHECK(intLit->value == 42);
}

TEST_CASE("Parser.FSharp.TypeAnnotation.function_params_and_return")
{
    auto ast = parse("let add (x: int) (y: int): int = x + y");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(firstStmt);
    REQUIRE(letStmt != nullptr);

    CHECK(letStmt->name == "add");
    REQUIRE(letStmt->parameters.size() == 2);

    CHECK(letStmt->parameters[0].name == "x");
    REQUIRE(letStmt->parameters[0].typeAnnotation.has_value());
    CHECK(endo::toString(*letStmt->parameters[0].typeAnnotation) == "int");

    CHECK(letStmt->parameters[1].name == "y");
    REQUIRE(letStmt->parameters[1].typeAnnotation.has_value());
    CHECK(endo::toString(*letStmt->parameters[1].typeAnnotation) == "int");

    REQUIRE(letStmt->returnType.has_value());
    CHECK(endo::toString(*letStmt->returnType) == "int");
}

TEST_CASE("Parser.FSharp.TypeAnnotation.lambda_param")
{
    auto ast = parse("let f = fun (x: int) -> x + 1");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(firstStmt);
    REQUIRE(letStmt != nullptr);

    auto* lambda = dynamic_cast<endo::ast::LambdaExpr*>(letStmt->value.get());
    REQUIRE(lambda != nullptr);

    REQUIRE(lambda->parameters.size() == 1);
    CHECK(lambda->parameters[0].name == "x");
    REQUIRE(lambda->parameters[0].typeAnnotation.has_value());
    CHECK(endo::toString(*lambda->parameters[0].typeAnnotation) == "int");
}

TEST_CASE("Parser.FSharp.TypeAnnotation.function_type_annotation")
{
    auto ast = parse("let f: int -> int = fun x -> x + 1");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(firstStmt);
    REQUIRE(letStmt != nullptr);

    CHECK(letStmt->name == "f");
    CHECK(letStmt->parameters.empty());
    REQUIRE(letStmt->returnType.has_value());
    CHECK(endo::toString(*letStmt->returnType) == "int -> int");
}

TEST_CASE("Parser.FSharp.TypeAnnotation.ASTPrinter.let_int")
{
    CHECK(parseAndPrintAST("let x: int = 42") == "let x: int = 42");
}

TEST_CASE("Parser.FSharp.TypeAnnotation.ASTPrinter.function_params_and_return")
{
    CHECK(parseAndPrintAST("let add (x: int) (y: int): int = x + y")
          == "let add (x: int) (y: int): int = (x + y)");
}

TEST_CASE("Parser.FSharp.TypeAnnotation.ASTPrinter.lambda_param")
{
    CHECK(parseAndPrintAST("let f = fun (x: int) -> x + 1") == "let f = fun (x: int) -> (x + 1)");
}

TEST_CASE("Parser.FSharp.TypeAnnotation.ASTPrinter.mixed_params")
{
    CHECK(parseAndPrintAST("let f (x: int) y = x + y") == "let f (x: int) y = (x + y)");
}

TEST_CASE("Parser.FSharp.TypeAnnotation.lambda_return_type")
{
    auto ast = parse("let f = fun (x: int) : int -> x + 1");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(firstStmt);
    REQUIRE(letStmt != nullptr);

    auto* lambda = dynamic_cast<endo::ast::LambdaExpr*>(letStmt->value.get());
    REQUIRE(lambda != nullptr);

    REQUIRE(lambda->parameters.size() == 1);
    CHECK(lambda->parameters[0].name == "x");
    REQUIRE(lambda->parameters[0].typeAnnotation.has_value());
    CHECK(endo::toString(*lambda->parameters[0].typeAnnotation) == "int");

    REQUIRE(lambda->returnType.has_value());
    CHECK(endo::toString(*lambda->returnType) == "int");
}

TEST_CASE("Parser.FSharp.TypeAnnotation.lambda_return_type_bool")
{
    auto ast = parse("let f = fun (x: int) : bool -> x > 0");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(firstStmt);
    REQUIRE(letStmt != nullptr);

    auto* lambda = dynamic_cast<endo::ast::LambdaExpr*>(letStmt->value.get());
    REQUIRE(lambda != nullptr);

    REQUIRE(lambda->returnType.has_value());
    CHECK(endo::toString(*lambda->returnType) == "bool");
}

TEST_CASE("Parser.FSharp.TypeAnnotation.lambda_return_type_option")
{
    auto ast = parse("let f = fun (x: int) : option<int> -> Some x");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(firstStmt);
    REQUIRE(letStmt != nullptr);

    auto* lambda = dynamic_cast<endo::ast::LambdaExpr*>(letStmt->value.get());
    REQUIRE(lambda != nullptr);

    REQUIRE(lambda->returnType.has_value());
    CHECK(endo::toString(*lambda->returnType) == "option<int>");
}

TEST_CASE("Parser.FSharp.TypeAnnotation.lambda_return_type_tuple")
{
    auto ast = parse("let f = fun (x: int) (y: str) : (int, str) -> (x, y)");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(firstStmt);
    REQUIRE(letStmt != nullptr);

    auto* lambda = dynamic_cast<endo::ast::LambdaExpr*>(letStmt->value.get());
    REQUIRE(lambda != nullptr);

    REQUIRE(lambda->parameters.size() == 2);
    REQUIRE(lambda->returnType.has_value());
    CHECK(endo::toString(*lambda->returnType) == "(int, string)");
}

TEST_CASE("Parser.FSharp.TypeAnnotation.ASTPrinter.lambda_return_int")
{
    CHECK(parseAndPrintAST("let f = fun (x: int) : int -> x + 1") == "let f = fun (x: int) : int -> (x + 1)");
}

TEST_CASE("Parser.FSharp.TypeAnnotation.ASTPrinter.lambda_return_no_type")
{
    // Backward compatibility: lambda without return type annotation
    CHECK(parseAndPrintAST("let f = fun x -> x + 1") == "let f = fun x -> (x + 1)");
}

TEST_CASE("Parser.FSharp.TypeAnnotation.let_list_int")
{
    auto ast = parse("let xs: list<int> = [1; 2; 3]");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(firstStmt);
    REQUIRE(letStmt != nullptr);

    CHECK(letStmt->name == "xs");
    REQUIRE(letStmt->returnType.has_value());
    CHECK(endo::toString(*letStmt->returnType) == "list<int>");
}

TEST_CASE("Parser.FSharp.TypeAnnotation.let_tuple_type")
{
    auto ast = parse("let p: (int, str) = (42, \"hi\")");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(firstStmt);
    REQUIRE(letStmt != nullptr);

    CHECK(letStmt->name == "p");
    REQUIRE(letStmt->returnType.has_value());
    CHECK(endo::toString(*letStmt->returnType) == "(int, string)");
}

// Note: option<list<int>> is not testable in parser tests because ">>" is lexed
// as a single token. This is covered by the ir-only .endo test instead.

// =============================================================================
// Record Types
// =============================================================================

TEST_CASE("Parser.FSharp.record_type_definition")
{
    CHECK(parseAndPrintAST("type Person = { name: str; age: int }")
          == "type Person = { name: string; age: int }");
}

TEST_CASE("Parser.FSharp.union_type_definition")
{
    CHECK(
        parseAndPrintAST("type Shape =\n    | Circle of float\n    | Rectangle of float * float\n    | Point")
        == "type Shape = | Circle of float | Rectangle of float * float | Point");
}

TEST_CASE("Parser.FSharp.union_type_single_variant")
{
    CHECK(parseAndPrintAST("type Wrapper =\n    | Wrap of int") == "type Wrapper = | Wrap of int");
}

TEST_CASE("Parser.FSharp.union_constructor_expression")
{
    CHECK(parseAndPrintAST("type Color =\n    | Red\n    | Green\n    | Blue\nlet c = Red")
          == "type Color = | Red | Green | Blue; let c = Red");
}

TEST_CASE("Parser.FSharp.record_literal")
{
    CHECK(parseAndPrintAST("let p = { name = 1; age = 30 }") == "let p = { name = 1; age = 30 }");
}

TEST_CASE("Parser.FSharp.record_field_access")
{
    CHECK(parseAndPrintAST("let x = p.name") == "let x = p.name");
}

TEST_CASE("Parser.FSharp.record_chained_field_access")
{
    CHECK(parseAndPrintAST("let x = emp.address.city") == "let x = emp.address.city");
}

TEST_CASE("Parser.FSharp.record_update")
{
    CHECK(parseAndPrintAST("let q = { p with age = 31 }") == "let q = { p with age = 31 }");
}

TEST_CASE("Parser.FSharp.block_expr_still_works")
{
    // Ensure block expression disambiguation still works
    CHECK(parseAndPrintAST("let r = { let x = 1; x + 2 }") == "let r = { let x = 1; (x + 2) }");
}

// =============================================================================
// Placeholder Lambda Sugar Tests (`_`)
// =============================================================================

TEST_CASE("Parser.FSharp.placeholder_field_access")
{
    // _.name → fun __x -> __x.name (bare postfix wrapping)
    CHECK(parseAndPrintAST("let f = _.name") == "let f = fun __x -> __x.name");
}

TEST_CASE("Parser.FSharp.placeholder_chained_field_access")
{
    // _.a.b → fun __x -> __x.a.b
    CHECK(parseAndPrintAST("let f = _.a.b") == "let f = fun __x -> __x.a.b");
}

TEST_CASE("Parser.FSharp.placeholder_parenthesized_add")
{
    // (_ + 1) → fun __x -> (__x + 1)
    CHECK(parseAndPrintAST("let f = (_ + 1)") == "let f = fun __x -> (__x + 1)");
}

TEST_CASE("Parser.FSharp.placeholder_parenthesized_mul")
{
    // (_ * 2) → fun __x -> (__x * 2)
    CHECK(parseAndPrintAST("let f = (_ * 2)") == "let f = fun __x -> (__x * 2)");
}

TEST_CASE("Parser.FSharp.placeholder_same_param_twice")
{
    // (_ + _) → fun __x -> (__x + __x)  (same parameter used twice)
    CHECK(parseAndPrintAST("let f = (_ + _)") == "let f = fun __x -> (__x + __x)");
}

TEST_CASE("Parser.FSharp.placeholder_field_comparison")
{
    // (_.name == "endo") → fun __x -> (__x.name == "endo")
    CHECK(parseAndPrintAST(R"(let f = (_.name == "endo"))") == R"(let f = fun __x -> (__x.name == "endo"))");
}

TEST_CASE("Parser.FSharp.placeholder_tuple")
{
    // (_.name, _.age) → fun __x -> (__x.name, __x.age)
    CHECK(parseAndPrintAST("let f = (_.name, _.age)") == "let f = fun __x -> (__x.name, __x.age)");
}

TEST_CASE("Parser.FSharp.placeholder_in_application")
{
    // sortBy _.cpu → application of sortBy with lambda
    CHECK(parseAndPrintAST("let f = sortBy _.cpu") == "let f = (sortBy fun __x -> __x.cpu)");
}

TEST_CASE("Parser.FSharp.placeholder_no_parens_no_postfix")
{
    // Bare _ without postfix or parens is just IdentifierExpr("__x") — no lambda wrapping
    CHECK(parseAndPrintAST("let f = _") == "let f = __x");
}

TEST_CASE("Parser.FSharp.placeholder_unparenthesized_add")
{
    // _ + 1 → fun __x -> __x + 1 (no parens needed)
    CHECK(parseAndPrintAST("let f = _ + 1") == "let f = fun __x -> (__x + 1)");
}

TEST_CASE("Parser.FSharp.placeholder_unparenthesized_comparison")
{
    // _ > 10 → fun __x -> __x > 10
    CHECK(parseAndPrintAST("let f = _ > 10") == "let f = fun __x -> (__x > 10)");
}

TEST_CASE("Parser.FSharp.placeholder_unparenthesized_complex")
{
    // _ * 2 + 1 → fun __x -> __x * 2 + 1
    CHECK(parseAndPrintAST("let f = _ * 2 + 1") == "let f = fun __x -> ((__x * 2) + 1)");
}

TEST_CASE("Parser.FSharp.placeholder_unparenthesized_string_eq")
{
    // _ == "hello" → fun __x -> __x == "hello"
    CHECK(parseAndPrintAST(R"(let f = _ == "hello")") == R"(let f = fun __x -> (__x == "hello"))");
}

TEST_CASE("Parser.FSharp.placeholder_unparenthesized_field_binop")
{
    // _.name > 10 → fun __x -> __x.name > 10 (deferred postfix wrapping)
    CHECK(parseAndPrintAST("let f = _.name > 10") == "let f = fun __x -> (__x.name > 10)");
}

TEST_CASE("Parser.FSharp.placeholder_unparenthesized_logical")
{
    // _ && true → fun __x -> __x && true
    CHECK(parseAndPrintAST("let f = _ && true") == "let f = fun __x -> (__x && true)");
}

// =============================================================================
// exec keyword tests
// =============================================================================

TEST_CASE("Parser.FSharp.exec_single_command")
{
    CHECK(parseAndPrintAST(R"(exec "/bin/echo" "hello")") == R"(exec "/bin/echo" "hello")");
}

TEST_CASE("Parser.FSharp.exec_piped_commands")
{
    CHECK(parseAndPrintAST(R"(exec "/bin/echo" "hello" | exec "/bin/cat")")
          == R"(exec "/bin/echo" "hello" | exec "/bin/cat")");
}

TEST_CASE("Parser.FSharp.exec_variable_program")
{
    CHECK(parseAndPrintAST(R"(let p = "/bin/echo"; exec p "hi")") == R"(let p = "/bin/echo"; exec p "hi")");
}

TEST_CASE("Parser.FSharp.exec_three_stage_pipeline")
{
    CHECK(parseAndPrintAST(R"(exec "/bin/echo" "hello" | exec "/bin/tr" "a-z" "A-Z" | exec "/bin/cat")")
          == R"(exec "/bin/echo" "hello" | exec "/bin/tr" "a-z" "A-Z" | exec "/bin/cat")");
}

TEST_CASE("Parser.FSharp.exec_in_parens")
{
    // Inside parentheses (e.g., match arms)
    CHECK(parseAndPrintAST(R"(let r = (exec "/bin/echo" "hello" | exec "/bin/cat"))")
          == R"(let r = (exec "/bin/echo" "hello" | exec "/bin/cat"))");
}

// =============================================================================
// Lazy Expression Tests
// =============================================================================

TEST_CASE("Parser.FSharp.lazy_int_literal")
{
    auto ast = parse("let x = lazy 42");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(firstStmt);
    REQUIRE(letStmt != nullptr);

    auto* lazyExpr = dynamic_cast<endo::ast::LazyExpr*>(letStmt->value.get());
    REQUIRE(lazyExpr != nullptr);
    CHECK(dynamic_cast<endo::ast::IntLiteralExpr*>(lazyExpr->body.get()) != nullptr);
}

TEST_CASE("Parser.FSharp.lazy_paren_expr")
{
    auto ast = parse("let x = lazy (1 + 2)");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(firstStmt);
    REQUIRE(letStmt != nullptr);

    auto* lazyExpr = dynamic_cast<endo::ast::LazyExpr*>(letStmt->value.get());
    REQUIRE(lazyExpr != nullptr);
    CHECK(dynamic_cast<endo::ast::ParenExpr*>(lazyExpr->body.get()) != nullptr);
}

TEST_CASE("Parser.FSharp.lazy_identifier")
{
    auto ast = parse("let x = lazy someVar");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(firstStmt);
    REQUIRE(letStmt != nullptr);

    auto* lazyExpr = dynamic_cast<endo::ast::LazyExpr*>(letStmt->value.get());
    REQUIRE(lazyExpr != nullptr);
    CHECK(dynamic_cast<endo::ast::IdentifierExpr*>(lazyExpr->body.get()) != nullptr);
}

TEST_CASE("Parser.FSharp.ASTPrinter.lazy_int")
{
    CHECK(parseAndPrintAST("let x = lazy 42") == "let x = lazy 42");
}

TEST_CASE("Parser.FSharp.ASTPrinter.lazy_paren")
{
    CHECK(parseAndPrintAST("let x = lazy (1 + 2)") == "let x = lazy ((1 + 2))");
}

TEST_CASE("Parser.FSharp.ASTPrinter.lazy_force")
{
    CHECK(parseAndPrintAST("let x = lazy 42; print (force x)") == "let x = lazy 42; (print ((force x)))");
}

TEST_CASE("Parser.FSharp.multiline_pipeline_known_binding")
{
    // Known F# variable followed by |> on the next line
    auto ast = parse("let x = 42\nx\n    |> println");
    REQUIRE(ast != nullptr);

    auto* compound = dynamic_cast<endo::ast::CompoundStmt*>(ast.get());
    REQUIRE(compound != nullptr);
    REQUIRE(compound->statements.size() == 2);

    // First statement: let x = 42
    auto* letStmt = dynamic_cast<endo::ast::LetBindingStmt*>(compound->statements[0].get());
    REQUIRE(letStmt != nullptr);
    CHECK(letStmt->name == "x");

    // Second statement: x |> println (parsed as pipeline despite newline before |>)
    auto* exprStmt = dynamic_cast<endo::ast::ExprStmt*>(compound->statements[1].get());
    REQUIRE(exprStmt != nullptr);
    auto* pipeline = dynamic_cast<endo::ast::PipelineExpr*>(exprStmt->expr.get());
    REQUIRE(pipeline != nullptr);
}

TEST_CASE("Parser.FSharp.multiline_pipeline_known_binding_multistep")
{
    // Known F# variable with multi-step multi-line pipeline
    auto ast = parse("let x = 42\nx\n    |> force\n    |> println");
    REQUIRE(ast != nullptr);

    auto* compound = dynamic_cast<endo::ast::CompoundStmt*>(ast.get());
    REQUIRE(compound != nullptr);
    REQUIRE(compound->statements.size() == 2);

    // Second statement should be a nested pipeline: (x |> force) |> println
    auto* exprStmt = dynamic_cast<endo::ast::ExprStmt*>(compound->statements[1].get());
    REQUIRE(exprStmt != nullptr);
    auto* outerPipeline = dynamic_cast<endo::ast::PipelineExpr*>(exprStmt->expr.get());
    REQUIRE(outerPipeline != nullptr);

    // The left side of the outer pipeline should itself be a pipeline (x |> force)
    auto* innerPipeline = dynamic_cast<endo::ast::PipelineExpr*>(outerPipeline->value.get());
    REQUIRE(innerPipeline != nullptr);
}

// =============================================================================
// Builtin Property as Bare Expression Tests
// =============================================================================

TEST_CASE("Parser.Property.bare_property_parsed_as_expr", "[parser][property]")
{
    // A registered builtin property name at statement level should produce
    // an ExprStmt(IdentifierExpr), not a ProgramCall (shell command).
    auto ast = parse("shell_prompt_preset");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    REQUIRE(firstStmt != nullptr);

    auto* exprStmt = dynamic_cast<endo::ast::ExprStmt*>(firstStmt);
    REQUIRE(exprStmt != nullptr);

    auto* identExpr = dynamic_cast<endo::ast::IdentifierExpr*>(exprStmt->expr.get());
    REQUIRE(identExpr != nullptr);
    CHECK(identExpr->name == "shell_prompt_preset");
}

TEST_CASE("Parser.Property.bare_property_not_program_call", "[parser][property]")
{
    // Verify the property does NOT parse as a ProgramCall (shell command).
    auto ast = parse("shell_ls_icons");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    REQUIRE(firstStmt != nullptr);

    auto* progCall = dynamic_cast<endo::ast::ProgramCall*>(firstStmt);
    CHECK(progCall == nullptr);

    auto* exprStmt = dynamic_cast<endo::ast::ExprStmt*>(firstStmt);
    CHECK(exprStmt != nullptr);
}

TEST_CASE("Parser.Property.assignment_via_left_arrow", "[parser][property]")
{
    // property <- value should produce a MutAssignStmt.
    auto ast = parse(R"(shell_prompt_preset <- "lambda-clean")");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    REQUIRE(firstStmt != nullptr);

    auto* mutAssign = dynamic_cast<endo::ast::MutAssignStmt*>(firstStmt);
    REQUIRE(mutAssign != nullptr);
    CHECK(mutAssign->name == "shell_prompt_preset");
}

TEST_CASE("Parser.Property.pipeline", "[parser][property]")
{
    // property |> function should produce an ExprStmt wrapping a PipelineExpr.
    auto ast = parse("shell_prompt_preset |> print");
    REQUIRE(ast != nullptr);

    auto* firstStmt = getFirstStatement(ast.get());
    REQUIRE(firstStmt != nullptr);

    auto* exprStmt = dynamic_cast<endo::ast::ExprStmt*>(firstStmt);
    REQUIRE(exprStmt != nullptr);

    auto* pipeline = dynamic_cast<endo::ast::PipelineExpr*>(exprStmt->expr.get());
    REQUIRE(pipeline != nullptr);
}

TEST_CASE("Parser.Property.interactive_display_number", "[parser][property]")
{
    // In interactive mode (auto-display), a bare numeric property should display its value.
    // shell_prompt_duration_threshold is a Number property; the test runtime's no-op getter
    // returns 0, which display_result renders as "0".
    auto result = executeInteractive("shell_prompt_duration_threshold");
    REQUIRE(result.has_value());
    CHECK(result->output == "0\n");
}

TEST_CASE("Parser.Property.print_property_via_pipeline", "[parser][property]")
{
    // Verify property value can flow through a pipeline.
    CHECK(executesSuccessfully("shell_prompt_duration_threshold |> print"));
}
