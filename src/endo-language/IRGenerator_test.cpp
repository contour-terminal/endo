// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_test_macros.hpp>

#include "AST.hpp"
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

// =============================================================================
// F# Match Expression IR Generation Tests
// =============================================================================

TEST_CASE("IRGenerator.FSharp.match_literal_int")
{
    // Match with integer literal patterns
    REQUIRE(generatesIRSuccessfully("let r = match 0 with | 0 -> 42 | _ -> 99"));
}

TEST_CASE("IRGenerator.FSharp.match_literal_multiple")
{
    // Match with multiple integer literals
    REQUIRE(generatesIRSuccessfully("let r = match 1 with | 0 -> 0 | 1 -> 10 | 2 -> 20 | _ -> 99"));
}

TEST_CASE("IRGenerator.FSharp.match_wildcard")
{
    // Wildcard pattern always matches
    REQUIRE(generatesIRSuccessfully("let r = match 42 with | _ -> 100"));
}

TEST_CASE("IRGenerator.FSharp.match_variable_binding")
{
    // Variable pattern binds and uses the matched value
    REQUIRE(generatesIRSuccessfully("let r = match 5 with | n -> n * 2"));
}

TEST_CASE("IRGenerator.FSharp.match_with_guard")
{
    // Match with when guard
    REQUIRE(generatesIRSuccessfully("let r = match 10 with | n when n > 0 -> 1 | _ -> 0"));
}

TEST_CASE("IRGenerator.FSharp.match_guard_negative")
{
    // Guard with negative check
    REQUIRE(
        generatesIRSuccessfully("let r = match -5 with | n when n < 0 -> 1 | n when n > 0 -> 2 | _ -> 0"));
}

TEST_CASE("IRGenerator.FSharp.match_variable_in_expression")
{
    // Use bound variable in body expression
    REQUIRE(generatesIRSuccessfully("let r = match 7 with | x -> x + x + x"));
}

TEST_CASE("IRGenerator.FSharp.match_with_function")
{
    // Match result used with a function
    REQUIRE(generatesIRSuccessfully("let double x = x * 2; let r = match 5 with | n -> double n"));
}

TEST_CASE("IRGenerator.FSharp.match_in_let_binding")
{
    // Match as part of let binding chain
    REQUIRE(generatesIRSuccessfully("let x = 10; let r = match x with | 0 -> 0 | n -> n * n"));
}

TEST_CASE("IRGenerator.FSharp.match_bool_literals")
{
    // Match with boolean literal patterns
    REQUIRE(generatesIRSuccessfully("let r = match true with | true -> 1 | false -> 0"));
}

// =============================================================================
// F# Option Expression IR Generation Tests
// =============================================================================

TEST_CASE("IRGenerator.FSharp.option_some")
{
    // Create Some value
    REQUIRE(generatesIRSuccessfully("let x = Some 42"));
}

TEST_CASE("IRGenerator.FSharp.option_none")
{
    // Create None value
    REQUIRE(generatesIRSuccessfully("let x = None"));
}

TEST_CASE("IRGenerator.FSharp.option_some_with_expression")
{
    // Some with an expression
    REQUIRE(generatesIRSuccessfully("let a = 5; let x = Some (a + 10)"));
}

// NOTE: Matching on Option/Result constructors requires runtime type info support
// This is deferred to a future phase when CoreVM supports proper sum types
// TEST_CASE("IRGenerator.FSharp.option_match")
// {
//     // Match on Option value
//     REQUIRE(generatesIRSuccessfully("let x = Some 42; let r = match x with | Some n -> n | None -> 0"));
// }

// =============================================================================
// F# Result Expression IR Generation Tests
// =============================================================================

TEST_CASE("IRGenerator.FSharp.result_ok")
{
    // Create Ok value
    REQUIRE(generatesIRSuccessfully("let r = Ok 100"));
}

TEST_CASE("IRGenerator.FSharp.result_error")
{
    // Create Error value
    REQUIRE(generatesIRSuccessfully("let r = Error 42"));
}

TEST_CASE("IRGenerator.FSharp.result_ok_with_expression")
{
    // Ok with an expression
    REQUIRE(generatesIRSuccessfully("let a = 10; let r = Ok (a * 2)"));
}

// NOTE: Matching on Option/Result constructors requires runtime type info support
// This is deferred to a future phase when CoreVM supports proper sum types
// TEST_CASE("IRGenerator.FSharp.result_match")
// {
//     // Match on Result value
//     REQUIRE(generatesIRSuccessfully("let r = Ok 42; let x = match r with | Ok n -> n | Error e -> 0"));
// }

// =============================================================================
// F# Try Expression (? operator) IR Generation Tests
// =============================================================================

TEST_CASE("IRGenerator.FSharp.try_expr_in_function")
{
    // Try expression inside a function (required for error propagation)
    // The function returns an Option (due to opt?), so function context is set up
    REQUIRE(generatesIRSuccessfully("let unwrap opt = opt?; let r = unwrap (Some 42)"));
}

TEST_CASE("IRGenerator.FSharp.try_expr_chained")
{
    // Test step by step
    REQUIRE(generatesIRSuccessfully("let f x = x??"));                     // Just function definition
    REQUIRE(generatesIRSuccessfully("let f x = x??; let y = 5"));          // With another statement
    REQUIRE(generatesIRSuccessfully("let f x = x??; let r = f 5"));        // With function call (simple)
    REQUIRE(generatesIRSuccessfully("let f x = x??; let r = f (Some 5)")); // With Option arg
    REQUIRE(generatesIRSuccessfully("let x = Some (Some 5)"));             // Nested Some by itself
    REQUIRE(
        generatesIRSuccessfully("let nested = Some (Some 5); let f x = x; let r = f nested")); // Via variable

    // Full chained try expressions using variable binding (workaround for parser limitation)
    REQUIRE(generatesIRSuccessfully(
        "let nested = Some (Some 5); let unwrap2 opt = opt??; let r = unwrap2 nested"));
}

// =============================================================================
// F# Option/Result - Additional Tests (after basic tests above)
// =============================================================================

TEST_CASE("IRGenerator.FSharp.option_some_bool")
{
    // Some with boolean
    REQUIRE(generatesIRSuccessfully("let x = Some true"));
}

TEST_CASE("IRGenerator.FSharp.option_nested")
{
    // Nested Option (Some (Some x))
    REQUIRE(generatesIRSuccessfully("let x = Some (Some 42)"));
}

TEST_CASE("IRGenerator.FSharp.option_chain")
{
    // Multiple option bindings
    REQUIRE(generatesIRSuccessfully("let a = Some 1; let b = Some 2; let c = None"));
}

TEST_CASE("IRGenerator.FSharp.result_error_value")
{
    // Error with identifier
    REQUIRE(generatesIRSuccessfully("let code = 404; let x = Error code"));
}

TEST_CASE("IRGenerator.FSharp.result_chain")
{
    // Multiple result bindings
    REQUIRE(generatesIRSuccessfully("let a = Ok 1; let b = Error 2; let c = Ok 3"));
}

TEST_CASE("IRGenerator.FSharp.result_nested_in_option")
{
    // Option containing Result
    REQUIRE(generatesIRSuccessfully("let x = Some (Ok 42)"));
}

// =============================================================================
// F# Match on Option/Result IR Generation Tests
// =============================================================================

TEST_CASE("IRGenerator.FSharp.match_option_some_none")
{
    // Match on Option with Some and None patterns (single-line due to newline parsing limitation)
    REQUIRE(
        generatesIRSuccessfully("let opt = Some 42; let result = match opt with | Some x -> x | None -> 0"));
}

TEST_CASE("IRGenerator.FSharp.match_option_none_pattern")
{
    // Match with None value (single-line format)
    REQUIRE(
        generatesIRSuccessfully("let opt = None; let result = match opt with | Some x -> x | None -> -1"));
}

TEST_CASE("IRGenerator.FSharp.match_result_ok_error")
{
    // Match on Result with Ok and Error patterns (single-line format)
    REQUIRE(
        generatesIRSuccessfully("let res = Ok 100; let value = match res with | Ok n -> n | Error e -> 0"));
}

TEST_CASE("IRGenerator.FSharp.match_result_error_pattern")
{
    // Match with Error value (single-line format)
    REQUIRE(generatesIRSuccessfully(
        "let res = Error 404; let value = match res with | Ok n -> n | Error e -> e"));
}

// =============================================================================
// F# Object Reference Counting IR Generation Tests
// =============================================================================

TEST_CASE("IRGenerator.FSharp.option_scope_tracking")
{
    // Option variable should be tracked for scope-based release
    REQUIRE(generatesIRSuccessfully("let x = Some 42; let y = x"));
}

TEST_CASE("IRGenerator.FSharp.result_scope_tracking")
{
    // Result variable should be tracked for scope-based release
    REQUIRE(generatesIRSuccessfully("let x = Ok 100; let y = x"));
}

TEST_CASE("IRGenerator.FSharp.nested_scope_option")
{
    // Option in nested scope - just test that we can match and return the bound value
    REQUIRE(generatesIRSuccessfully(
        "let outer = Some 1; let result = match outer with | Some x -> x | None -> 0"));
}

// =============================================================================
// F# Try-With Expression IR Generation Tests
// =============================================================================

TEST_CASE("IRGenerator.FSharp.try_with_simple")
{
    // Simple try-with expression - IR generation
    REQUIRE(generatesIRSuccessfully("let getValue x = Ok x; let r = try getValue 42 with | Error e -> 0"));
}

// NOTE: Multiple handlers not yet implemented - only first handler is used
// TEST_CASE("IRGenerator.FSharp.try_with_multiple_handlers")
// {
//     // Try-with with multiple error handlers
//     REQUIRE(generatesIRSuccessfully(
//         "let getValue x = Ok x; let r = try getValue 42 with | Error 1 -> 10 | Error _ -> 0"));
// }

// =============================================================================
// F# Option/Result Execution Tests
// =============================================================================
// These tests verify that the generated IR actually executes correctly,
// not just that it compiles. Note: The main handler always returns 0
// (shell success convention), so we test that execution completes without error.

TEST_CASE("IRGenerator.FSharp.exec_match_option_some")
{
    REQUIRE(executesSuccessfully("let opt = Some 42; let r = match opt with | Some x -> x | None -> 0"));
}

TEST_CASE("IRGenerator.FSharp.exec_match_option_none")
{
    REQUIRE(executesSuccessfully("let opt = None; let r = match opt with | Some x -> x | None -> 0"));
}

TEST_CASE("IRGenerator.FSharp.exec_match_result_ok")
{
    REQUIRE(executesSuccessfully("let res = Ok 100; let r = match res with | Ok n -> n | Error e -> 0"));
}

TEST_CASE("IRGenerator.FSharp.exec_match_result_error")
{
    REQUIRE(executesSuccessfully("let res = Error 404; let r = match res with | Ok n -> n | Error e -> 0"));
}

TEST_CASE("IRGenerator.FSharp.exec_simple_arithmetic")
{
    // Verify basic arithmetic executes without error
    REQUIRE(executesSuccessfully("let x = 2 + 3"));
}

TEST_CASE("IRGenerator.FSharp.exec_option_construction")
{
    // Verify Option construction executes without error
    REQUIRE(executesSuccessfully("let a = Some 42; let b = None"));
}

TEST_CASE("IRGenerator.FSharp.exec_result_construction")
{
    // Verify Result construction executes without error
    REQUIRE(executesSuccessfully("let a = Ok 100; let b = Error 404"));
}

TEST_CASE("IRGenerator.FSharp.exec_nested_option")
{
    // Nested Option - tests ORELEASE at scope exit
    REQUIRE(executesSuccessfully("let x = Some (Some 42)"));
}

TEST_CASE("IRGenerator.FSharp.exec_option_in_result")
{
    // Option inside Result - tests ORELEASE at scope exit
    REQUIRE(executesSuccessfully("let x = Ok (Some 42)"));
}

// =============================================================================
// F# Try Expression (? operator) Execution Tests
// =============================================================================

TEST_CASE("IRGenerator.FSharp.exec_try_unwrap_some")
{
    // ? operator unwraps Some value - function returns unwrapped value
    REQUIRE(executesSuccessfully("let unwrap opt = opt?; let r = unwrap (Some 42)"));
}

TEST_CASE("IRGenerator.FSharp.exec_try_propagate_none")
{
    // ? operator on None propagates the None - function early returns
    REQUIRE(executesSuccessfully("let unwrap opt = opt?; let r = unwrap None"));
}

TEST_CASE("IRGenerator.FSharp.exec_try_chained")
{
    // Chained ? operators for nested Option
    REQUIRE(
        executesSuccessfully("let nested = Some (Some 5); let unwrap2 opt = opt??; let r = unwrap2 nested"));
}

TEST_CASE("IRGenerator.FSharp.exec_try_with_result_ok")
{
    // ? operator unwraps Ok value
    REQUIRE(executesSuccessfully("let unwrap res = res?; let r = unwrap (Ok 100)"));
}

TEST_CASE("IRGenerator.FSharp.exec_try_with_result_error")
{
    // ? operator on Error propagates the Error
    REQUIRE(executesSuccessfully("let unwrap res = res?; let r = unwrap (Error 404)"));
}

TEST_CASE("IRGenerator.FSharp.exec_try_with_function_call")
{
    // Regression test: ? operator on function call result
    // This tests the fix for use-after-free when codegen(operand) pushes a new FSharpFunctionContext
    REQUIRE(executesSuccessfully("let inc x = Some (x + 1); let f x = (inc x)?; let r = f 5"));
}

TEST_CASE("IRGenerator.FSharp.exec_try_nested_function_calls")
{
    // Regression test: nested function calls with ? operator
    // Stress test for FSharpFunctionContext stack stability
    // Each function call pushes a context, verifying no use-after-free with multiple context pushes
    REQUIRE(executesSuccessfully("let inc x = Some (x + 1); "
                                 "let f x = (inc x)?; "
                                 "let r = f 5"));
}

TEST_CASE("IRGenerator.FSharp.exec_try_multiple_unwraps_same_function")
{
    // Multiple ? operators in the same function, each on a function call
    // This thoroughly tests the context pointer stability across multiple codegen calls
    REQUIRE(executesSuccessfully("let wrap x = Some x; "
                                 "let f x = (wrap (wrap x)?)?; "
                                 "let r = f 42"));
}

// =============================================================================
// F# Try-With Expression Execution Tests
// =============================================================================

TEST_CASE("IRGenerator.FSharp.exec_trywith_ok_passthrough")
{
    // try-with with Ok value - should extract inner value
    REQUIRE(executesSuccessfully("let getValue x = Ok x; let r = try getValue 42 with | Error e -> 0"));
}

TEST_CASE("IRGenerator.FSharp.exec_trywith_error_handled")
{
    // try-with catching Error - should run handler
    REQUIRE(executesSuccessfully("let getErr x = Error x; let r = try getErr 42 with | Error e -> e"));
}

TEST_CASE("IRGenerator.FSharp.exec_trywith_some_passthrough")
{
    // try-with with Some value - should extract inner value
    REQUIRE(executesSuccessfully("let getOpt x = Some x; let r = try getOpt 42 with | None -> 0"));
}

TEST_CASE("IRGenerator.FSharp.exec_trywith_none_handled")
{
    // try-with catching None - should run handler
    REQUIRE(executesSuccessfully("let getNone x = None; let r = try getNone 42 with | None -> 99"));
}

TEST_CASE("IRGenerator.FSharp.exec_trywith_multiple_handlers_first")
{
    // Multiple handlers - first pattern matches
    CHECK(
        executeSourceAndGetOutput("let getErr x = Error x; "
                                  "let r = try getErr 1 with | Error 1 -> 10 | Error 2 -> 20 | Error _ -> 0; "
                                  "print r")
        == "10");
}

TEST_CASE("IRGenerator.FSharp.exec_trywith_multiple_handlers_second")
{
    // Multiple handlers - second pattern matches
    CHECK(
        executeSourceAndGetOutput("let getErr x = Error x; "
                                  "let r = try getErr 2 with | Error 1 -> 10 | Error 2 -> 20 | Error _ -> 0; "
                                  "print r")
        == "20");
}

TEST_CASE("IRGenerator.FSharp.exec_trywith_multiple_handlers_fallback")
{
    // Multiple handlers - fallback pattern matches
    CHECK(executeSourceAndGetOutput(
              "let getErr x = Error x; "
              "let r = try getErr 99 with | Error 1 -> 10 | Error 2 -> 20 | Error _ -> 0; "
              "print r")
          == "0");
}

TEST_CASE("IRGenerator.FSharp.exec_trywith_ok_not_caught")
{
    // Ok value should not trigger any error handler
    CHECK(executeSourceAndGetOutput("let getValue x = Ok x; "
                                    "let r = try getValue 42 with | Error 1 -> 10 | Error _ -> 0; "
                                    "print r")
          == "42");
}

TEST_CASE("IRGenerator.FSharp.exec_match_constructor_literal_first")
{
    // Match expression with constructor and literal payload - first pattern
    CHECK(executeSourceAndGetOutput("let getErr x = Error x; "
                                    "let r = match getErr 1 with | Error 1 -> 10 | Error 2 -> 20 | _ -> 0; "
                                    "print r")
          == "10");
}

TEST_CASE("IRGenerator.FSharp.exec_match_constructor_literal_second")
{
    // Match expression with constructor and literal payload - second pattern
    CHECK(executeSourceAndGetOutput("let getErr x = Error x; "
                                    "let r = match getErr 2 with | Error 1 -> 10 | Error 2 -> 20 | _ -> 0; "
                                    "print r")
          == "20");
}

TEST_CASE("IRGenerator.FSharp.exec_match_constructor_literal_fallback")
{
    // Match expression with constructor and literal payload - fallback
    CHECK(executeSourceAndGetOutput("let getErr x = Error x; "
                                    "let r = match getErr 99 with | Error 1 -> 10 | Error 2 -> 20 | _ -> 0; "
                                    "print r")
          == "0");
}

// =============================================================================
// F# Guard Expression Tests (try-with and match)
// =============================================================================

TEST_CASE("IRGenerator.FSharp.exec_trywith_guard_gt_pass")
{
    // Guard with > that passes
    CHECK(executeSourceAndGetOutput("let getErr x = Error x; "
                                    "let r = try getErr 5 with | Error e when e > 3 -> 100 | Error _ -> 0; "
                                    "print r")
          == "100");
}

TEST_CASE("IRGenerator.FSharp.exec_trywith_guard_gt_fail")
{
    // Guard with > that fails - falls through to next handler
    CHECK(executeSourceAndGetOutput("let getErr x = Error x; "
                                    "let r = try getErr 2 with | Error e when e > 3 -> 100 | Error _ -> 0; "
                                    "print r")
          == "0");
}

TEST_CASE("IRGenerator.FSharp.exec_trywith_guard_lt")
{
    // Guard with <
    CHECK(executeSourceAndGetOutput("let getErr x = Error x; "
                                    "let r = try getErr 2 with | Error e when e < 5 -> 100 | Error _ -> 0; "
                                    "print r")
          == "100");
}

TEST_CASE("IRGenerator.FSharp.exec_trywith_guard_ge")
{
    // Guard with >=
    CHECK(executeSourceAndGetOutput("let getErr x = Error x; "
                                    "let r = try getErr 5 with | Error e when e >= 5 -> 100 | Error _ -> 0; "
                                    "print r")
          == "100");
}

TEST_CASE("IRGenerator.FSharp.exec_trywith_guard_le")
{
    // Guard with <=
    CHECK(executeSourceAndGetOutput("let getErr x = Error x; "
                                    "let r = try getErr 5 with | Error e when e <= 5 -> 100 | Error _ -> 0; "
                                    "print r")
          == "100");
}

TEST_CASE("IRGenerator.FSharp.exec_trywith_guard_ne")
{
    // Guard with !=
    CHECK(executeSourceAndGetOutput("let getErr x = Error x; "
                                    "let r = try getErr 5 with | Error e when e != 3 -> 100 | Error _ -> 0; "
                                    "print r")
          == "100");
}

TEST_CASE("IRGenerator.FSharp.exec_trywith_guard_eq")
{
    // Guard with ==
    CHECK(executeSourceAndGetOutput("let getErr x = Error x; "
                                    "let r = try getErr 5 with | Error e when e == 5 -> 100 | Error _ -> 0; "
                                    "print r")
          == "100");
}

TEST_CASE("IRGenerator.FSharp.exec_match_guard_gt")
{
    // Match expression with guard
    CHECK(executeSourceAndGetOutput("let getErr x = Error x; "
                                    "let r = match getErr 5 with | Error e when e > 3 -> 100 | _ -> 0; "
                                    "print r")
          == "100");
}

TEST_CASE("IRGenerator.FSharp.exec_match_guard_complex")
{
    // Match expression with multiple guards
    CHECK(executeSourceAndGetOutput(
              "let getErr x = Error x; "
              "let r = match getErr 5 with | Error e when e > 10 -> 1 | Error e when e > 3 -> 2 | _ -> 0; "
              "print r")
          == "2");
}

// =============================================================================
// F# print/println Execution Tests
// =============================================================================

TEST_CASE("IRGenerator.FSharp.exec_print_literal")
{
    CHECK(executeSourceAndGetOutput(R"(print "hello")") == "hello");
}

TEST_CASE("IRGenerator.FSharp.exec_println_literal")
{
    CHECK(executeSourceAndGetOutput(R"(println "hello")") == "hello\n");
}

TEST_CASE("IRGenerator.FSharp.exec_print_variable")
{
    CHECK(executeSourceAndGetOutput(R"(let msg = "world"; print msg)") == "world");
}

TEST_CASE("IRGenerator.FSharp.exec_println_variable")
{
    CHECK(executeSourceAndGetOutput(R"(let msg = "world"; println msg)") == "world\n");
}

TEST_CASE("IRGenerator.FSharp.exec_print_multiple")
{
    CHECK(executeSourceAndGetOutput(R"(print "a"; print "b"; print "c")") == "abc");
}

TEST_CASE("IRGenerator.FSharp.exec_println_multiple")
{
    CHECK(executeSourceAndGetOutput(R"(println "a"; println "b")") == "a\nb\n");
}

TEST_CASE("IRGenerator.FSharp.exec_print_mixed")
{
    CHECK(executeSourceAndGetOutput(R"(print "hello "; println "world")") == "hello world\n");
}

TEST_CASE("IRGenerator.FSharp.exec_print_with_let_binding")
{
    auto result = executeSource(R"(let x = "test"; print x)");
    REQUIRE(result.has_value());
    CHECK(result->exitCode == 0);
    CHECK(result->output == "test");
}

TEST_CASE("IRGenerator.FSharp.exec_print_empty_string")
{
    CHECK(executeSourceAndGetOutput(R"(print "")") == "");
}

TEST_CASE("IRGenerator.FSharp.exec_println_empty_string")
{
    CHECK(executeSourceAndGetOutput(R"(println "")") == "\n");
}

TEST_CASE("IRGenerator.FSharp.exec_result_returns_expected_on_failure")
{
    // Test that executeSource returns an error on parse failure
    auto result = executeSource("let x = "); // Incomplete, should fail
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("IRGenerator.FSharp.exec_print_preserves_exit_code")
{
    // Verify that print doesn't affect exit code
    auto result = executeSource(R"(print "test"; let x = 0)");
    REQUIRE(result.has_value());
    CHECK(result->exitCode == 0);
    CHECK(result->output == "test");
}

TEST_CASE("IRGenerator.FSharp.exec_print_number")
{
    // Print automatically converts numbers to strings via N2S
    CHECK(executeSourceAndGetOutput("print 42") == "42");
}

TEST_CASE("IRGenerator.FSharp.exec_print_number_expression")
{
    // Print works with numeric expressions
    CHECK(executeSourceAndGetOutput("let x = 10; print (x * 2 + 5)") == "25");
}

// =============================================================================
// F# Recursive Function (let rec) Execution Tests
// =============================================================================

TEST_CASE("IRGenerator.FSharp.exec_rec_countdown")
{
    // Simple recursive countdown to 0
    CHECK(executeSourceAndGetOutput("let rec countdown n = match n with | 0 -> 0 | _ -> countdown (n - 1); "
                                    "print (countdown 10)")
          == "0");
}

TEST_CASE("IRGenerator.FSharp.exec_rec_factorial")
{
    // Tail-recursive factorial with accumulator
    CHECK(executeSourceAndGetOutput(
              "let rec factorial n acc = match n with | 0 -> acc | _ -> factorial (n - 1) (n * acc); "
              "print (factorial 5 1)")
          == "120");
}

TEST_CASE("IRGenerator.FSharp.exec_rec_fibonacci")
{
    // Tail-recursive fibonacci with two accumulators
    CHECK(executeSourceAndGetOutput("let rec fib n a b = match n with | 0 -> a | _ -> fib (n - 1) b (a + b); "
                                    "print (fib 10 0 1)")
          == "55");
}

TEST_CASE("IRGenerator.FSharp.exec_rec_sum")
{
    // Tail-recursive sum with accumulator
    CHECK(
        executeSourceAndGetOutput("let rec sum n acc = match n with | 0 -> acc | _ -> sum (n - 1) (acc + n); "
                                  "print (sum 10 0)")
        == "55");
}

TEST_CASE("IRGenerator.FSharp.exec_rec_pipeline")
{
    // Recursive function invoked via pipeline
    CHECK(executeSourceAndGetOutput("let rec countdown n = match n with | 0 -> 0 | _ -> countdown (n - 1); "
                                    "print (10 |> countdown)")
          == "0");
}

TEST_CASE("IRGenerator.FSharp.exec_rec_non_tail_error")
{
    // Non-tail recursive call (n * f(n-1)) should fail to compile when called
    CHECK_FALSE(
        generatesIRSuccessfully("let rec factorial n = match n with | 0 -> 1 | _ -> n * factorial (n - 1); "
                                "let r = factorial 5"));
}

// =============================================================================
// Closure Tests (capturing outer scope variables)
// =============================================================================

TEST_CASE("IRGenerator.FSharp.exec_closure_simple")
{
    // Lambda captures outer variable
    CHECK(executeSourceAndGetOutput("let n = 10; let f = fun x -> x + n; print (f 5)") == "15");
}

TEST_CASE("IRGenerator.FSharp.exec_closure_multiplier")
{
    // Closure over a multiplier variable
    CHECK(executeSourceAndGetOutput("let m = 3; let scale = fun x -> x * m; print (scale 7)") == "21");
}

TEST_CASE("IRGenerator.FSharp.exec_closure_nested")
{
    // Nested closure: inner lambda captures from outer lambda's scope
    CHECK(executeSourceAndGetOutput(
              "let a = 1; let f = fun x -> (fun y -> x + y + a); let g = f 10; print (g 5)")
          == "16");
}

TEST_CASE("IRGenerator.FSharp.exec_closure_named_function")
{
    // Named function (let f x = ...) captures outer variable
    CHECK(executeSourceAndGetOutput("let offset = 100; let addOffset x = x + offset; print (addOffset 5)")
          == "105");
}

TEST_CASE("IRGenerator.FSharp.exec_closure_in_pipeline")
{
    // Closure used in pipeline
    CHECK(executeSourceAndGetOutput("let n = 10; let add_n = fun x -> x + n; print (5 |> add_n)") == "15");
}

// =============================================================================
// Partial Application Tests
// =============================================================================

TEST_CASE("IRGenerator.FSharp.exec_partial_simple")
{
    // Partial application of a 2-arg function
    CHECK(executeSourceAndGetOutput("let add x y = x + y; let add5 = add 5; print (add5 10)") == "15");
}

TEST_CASE("IRGenerator.FSharp.exec_partial_three_params")
{
    // Chained partial application of a 3-arg function
    CHECK(executeSourceAndGetOutput(
              "let mul x y z = x * y * z; let mul2 = mul 2; let mul2x3 = mul2 3; print (mul2x3 4)")
          == "24");
}

TEST_CASE("IRGenerator.FSharp.exec_partial_pipeline")
{
    // Partial application in pipeline: value |> func arg
    CHECK(executeSourceAndGetOutput("let add x y = x + y; print (10 |> add 5)") == "15");
}

TEST_CASE("IRGenerator.FSharp.exec_partial_of_partial")
{
    // Function alias of a partially applied function
    CHECK(executeSourceAndGetOutput("let add x y = x + y; let f = add 1; let g = f; print (g 2)") == "3");
}

TEST_CASE("IRGenerator.FSharp.exec_partial_function_alias")
{
    // Zero-arg partial application (function aliasing): let f = add
    CHECK(executeSourceAndGetOutput("let add x y = x + y; let f = add; print (f 3 4)") == "7");
}

TEST_CASE("IRGenerator.FSharp.exec_partial_over_application_error")
{
    // Over-application should fail
    CHECK_FALSE(generatesIRSuccessfully("let add x y = x + y; let r = add 1 2 3"));
}
