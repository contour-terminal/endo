// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_test_macros.hpp>

#include "TestHelper.hpp"

using namespace endo::test;

// =============================================================================
// F# Let Binding IR Generation Tests
// =============================================================================

TEST_CASE("IRGenerator.FSharp.let_simple_int")
{
    // Simple integer binding
    REQUIRE(generatesIRSuccessfully("let x = 42"));
}

TEST_CASE("IRGenerator.FSharp.let_bool_true")
{
    REQUIRE(generatesIRSuccessfully("let flag = true"));
}

TEST_CASE("IRGenerator.FSharp.let_bool_false")
{
    REQUIRE(generatesIRSuccessfully("let flag = false"));
}

TEST_CASE("IRGenerator.FSharp.let_multiple_bindings")
{
    // Multiple let bindings in sequence
    REQUIRE(generatesIRSuccessfully("let x = 1; let y = 2; let z = 3"));
}

// =============================================================================
// F# Identifier Expression IR Generation Tests
// =============================================================================

TEST_CASE("IRGenerator.FSharp.identifier_simple")
{
    // Reference a previously bound identifier
    REQUIRE(generatesIRSuccessfully("let x = 42; let y = x"));
}

TEST_CASE("IRGenerator.FSharp.identifier_chain")
{
    // Chain of references
    REQUIRE(generatesIRSuccessfully("let a = 1; let b = a; let c = b"));
}

// =============================================================================
// F# Binary Expression IR Generation Tests
// =============================================================================

TEST_CASE("IRGenerator.FSharp.binary_add")
{
    REQUIRE(generatesIRSuccessfully("let x = 1 + 2"));
}

TEST_CASE("IRGenerator.FSharp.binary_sub")
{
    REQUIRE(generatesIRSuccessfully("let x = 5 - 3"));
}

TEST_CASE("IRGenerator.FSharp.binary_mul")
{
    REQUIRE(generatesIRSuccessfully("let x = 4 * 5"));
}

TEST_CASE("IRGenerator.FSharp.binary_div")
{
    REQUIRE(generatesIRSuccessfully("let x = 10 / 2"));
}

TEST_CASE("IRGenerator.FSharp.binary_mod")
{
    REQUIRE(generatesIRSuccessfully("let x = 10 % 3"));
}

TEST_CASE("IRGenerator.FSharp.binary_pow")
{
    REQUIRE(generatesIRSuccessfully("let x = 2 ** 10"));
}

TEST_CASE("IRGenerator.FSharp.binary_complex_arithmetic")
{
    // More complex expression: 1 + 2 * 3 (should be 7 due to precedence)
    REQUIRE(generatesIRSuccessfully("let x = 1 + 2 * 3"));
}

TEST_CASE("IRGenerator.FSharp.binary_with_identifiers")
{
    // Arithmetic using previously bound identifiers
    REQUIRE(generatesIRSuccessfully("let a = 10; let b = 5; let c = a + b"));
}

TEST_CASE("IRGenerator.FSharp.binary_comparison_eq")
{
    REQUIRE(generatesIRSuccessfully("let x = 5 == 5"));
}

TEST_CASE("IRGenerator.FSharp.binary_comparison_ne")
{
    REQUIRE(generatesIRSuccessfully("let x = 5 != 3"));
}

TEST_CASE("IRGenerator.FSharp.binary_comparison_lt")
{
    REQUIRE(generatesIRSuccessfully("let x = 3 < 5"));
}

TEST_CASE("IRGenerator.FSharp.binary_comparison_le")
{
    REQUIRE(generatesIRSuccessfully("let x = 3 <= 5"));
}

TEST_CASE("IRGenerator.FSharp.binary_comparison_gt")
{
    REQUIRE(generatesIRSuccessfully("let x = 5 > 3"));
}

TEST_CASE("IRGenerator.FSharp.binary_comparison_ge")
{
    REQUIRE(generatesIRSuccessfully("let x = 5 >= 3"));
}

TEST_CASE("IRGenerator.FSharp.binary_logical_and")
{
    REQUIRE(generatesIRSuccessfully("let x = true && false"));
}

TEST_CASE("IRGenerator.FSharp.binary_logical_or")
{
    REQUIRE(generatesIRSuccessfully("let x = true || false"));
}

// =============================================================================
// F# Unary Expression IR Generation Tests
// =============================================================================

TEST_CASE("IRGenerator.FSharp.unary_neg")
{
    REQUIRE(generatesIRSuccessfully("let x = -42"));
}

TEST_CASE("IRGenerator.FSharp.unary_not")
{
    REQUIRE(generatesIRSuccessfully("let x = !true"));
}

TEST_CASE("IRGenerator.FSharp.unary_neg_expr")
{
    REQUIRE(generatesIRSuccessfully("let a = 5; let x = -a"));
}

// =============================================================================
// F# Parenthesized Expression IR Generation Tests
// =============================================================================

TEST_CASE("IRGenerator.FSharp.paren_simple")
{
    REQUIRE(generatesIRSuccessfully("let x = (1 + 2)"));
}

TEST_CASE("IRGenerator.FSharp.paren_precedence")
{
    // (1 + 2) * 3 = 9 vs 1 + 2 * 3 = 7
    REQUIRE(generatesIRSuccessfully("let x = (1 + 2) * 3"));
}

// =============================================================================
// Combined Expression Tests
// =============================================================================

TEST_CASE("IRGenerator.FSharp.combined_arithmetic_and_comparison")
{
    REQUIRE(generatesIRSuccessfully("let a = 5; let b = 3; let c = a + b; let result = c > 7"));
}

TEST_CASE("IRGenerator.FSharp.combined_complex")
{
    REQUIRE(generatesIRSuccessfully(
        "let x = 10; let y = 20; let sum = x + y; let product = x * y; let check = sum < product"));
}

// =============================================================================
// F# Function Definition Tests
// =============================================================================

TEST_CASE("IRGenerator.FSharp.function_def_simple")
{
    // Simple single-parameter function
    REQUIRE(generatesIRSuccessfully("let double x = x * 2"));
}

TEST_CASE("IRGenerator.FSharp.function_def_two_params")
{
    // Two-parameter function
    REQUIRE(generatesIRSuccessfully("let add x y = x + y"));
}

TEST_CASE("IRGenerator.FSharp.function_def_three_params")
{
    // Three-parameter function
    REQUIRE(generatesIRSuccessfully("let add3 x y z = x + y + z"));
}

TEST_CASE("IRGenerator.FSharp.function_def_with_parens")
{
    // Function with parenthesized body
    REQUIRE(generatesIRSuccessfully("let compute x y = (x + y) * 2"));
}

// =============================================================================
// F# Function Application Tests
// =============================================================================

TEST_CASE("IRGenerator.FSharp.function_call_single_arg")
{
    // Define and call single-arg function
    REQUIRE(generatesIRSuccessfully("let double x = x * 2; let result = double 5"));
}

TEST_CASE("IRGenerator.FSharp.function_call_two_args")
{
    // Define and call two-arg function
    REQUIRE(generatesIRSuccessfully("let add x y = x + y; let result = add 3 4"));
}

TEST_CASE("IRGenerator.FSharp.function_call_nested")
{
    // Call function with result of another expression
    REQUIRE(generatesIRSuccessfully("let double x = x * 2; let a = 5; let result = double a"));
}

TEST_CASE("IRGenerator.FSharp.function_call_chained")
{
    // Chain function calls
    REQUIRE(generatesIRSuccessfully("let double x = x * 2; let r1 = double 3; let r2 = double r1"));
}

TEST_CASE("IRGenerator.FSharp.function_with_complex_body")
{
    // Function with complex expression body
    REQUIRE(generatesIRSuccessfully("let compute x y = x * x + y * y; let r = compute 3 4"));
}

// =============================================================================
// F# Pipeline Tests
// =============================================================================

TEST_CASE("IRGenerator.FSharp.pipeline_simple")
{
    // Simple pipeline: value |> function
    REQUIRE(generatesIRSuccessfully("let double x = x * 2; let result = 5 |> double"));
}

TEST_CASE("IRGenerator.FSharp.pipeline_with_binding")
{
    // Pipeline with bound value
    REQUIRE(generatesIRSuccessfully("let double x = x * 2; let a = 10; let result = a |> double"));
}

TEST_CASE("IRGenerator.FSharp.pipeline_chain")
{
    // Chained pipelines: value |> f |> g
    REQUIRE(
        generatesIRSuccessfully("let double x = x * 2; let inc x = x + 1; let result = 5 |> double |> inc"));
}

TEST_CASE("IRGenerator.FSharp.pipeline_with_expression")
{
    // Pipeline with expression on left side
    REQUIRE(generatesIRSuccessfully("let double x = x * 2; let result = (3 + 2) |> double"));
}

// =============================================================================
// F# Lambda Expression Tests
// =============================================================================

TEST_CASE("IRGenerator.FSharp.lambda_simple")
{
    // Simple lambda assigned to a variable
    REQUIRE(generatesIRSuccessfully("let f = fun x -> x * 2"));
}

TEST_CASE("IRGenerator.FSharp.lambda_two_params")
{
    // Lambda with two parameters
    REQUIRE(generatesIRSuccessfully("let add = fun x y -> x + y"));
}

TEST_CASE("IRGenerator.FSharp.lambda_application_direct")
{
    // Lambda applied directly: (fun x -> x * 2) 5
    REQUIRE(generatesIRSuccessfully("let result = (fun x -> x * 2) 5"));
}

TEST_CASE("IRGenerator.FSharp.lambda_application_two_args")
{
    // Lambda with two args applied directly
    REQUIRE(generatesIRSuccessfully("let result = (fun x y -> x + y) 3 4"));
}

TEST_CASE("IRGenerator.FSharp.lambda_in_pipeline")
{
    // Lambda in pipeline: 5 |> (fun x -> x * 2)
    REQUIRE(generatesIRSuccessfully("let result = 5 |> (fun x -> x * 2)"));
}

TEST_CASE("IRGenerator.FSharp.lambda_pipeline_chain")
{
    // Chained lambdas in pipeline
    REQUIRE(generatesIRSuccessfully("let result = 5 |> (fun x -> x * 2) |> (fun x -> x + 1)"));
}

TEST_CASE("IRGenerator.FSharp.lambda_with_named_function")
{
    // Mix lambda and named function in pipeline
    REQUIRE(generatesIRSuccessfully("let double x = x * 2; let result = 5 |> double |> (fun x -> x + 1)"));
}

TEST_CASE("IRGenerator.FSharp.lambda_stored_and_called")
{
    // Store lambda in variable, then call it
    REQUIRE(generatesIRSuccessfully("let f = fun x -> x * 2; let result = f 5"));
}

TEST_CASE("IRGenerator.FSharp.lambda_stored_in_pipeline")
{
    // Store lambda in variable, use in pipeline
    REQUIRE(generatesIRSuccessfully("let double = fun x -> x * 2; let result = 5 |> double"));
}

TEST_CASE("IRGenerator.FSharp.lambda_with_complex_body")
{
    // Lambda with complex expression body
    REQUIRE(generatesIRSuccessfully("let result = (fun x -> x * x + x) 5"));
}
