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
// F# Handler-Compiled Function Tests (UCALL/URET)
// =============================================================================

TEST_CASE("IRGenerator.FSharp.handler_typed_identity")
{
    // Identity function with type annotation compiles as handler (UCALL/URET)
    CHECK(executesWithOutput("let id (x: int) = x; print (id 42)", "42"));
}

TEST_CASE("IRGenerator.FSharp.handler_typed_arithmetic")
{
    // Integer arithmetic with typed params compiles as handler
    CHECK(executesWithOutput("let add (x: int) (y: int) = x + y; print (add 3 4)", "7"));
    CHECK(executesWithOutput("let sq (x: int) = x * x; print (sq 5)", "25"));
}

TEST_CASE("IRGenerator.FSharp.handler_typed_if_then_else")
{
    // Branching with typed params in handler
    CHECK(executesWithOutput("let abs (x: int) = if x < 0 then 0 - x else x; print (abs (0 - 7))", "7"));
    CHECK(executesWithOutput("let abs (x: int) = if x < 0 then 0 - x else x; print (abs 3)", "3"));
}

TEST_CASE("IRGenerator.FSharp.handler_typed_string_concat")
{
    // String concat with typed string param compiles as handler
    CHECK(executesWithOutput(R"(let greet (name: str) = "hello " + name; print (greet "world"))",
                             "hello world"));
}

TEST_CASE("IRGenerator.FSharp.handler_typed_multiple_calls")
{
    // Multiple calls to the same handler
    CHECK(executesWithOutput("let inc (x: int) = x + 1; print (inc (inc (inc 0)))", "3"));
}

TEST_CASE("IRGenerator.FSharp.handler_typed_float")
{
    // Float function with type annotations compiles as handler
    CHECK(executesWithOutput("let double (x: float) = x * 2.0; print (double 3.5)", "7"));
}

TEST_CASE("IRGenerator.FSharp.handler_typed_bool")
{
    // Boolean parameter with type annotation
    CHECK(executesWithOutput(R"(let show (b: bool) = if b then "yes" else "no"; print (show true))", "yes"));
}

TEST_CASE("IRGenerator.FSharp.handler_zero_params")
{
    // Zero-parameter function compiles as handler
    CHECK(executesWithOutput("let answer = 42; print answer", "42"));
}

TEST_CASE("IRGenerator.FSharp.handler_fallback_untyped")
{
    // Functions without type annotations fall back to AST inlining but still work
    CHECK(executesWithOutput("let id x = x; print (id 42)", "42"));
    CHECK(executesWithOutput("let add x y = x + y; print (add 3 4)", "7"));
    CHECK(executesWithOutput("let double x = x * 2.0; print (double 3.5)", "7"));
    CHECK(executesWithOutput("let make x = x :: []; print (make 99)", "[99]"));
    CHECK(executesWithOutput(R"(let greet name = "hello " + name; print (greet "world"))", "hello world"));
}

// =============================================================================
// F# Closure Tests (Handler-compiled functions with captured variables)
// =============================================================================

TEST_CASE("IRGenerator.FSharp.closure_capture_int")
{
    // Closure captures an integer from enclosing scope
    CHECK(executesWithOutput("let x = 10; let addx (y: int) = x + y; print (addx 5)", "15"));
}

TEST_CASE("IRGenerator.FSharp.closure_capture_multiple")
{
    // Closure captures multiple variables
    CHECK(executesWithOutput("let a = 3; let b = 7; let f (x: int) = a + b + x; print (f 10)", "20"));
}

TEST_CASE("IRGenerator.FSharp.closure_capture_string")
{
    // Closure captures a string from enclosing scope (type annotation is `str` not `string`)
    CHECK(executesWithOutput(
        R"(let prefix = "hello "; let greet (name: str) = prefix + name; print (greet "world"))",
        "hello world"));
}

TEST_CASE("IRGenerator.FSharp.closure_capture_float")
{
    // Closure captures a float
    CHECK(executesWithOutput("let scale = 2.5; let f (x: float) = x * scale; print (f 4.0)", "10"));
}

TEST_CASE("IRGenerator.FSharp.closure_multiple_calls")
{
    // Same closure called multiple times with different arguments
    CHECK(executesWithOutput("let offset = 100; let f (x: int) = x + offset; print (f 1); print (f 2)",
                             "101102"));
}

TEST_CASE("IRGenerator.FSharp.closure_nested_lets")
{
    // Closure captures from nested let bindings
    CHECK(executesWithOutput("let x = 5; let y = 10; let z = 20; let f (n: int) = x + y + z + n; print (f 1)",
                             "36"));
}

TEST_CASE("IRGenerator.FSharp.closure_zero_params_with_capture")
{
    // Thunk with capture — uses untyped parameter, falls back to AST inlining
    CHECK(executesWithOutput("let x = 42; let f _dummy = x; print (f 0)", "42"));
}

TEST_CASE("IRGenerator.FSharp.closure_capture_untyped_fallback")
{
    // Untyped closure falls back to AST inlining but still works
    CHECK(executesWithOutput("let x = 10; let addx y = x + y; print (addx 5)", "15"));
}

// =============================================================================
// F# Recursive Function Tests (UCALL/UTCALL-based with type annotations)
// =============================================================================

TEST_CASE("IRGenerator.FSharp.handler_recursive_countdown")
{
    // Tail-recursive countdown using UTCALL
    CHECK(executesWithOutput(
        "let rec countdown (n: int) = if n <= 0 then 0 else countdown (n - 1); print (countdown 10)", "0"));
}

TEST_CASE("IRGenerator.FSharp.handler_recursive_factorial_tail")
{
    // Tail-recursive factorial with accumulator
    CHECK(executesWithOutput(
        "let rec fact (n: int) (acc: int) = if n <= 1 then acc else fact (n - 1) (n * acc);"
        " print (fact 5 1)",
        "120"));
}

TEST_CASE("IRGenerator.FSharp.handler_recursive_sum")
{
    // Tail-recursive sum using match
    CHECK(executesWithOutput(
        "let rec sum (n: int) (acc: int) = match n with | 0 -> acc | _ -> sum (n - 1) (acc + n);"
        " print (sum 10 0)",
        "55"));
}

TEST_CASE("IRGenerator.FSharp.handler_recursive_multiple_calls")
{
    // Recursive function called multiple times
    CHECK(executesWithOutput("let rec countdown (n: int) = if n <= 0 then 0 else countdown (n - 1);"
                             " print (countdown 5); print (countdown 3)",
                             "00"));
}

TEST_CASE("IRGenerator.FSharp.handler_recursive_with_capture")
{
    // Recursive function that captures a variable
    CHECK(executesWithOutput(
        "let step = 2; let rec count (n: int) = if n <= 0 then 0 else count (n - step); print (count 10)",
        "0"));
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

TEST_CASE("IRGenerator.FSharp.option_match")
{
    // Match on Option value and verify correct arm is taken
    CHECK(
        executeSourceAndGetOutput("let x = Some 42; let r = match x with | Some n -> n | None -> 0; print r")
        == "42");
}

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

TEST_CASE("IRGenerator.FSharp.result_match")
{
    // Match on Result value and verify correct arm is taken
    CHECK(executeSourceAndGetOutput("let r = Ok 42; let x = match r with | Ok n -> n | Error e -> 0; print x")
          == "42");
}

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
    // Match on Option with Some and None patterns
    REQUIRE(
        generatesIRSuccessfully("let opt = Some 42; let result = match opt with | Some x -> x | None -> 0"));
}

TEST_CASE("IRGenerator.FSharp.match_option_none_pattern")
{
    // Match with None value
    REQUIRE(
        generatesIRSuccessfully("let opt = None; let result = match opt with | Some x -> x | None -> -1"));
}

TEST_CASE("IRGenerator.FSharp.match_result_ok_error")
{
    // Match on Result with Ok and Error patterns
    REQUIRE(
        generatesIRSuccessfully("let res = Ok 100; let value = match res with | Ok n -> n | Error e -> 0"));
}

TEST_CASE("IRGenerator.FSharp.match_result_error_pattern")
{
    // Match with Error value
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

TEST_CASE("IRGenerator.FSharp.try_with_multiple_handlers")
{
    // Try-with with multiple error handlers - verify pattern matching selects correct handler
    CHECK(executeSourceAndGetOutput("let getErr x = Error x; "
                                    "let r = try getErr 1 with | Error 1 -> 10 | Error _ -> 0; "
                                    "print r")
          == "10");
}

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

// =============================================================================
// F# Arithmetic Output Verification Tests
// =============================================================================

TEST_CASE("IRGenerator.FSharp.exec_arith_add")
{
    CHECK(executeSourceAndGetOutput("let x = 2 + 3; print x") == "5");
}

TEST_CASE("IRGenerator.FSharp.exec_arith_sub")
{
    CHECK(executeSourceAndGetOutput("let x = 10 - 3; print x") == "7");
}

TEST_CASE("IRGenerator.FSharp.exec_arith_mul")
{
    CHECK(executeSourceAndGetOutput("let x = 4 * 5; print x") == "20");
}

TEST_CASE("IRGenerator.FSharp.exec_arith_div")
{
    CHECK(executeSourceAndGetOutput("let x = 15 / 3; print x") == "5");
}

TEST_CASE("IRGenerator.FSharp.exec_arith_mod")
{
    CHECK(executeSourceAndGetOutput("let x = 17 % 5; print x") == "2");
}

TEST_CASE("IRGenerator.FSharp.exec_arith_pow")
{
    CHECK(executeSourceAndGetOutput("let x = 2 ** 8; print x") == "256");
}

TEST_CASE("IRGenerator.FSharp.exec_arith_neg")
{
    CHECK(executeSourceAndGetOutput("let x = 10; print (0 - x)") == "-10");
}

TEST_CASE("IRGenerator.FSharp.exec_arith_complex")
{
    // Precedence: 2 + 3 * 4 = 14
    CHECK(executeSourceAndGetOutput("let x = 2 + 3 * 4; print x") == "14");
}

TEST_CASE("IRGenerator.FSharp.exec_arith_parens")
{
    // Override precedence: (2 + 3) * 4 = 20
    CHECK(executeSourceAndGetOutput("let x = (2 + 3) * 4; print x") == "20");
}

TEST_CASE("IRGenerator.FSharp.exec_comparison_eq")
{
    // Comparison via guard expression (avoids matching on boolean directly)
    CHECK(executeSourceAndGetOutput("let r = match 0 with | _ when 5 == 5 -> 1 | _ -> 0; print r") == "1");
}

TEST_CASE("IRGenerator.FSharp.exec_comparison_ne")
{
    CHECK(executeSourceAndGetOutput("let r = match 0 with | _ when 5 != 3 -> 1 | _ -> 0; print r") == "1");
}

TEST_CASE("IRGenerator.FSharp.exec_comparison_lt")
{
    CHECK(executeSourceAndGetOutput("let r = match 0 with | _ when 3 < 5 -> 1 | _ -> 0; print r") == "1");
}

TEST_CASE("IRGenerator.FSharp.exec_comparison_gt")
{
    CHECK(executeSourceAndGetOutput("let r = match 0 with | _ when 5 > 3 -> 1 | _ -> 0; print r") == "1");
}

TEST_CASE("IRGenerator.FSharp.exec_logical_and")
{
    CHECK(executeSourceAndGetOutput("let a = 5; let r = match 0 with | _ when a > 3 && a < 10 -> 1 | _ -> 0; "
                                    "print r")
          == "1");
}

TEST_CASE("IRGenerator.FSharp.exec_logical_or")
{
    CHECK(executeSourceAndGetOutput("let a = 5; let r = match 0 with | _ when a < 3 || a > 4 -> 1 | _ -> 0; "
                                    "print r")
          == "1");
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
// F# Try (?) Operator — Pattern Matching on Return Value Tests
// =============================================================================

TEST_CASE("IRGenerator.FSharp.exec_try_result_ok_match")
{
    // Pattern match on ?-returning function with Ok value
    CHECK(executesWithOutput("let f x = x?; "
                             "let r = match (f (Ok 42)) with | Ok n -> n | Error e -> 0; "
                             "print r",
                             "42"));
}

TEST_CASE("IRGenerator.FSharp.exec_try_result_error_match")
{
    // Pattern match on ?-returning function with Error value (error propagation)
    CHECK(executesWithOutput("let f x = x?; "
                             "let r = match (f (Error 99)) with | Ok n -> n | Error e -> e; "
                             "print r",
                             "99"));
}

TEST_CASE("IRGenerator.FSharp.exec_try_option_some_match")
{
    // Pattern match on ?-returning function with Some value
    CHECK(executesWithOutput("let f x = x?; "
                             "let r = match (f (Some 42)) with | Some n -> n | None -> 0; "
                             "print r",
                             "42"));
}

TEST_CASE("IRGenerator.FSharp.exec_try_option_none_match")
{
    // Pattern match on ?-returning function with None value (error propagation)
    CHECK(executesWithOutput("let f x = x?; "
                             "let r = match (f None) with | Some n -> n | None -> 0; "
                             "print r",
                             "0"));
}

TEST_CASE("IRGenerator.FSharp.exec_try_in_let_in")
{
    // ? inside let-in expression
    CHECK(executesWithOutput("let inc x = Some (x + 1); "
                             "let f x = let v = (inc x)? in v * 2; "
                             "let r = match (f 5) with | Some n -> n | None -> 0; "
                             "print r",
                             "12"));
}

TEST_CASE("IRGenerator.FSharp.exec_try_chained_with_match")
{
    // Chained ? with pattern match on result
    CHECK(executesWithOutput("let wrap x = Ok x; "
                             "let f x = (wrap (wrap x)?)?; "
                             "let r = match (f 42) with | Ok n -> n | Error e -> 0; "
                             "print r",
                             "42"));
}

TEST_CASE("IRGenerator.FSharp.exec_try_function_call_with_match")
{
    // ? on function call result with pattern match
    CHECK(executesWithOutput("let inc x = Ok (x + 1); "
                             "let f x = (inc x)?; "
                             "let r = match (f 5) with | Ok n -> n | Error e -> 0; "
                             "print r",
                             "6"));
}

TEST_CASE("IRGenerator.FSharp.exec_try_result_error_propagation_with_match")
{
    // Error propagation through ? with match on outer function
    CHECK(executesWithOutput("let maybe_fail x = if x > 0 then Ok x else Error 0; "
                             "let f x = (maybe_fail x)?; "
                             "let r = match (f 0) with | Ok n -> n | Error e -> 99; "
                             "print r",
                             "99"));
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
// F# Try-Finally Expression Execution Tests
// =============================================================================

TEST_CASE("IRGenerator.FSharp.exec_tryfinally_basic")
{
    // try-finally: body returns 42, finally runs print, result is 42
    CHECK(executeSourceAndGetOutput("let f x = try 42 finally print \"cleanup\"; print (f 0)")
          == "cleanup42");
}

TEST_CASE("IRGenerator.FSharp.exec_tryfinally_string")
{
    // try-finally: body returns string, finally runs, string result preserved
    CHECK(executeSourceAndGetOutput("let f x = try \"hello\" finally print \"done\"; print (f 0)")
          == "donehello");
}

TEST_CASE("IRGenerator.FSharp.exec_tryfinally_error_propagation")
{
    // try-finally with ? error propagation: finally runs before error propagates
    CHECK(executeSourceAndGetOutput("let inner x = Error 99; "
                                    "let outer x = try (inner x)? finally print \"cleanup\"; "
                                    "let r = match outer 0 with | Ok v -> v | Error e -> e; "
                                    "print r")
          == "cleanup99");
}

TEST_CASE("IRGenerator.FSharp.exec_tryfinally_nested")
{
    // Nested try-finally: inner and outer finally blocks both run in correct order
    CHECK(executeSourceAndGetOutput(
              "let f x = try (try 1 finally print \"inner\") finally print \"outer\"; print (f 0)")
          == "innerouter1");
}

TEST_CASE("IRGenerator.FSharp.exec_tryfinally_toplevel")
{
    // try-finally at top level (no function context)
    CHECK(executeSourceAndGetOutput("let r = try 42 finally print \"cleanup\"; print r") == "cleanup42");
}

TEST_CASE("IRGenerator.FSharp.exec_tryfinally_finally_value_discarded")
{
    // The finally expression's own value is discarded — result is always the body's value
    CHECK(executeSourceAndGetOutput("let f x = try 10 finally 999; print (f 0)") == "10");
}

TEST_CASE("IRGenerator.FSharp.exec_tryfinally_ok_success_path")
{
    // ? inside try-finally on Ok value: unwraps successfully, finally still runs.
    // The function returns a Result-wrapped value, so we pattern-match to extract it.
    CHECK(executeSourceAndGetOutput("let inner x = Ok 42; "
                                    "let outer x = try (inner x)? finally print \"fin\"; "
                                    "let r = match (outer 0) with | Ok n -> n | Error e -> 0; "
                                    "print r")
          == "fin42");
}

TEST_CASE("IRGenerator.FSharp.exec_tryfinally_some_success_path")
{
    // ? inside try-finally on Some value: unwraps successfully, finally still runs
    // The function returns an Option-wrapped value, so we pattern-match to extract it.
    CHECK(executeSourceAndGetOutput("let inner x = Some 7; "
                                    "let outer x = try (inner x)? finally print \"done\"; "
                                    "let r = match (outer 0) with | Some n -> n | None -> 0; "
                                    "print r")
          == "done7");
}

TEST_CASE("IRGenerator.FSharp.exec_tryfinally_body_computes")
{
    // Finally with a body that does computation, cleanup just prints
    CHECK(executeSourceAndGetOutput("let f x = try x * 2 + 1 finally print \"fin\"; print (f 20)")
          == "fin41");
}

TEST_CASE("IRGenerator.FSharp.exec_tryfinally_body_uses_arg")
{
    // Body uses function argument, finally runs, argument-derived result preserved
    CHECK(executeSourceAndGetOutput("let f x = try x + 10 finally print \"ok\"; print (f 32)") == "ok42");
}

TEST_CASE("IRGenerator.FSharp.exec_tryfinally_in_match_arm")
{
    // try-finally used inside a match arm body
    CHECK(executeSourceAndGetOutput("let f x = match x with "
                                    "| 1 -> try 100 finally print \"arm1\" "
                                    "| _ -> try 200 finally print \"arm2\"; "
                                    "print (f 1)")
          == "arm1100");
}

TEST_CASE("IRGenerator.FSharp.exec_tryfinally_error_nested_propagation")
{
    // Error propagates through nested try-finally: both finally blocks run
    CHECK(executeSourceAndGetOutput("let fail x = Error 7; "
                                    "let inner x = try (fail x)? finally print \"i\"; "
                                    "let outer x = try (inner x)? finally print \"o\"; "
                                    "let r = match outer 0 with | Ok v -> v | Error e -> e; "
                                    "print r")
          == "io7");
}

TEST_CASE("IRGenerator.FSharp.exec_tryfinally_toplevel_string_body")
{
    // Top-level try-finally with string body
    CHECK(executeSourceAndGetOutput("let r = try \"world\" finally print \"hello \"; print r")
          == "hello world");
}

// =============================================================================
// F# Top-Level ? Operator Tests
// =============================================================================

TEST_CASE("IRGenerator.FSharp.try_toplevel_some_ir")
{
    // IR generation succeeds for top-level ? on Some
    REQUIRE(generatesIRSuccessfully("let x = (Some 42)?"));
}

TEST_CASE("IRGenerator.FSharp.try_toplevel_none_ir")
{
    // IR generation succeeds for top-level ? on None
    REQUIRE(generatesIRSuccessfully("let x = None?"));
}

TEST_CASE("IRGenerator.FSharp.exec_try_toplevel_unwrap_some")
{
    // Top-level ? unwraps Some value
    CHECK(executeSourceAndGetOutput("let x = (Some 42)?; print x") == "42");
}

TEST_CASE("IRGenerator.FSharp.exec_try_toplevel_none_exits")
{
    // Top-level ? on None exits with code 1
    REQUIRE(executesWithExitCode("let x = None?", 1));
}

TEST_CASE("IRGenerator.FSharp.exec_try_toplevel_unwrap_ok")
{
    // Top-level ? unwraps Ok value
    CHECK(executeSourceAndGetOutput("let x = (Ok 100)?; print x") == "100");
}

TEST_CASE("IRGenerator.FSharp.exec_try_toplevel_error_exits")
{
    // Top-level ? on Error exits with code 1
    REQUIRE(executesWithExitCode("let x = (Error 404)?", 1));
}

TEST_CASE("IRGenerator.FSharp.exec_try_toplevel_multiple")
{
    // Multiple top-level ? operators in sequence
    CHECK(executeSourceAndGetOutput("let a = (Some 10)?; let b = (Some 20)?; print (a + b)") == "30");
}

TEST_CASE("IRGenerator.FSharp.exec_try_toplevel_inline")
{
    // Inline top-level ? (result used directly in function argument)
    CHECK(executeSourceAndGetOutput("print (Some 42)?") == "42");
}

TEST_CASE("IRGenerator.FSharp.exec_try_toplevel_env_exists")
{
    // Top-level ? with env builtin for existing variable — prints actual string value
    auto& rt = TestRuntime::instance();
    rt.clearMockEnvVars();
    rt.setMockEnvVar("USER", "testuser");
    CHECK(executeSourceAndGetOutput(R"(let name = (env "USER")?; print name)") == "testuser");
    rt.clearMockEnvVars();
}

TEST_CASE("IRGenerator.FSharp.exec_try_env_println")
{
    // Verify println also works correctly with unwrapped env string
    auto& rt = TestRuntime::instance();
    rt.clearMockEnvVars();
    rt.setMockEnvVar("USER", "testuser");
    CHECK(executeSourceAndGetOutput(R"(let name = (env "USER")?; println name)") == "testuser\n");
    rt.clearMockEnvVars();
}

TEST_CASE("IRGenerator.FSharp.exec_try_env_via_let")
{
    // Multi-step: bind env result to variable, then unwrap with ?
    auto& rt = TestRuntime::instance();
    rt.clearMockEnvVars();
    rt.setMockEnvVar("HOME", "/home/test");
    CHECK(executeSourceAndGetOutput(R"(let opt = env "HOME"; let h = opt?; print h)") == "/home/test");
    rt.clearMockEnvVars();
}

TEST_CASE("IRGenerator.FSharp.exec_try_some_string")
{
    // Unwrapping Some with a string value should print the string, not a raw pointer
    CHECK(executeSourceAndGetOutput(R"(let x = (Some "hello")?; print x)") == "hello");
}

TEST_CASE("IRGenerator.FSharp.exec_try_toplevel_env_missing_exits")
{
    // Top-level ? with env builtin for missing variable exits with code 1
    auto& rt = TestRuntime::instance();
    rt.clearMockEnvVars();
    REQUIRE(executesWithExitCode(R"(let x = (env "NONEXISTENT_VAR_12345")?)", 1));
    rt.clearMockEnvVars();
}

// =============================================================================
// F# Or Pattern Tests
// =============================================================================

TEST_CASE("IRGenerator.FSharp.exec_or_pattern_first")
{
    // Or pattern: first alternative matches
    CHECK(executeSourceAndGetOutput("let x = 1; "
                                    "let r = match x with | 1 | 2 | 3 -> 10 | _ -> 0; "
                                    "print r")
          == "10");
}

TEST_CASE("IRGenerator.FSharp.exec_or_pattern_middle")
{
    // Or pattern: middle alternative matches
    CHECK(executeSourceAndGetOutput("let x = 2; "
                                    "let r = match x with | 1 | 2 | 3 -> 10 | _ -> 0; "
                                    "print r")
          == "10");
}

TEST_CASE("IRGenerator.FSharp.exec_or_pattern_last")
{
    // Or pattern: last alternative matches
    CHECK(executeSourceAndGetOutput("let x = 3; "
                                    "let r = match x with | 1 | 2 | 3 -> 10 | _ -> 0; "
                                    "print r")
          == "10");
}

TEST_CASE("IRGenerator.FSharp.exec_or_pattern_no_match")
{
    // Or pattern: none match, falls through to wildcard
    CHECK(executeSourceAndGetOutput("let x = 99; "
                                    "let r = match x with | 1 | 2 | 3 -> 10 | _ -> 0; "
                                    "print r")
          == "0");
}

// =============================================================================
// F# As Pattern Tests
// =============================================================================

TEST_CASE("IRGenerator.FSharp.exec_as_pattern_binding")
{
    // As pattern: bind the entire value while also matching
    CHECK(executeSourceAndGetOutput("let x = 42; "
                                    "let r = match x with | n as val -> val; "
                                    "print r")
          == "42");
}

TEST_CASE("IRGenerator.FSharp.exec_as_pattern_with_literal")
{
    // As pattern with literal inner pattern
    CHECK(executeSourceAndGetOutput("let x = 5; "
                                    "let r = match x with | 5 as val -> val | _ -> 0; "
                                    "print r")
          == "5");
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
    // Non-tail recursive calls now work via UCALL (handler compilation).
    // Previously this was an error when using AST inlining.
    CHECK(
        executeSourceAndGetOutput("let rec factorial n = match n with | 0 -> 1 | _ -> n * factorial (n - 1); "
                                  "print (factorial 5)")
        == "120");
}

// =============================================================================
// Mutual Recursion Tests (let rec ... and ...)
// =============================================================================

TEST_CASE("IRGenerator.FSharp.mutual_rec_even_odd")
{
    // Classic mutual recursion: isEven/isOdd
    CHECK(executeSourceAndGetOutput("let rec isEven n = match n with | 0 -> 1 | _ -> isOdd (n - 1) "
                                    "and isOdd n = match n with | 0 -> 0 | _ -> isEven (n - 1); "
                                    "print (isEven 4)")
          == "1");
}

TEST_CASE("IRGenerator.FSharp.mutual_rec_even_odd_false")
{
    // isEven on odd number should return 0
    CHECK(executeSourceAndGetOutput("let rec isEven n = match n with | 0 -> 1 | _ -> isOdd (n - 1) "
                                    "and isOdd n = match n with | 0 -> 0 | _ -> isEven (n - 1); "
                                    "print (isEven 3)")
          == "0");
}

TEST_CASE("IRGenerator.FSharp.mutual_rec_isOdd")
{
    // Call the second function in the mutual recursion
    CHECK(executeSourceAndGetOutput("let rec isEven n = match n with | 0 -> 1 | _ -> isOdd (n - 1) "
                                    "and isOdd n = match n with | 0 -> 0 | _ -> isEven (n - 1); "
                                    "print (isOdd 5)")
          == "1");
}

TEST_CASE("IRGenerator.FSharp.mutual_rec_parser_test")
{
    // Verify mutual recursion parses and generates IR
    REQUIRE(generatesIRSuccessfully("let rec f n = match n with | 0 -> 0 | _ -> g (n - 1) "
                                    "and g n = match n with | 0 -> 1 | _ -> f (n - 1)"));
}

TEST_CASE("IRGenerator.FSharp.mutual_rec_multiline")
{
    CHECK(executesWithOutput("let rec isEven n =\n"
                             "    match n with | 0 -> 1 | _ -> isOdd (n - 1)\n"
                             "and isOdd n =\n"
                             "    match n with | 0 -> 0 | _ -> isEven (n - 1);\n"
                             "print (isEven 4)",
                             "1"));
}

// =============================================================================
// Let-In Expression Tests
// =============================================================================

TEST_CASE("IRGenerator.FSharp.let_in_simple")
{
    // Simple let-in binding
    CHECK(executeSourceAndGetOutput("let r = let x = 5 in x + 10; print r") == "15");
}

TEST_CASE("IRGenerator.FSharp.let_in_function")
{
    // Function binding in let-in
    CHECK(executeSourceAndGetOutput("let r = let double x = x * 2 in double 5; print r") == "10");
}

TEST_CASE("IRGenerator.FSharp.let_in_nested")
{
    // Nested let-in expressions
    CHECK(executeSourceAndGetOutput("let r = let a = 1 in let b = 2 in a + b; print r") == "3");
}

TEST_CASE("IRGenerator.FSharp.let_in_scoping")
{
    // Variable in let-in should not leak to outer scope
    CHECK(executeSourceAndGetOutput("let x = 100; let r = let x = 5 in x + 1; print r") == "6");
}

TEST_CASE("IRGenerator.FSharp.let_in_with_outer_variable")
{
    // Let-in body can access outer scope
    CHECK(executeSourceAndGetOutput("let y = 10; let r = let x = 5 in x + y; print r") == "15");
}

TEST_CASE("IRGenerator.FSharp.let_in_parenthesized")
{
    // Let-in inside parentheses (as function argument)
    CHECK(executeSourceAndGetOutput("print (let x = 7 in x * 3)") == "21");
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
    CHECK(generatesIRWithError("let add x y = x + y; let r = add 1 2 3", "expects 2 arguments, got 3"));
}

// =============================================================================
// Arity Enforcement Tests
// =============================================================================

TEST_CASE("IRGenerator.FSharp.ArityEnforcement.over_application_1param")
{
    // 1-param function called with 2 args must fail
    CHECK(generatesIRWithError("let f x = x + 1; f 3 4", "expects 1 argument, got 2"));
}

TEST_CASE("IRGenerator.FSharp.ArityEnforcement.over_application_1param_annotated")
{
    // Annotated 1-param function called with 2 args must fail
    CHECK(generatesIRWithError("let f (x: int): int = x + 1; f 3 4", "expects 1 argument, got 2"));
}

TEST_CASE("IRGenerator.FSharp.ArityEnforcement.over_application_1param_in_print")
{
    // Over-application inside print wrapper must fail
    CHECK(generatesIRWithError("let f (x: int): int = x + 1; print (f 3 4)", "expects 1 argument, got 2"));
}

TEST_CASE("IRGenerator.FSharp.ArityEnforcement.over_application_2param")
{
    // 2-param function called with 3 args must fail
    CHECK(generatesIRWithError("let add x y = x + y; add 1 2 3", "expects 2 arguments, got 3"));
}

TEST_CASE("IRGenerator.FSharp.ArityEnforcement.over_application_2param_annotated")
{
    // Annotated 2-param function called with 3 args must fail
    CHECK(
        generatesIRWithError("let f (x: int) (y: int): int = x + y; f 1 2 3", "expects 2 arguments, got 3"));
}

TEST_CASE("IRGenerator.FSharp.ArityEnforcement.exact_arity_1param")
{
    // 1-param function called with exactly 1 arg must succeed
    CHECK(executeSourceAndGetOutput("let f x = x + 1; print (f 3)") == "4");
}

TEST_CASE("IRGenerator.FSharp.ArityEnforcement.exact_arity_2param")
{
    // 2-param function called with exactly 2 args must succeed
    CHECK(executeSourceAndGetOutput("let add x y = x + y; print (add 3 4)") == "7");
}

TEST_CASE("IRGenerator.FSharp.ArityEnforcement.partial_then_exact")
{
    // Partial application followed by exact application must succeed
    CHECK(executeSourceAndGetOutput("let add x y = x + y; let f = add 3; print (f 4)") == "7");
}

// =============================================================================
// REPL Session Persistence Tests (multi-prompt)
// =============================================================================

TEST_CASE("IRGenerator.FSharp.session_simple_function")
{
    // Define function in first prompt, call it in second prompt
    CHECK(sessionProducesOutput({ "let double x = x * 2", "print (double 5)" }, "10"));
}

TEST_CASE("IRGenerator.FSharp.session_recursive_function")
{
    // Define tail-recursive function in first prompt, call it in second prompt
    CHECK(sessionProducesOutput(
        { "let rec factorial n acc = match n with | 0 -> acc | _ -> factorial (n - 1) (n * acc)",
          "print (factorial 5 1)" },
        "120"));
}

TEST_CASE("IRGenerator.FSharp.session_multiple_functions")
{
    // Define two functions across prompts, use both
    CHECK(sessionProducesOutput({ "let add x y = x + y", "let mul x y = x * y", "print (add (mul 3 4) 5)" },
                                "17"));
}

TEST_CASE("IRGenerator.FSharp.session_function_reuse_across_three_prompts")
{
    // Define in prompt 1, use in prompt 2 and 3
    CHECK(sessionProducesOutput({ "let square x = x * x", "print (square 4)", "print (square 7)" }, "49"));
}

TEST_CASE("IRGenerator.FSharp.session_function_redefinition")
{
    // Define function, redefine it, verify new definition is used
    CHECK(sessionProducesOutput({ "let f x = x + 1", "let f x = x * 10", "print (f 5)" }, "50"));
}

TEST_CASE("IRGenerator.FSharp.session_function_calling_persisted_function")
{
    // Define helper function, then define function using it
    CHECK(sessionProducesOutput(
        { "let double x = x * 2", "let quadruple x = double (double x)", "print (quadruple 3)" }, "12"));
}

TEST_CASE("IRGenerator.FSharp.session_recursive_fibonacci")
{
    // Tail-recursive fibonacci across prompts
    CHECK(sessionProducesOutput(
        { "let rec fib n a b = match n with | 0 -> a | _ -> fib (n - 1) b (a + b)", "print (fib 10 0 1)" },
        "55"));
}

TEST_CASE("IRGenerator.FSharp.session_lambda_bound_function")
{
    // Lambda assigned to variable persists
    CHECK(sessionProducesOutput({ "let inc = fun x -> x + 1", "print (inc 41)" }, "42"));
}

// =============================================================================
// REPL Session Persistence Tests — Value Bindings
// =============================================================================

TEST_CASE("IRGenerator.FSharp.session_simple_value_binding")
{
    CHECK(sessionProducesOutput({ "let x = 42", "print x" }, "42"));
}

TEST_CASE("IRGenerator.FSharp.session_value_binding_in_expression")
{
    CHECK(sessionProducesOutput({ "let x = 10", "print (x + 5)" }, "15"));
}

TEST_CASE("IRGenerator.FSharp.session_value_depending_on_value")
{
    CHECK(sessionProducesOutput({ "let x = 42", "let y = x + 1", "print y" }, "43"));
}

TEST_CASE("IRGenerator.FSharp.session_function_using_persisted_value")
{
    CHECK(sessionProducesOutput({ "let x = 10", "let f y = y + x", "print (f 5)" }, "15"));
}

TEST_CASE("IRGenerator.FSharp.session_value_redefinition")
{
    CHECK(sessionProducesOutput({ "let x = 42", "let x = 100", "print x" }, "100"));
}

TEST_CASE("IRGenerator.FSharp.session_value_binding_string")
{
    CHECK(sessionProducesOutput({ R"(let name = "world")", R"(print ("hello " + name))" }, "hello world"));
}

TEST_CASE("IRGenerator.FSharp.session_value_binding_bool")
{
    CHECK(sessionProducesOutput({ "let flag = true", "print flag" }, "true"));
}

TEST_CASE("IRGenerator.FSharp.session_value_binding_float")
{
    CHECK(sessionProducesOutput({ "let pi = 3.14", "print pi" }, "3.14"));
}

TEST_CASE("IRGenerator.FSharp.session_mutable_value_binding")
{
    // Mutable binding persists across prompts (initial value preserved)
    CHECK(sessionProducesOutput({ "let mut x = 0", "print x" }, "0"));
}

TEST_CASE("IRGenerator.FSharp.session_mutable_cross_prompt_mutation")
{
    // Basic cross-prompt mutation: x <- 5 updates the persisted value
    CHECK(sessionProducesOutput({ "let mut x = 0", "x <- 5", "print x" }, "5"));
}

TEST_CASE("IRGenerator.FSharp.session_mutable_self_referential_mutation")
{
    // Self-referential mutation: x <- x + 10
    CHECK(sessionProducesOutput({ "let mut x = 0", "x <- x + 10", "print x" }, "10"));
}

TEST_CASE("IRGenerator.FSharp.session_mutable_multiple_mutations")
{
    // Multiple mutations across prompts: last value wins
    CHECK(sessionProducesOutput({ "let mut x = 0", "x <- 1", "x <- 2", "print x" }, "2"));
}

TEST_CASE("IRGenerator.FSharp.session_mutable_other_vars_unaffected")
{
    // Mutating one variable does not affect another
    CHECK(sessionProducesOutput({ "let mut x = 1", "let mut y = 2", "x <- 10", "print y" }, "2"));
}

// =============================================================================
// Phase 2 — Bug Fixes
// =============================================================================

TEST_CASE("IRGenerator.FSharp.logical_or_true_false")
{
    CHECK(executesWithOutput("let x = true || false; print x", "true"));
}

TEST_CASE("IRGenerator.FSharp.logical_or_false_true")
{
    CHECK(executesWithOutput("let x = false || true; print x", "true"));
}

TEST_CASE("IRGenerator.FSharp.print_bool_true")
{
    CHECK(executesWithOutput("let x = true; print x", "true"));
}

TEST_CASE("IRGenerator.FSharp.print_bool_false")
{
    CHECK(executesWithOutput("let x = false; print x", "false"));
}

TEST_CASE("IRGenerator.FSharp.logical_or_false_false")
{
    CHECK(executesWithOutput("let x = false || false; print x", "false"));
}

TEST_CASE("IRGenerator.FSharp.logical_or_true_true")
{
    CHECK(executesWithOutput("let x = true || true; print x", "true"));
}

TEST_CASE("IRGenerator.FSharp.logical_and_true_false")
{
    CHECK(executesWithOutput("let x = true && false; print x", "false"));
}

TEST_CASE("IRGenerator.FSharp.logical_and_true_true")
{
    CHECK(executesWithOutput("let x = true && true; print x", "true"));
}

// =============================================================================
// Phase 2 — String Concatenation
// =============================================================================

TEST_CASE("IRGenerator.FSharp.string_concat_basic")
{
    CHECK(executesWithOutput(R"(let s = "hello" + " world"; print s)", "hello world"));
}

TEST_CASE("IRGenerator.FSharp.string_concat_number_right")
{
    CHECK(executesWithOutput(R"(let s = "count: " + 42; print s)", "count: 42"));
}

TEST_CASE("IRGenerator.FSharp.string_concat_number_left")
{
    CHECK(executesWithOutput(R"(let s = 42 + " items"; print s)", "42 items"));
}

// =============================================================================
// Phase 2 — If-Then-Else Expressions
// =============================================================================

TEST_CASE("IRGenerator.FSharp.if_expr_true")
{
    CHECK(executesWithOutput("let x = if true then 1 else 2; print x", "1"));
}

TEST_CASE("IRGenerator.FSharp.if_expr_false")
{
    CHECK(executesWithOutput("let x = if false then 1 else 2; print x", "2"));
}

TEST_CASE("IRGenerator.FSharp.if_expr_string_true")
{
    CHECK(executesWithOutput("print (if true then \"Hello\" else \"World\")", "Hello"));
}

TEST_CASE("IRGenerator.FSharp.if_expr_string_false")
{
    CHECK(executesWithOutput("print (if false then \"Hello\" else \"World\")", "World"));
}

TEST_CASE("IRGenerator.FSharp.if_expr_float_true")
{
    CHECK(executesWithOutput("print (if true then 3.14 else 2.81)", "3.14"));
}

TEST_CASE("IRGenerator.FSharp.if_expr_float_false")
{
    CHECK(executesWithOutput("print (if false then 3.14 else 2.81)", "2.81"));
}

TEST_CASE("IRGenerator.FSharp.if_expr_with_comparison")
{
    CHECK(executesWithOutput("let id x = x; print (id 5)", "5"));
    CHECK(executesWithOutput("print (7)", "7"));
    CHECK(executesWithOutput("let id x = x; print (id (7))", "7"));
    CHECK(executesWithOutput("let id x = x; print (id (0 - 7))", "-7"));
    CHECK(executesWithOutput("let abs x = if x < 0 then 0 - x else x; print (abs (0 - 7))", "7"));
}

TEST_CASE("IRGenerator.FSharp.if_expr_nested")
{
    CHECK(executesWithOutput(
        "let clamp x = if x < 0 then 0 else if x > 100 then 100 else x; print (clamp 50)", "50"));
}

TEST_CASE("IRGenerator.FSharp.if_expr_nested_low")
{
    CHECK(executesWithOutput(
        "let clamp x = if x < 0 then 0 else if x > 100 then 100 else x; print (clamp (0 - 5))", "0"));
}

TEST_CASE("IRGenerator.FSharp.if_expr_nested_high")
{
    CHECK(executesWithOutput(
        "let clamp x = if x < 0 then 0 else if x > 100 then 100 else x; print (clamp 200)", "100"));
}

TEST_CASE("IRGenerator.FSharp.if_expr_recursive_factorial")
{
    CHECK(executesWithOutput(
        "let rec fact n acc = if n <= 1 then acc else fact (n - 1) (acc * n); print (fact 5 1)", "120"));
}

// =============================================================================
// Phase 2 — If-then-else branch type mismatch
// =============================================================================

TEST_CASE("IRGenerator.FSharp.if_expr_type_mismatch_int_vs_str")
{
    CHECK(!generatesIRSuccessfully("let x = if true then 42 else \"oops\""));
}

TEST_CASE("IRGenerator.FSharp.if_expr_type_mismatch_bool_vs_int")
{
    CHECK(!generatesIRSuccessfully("let x = if true then true else 42"));
}

TEST_CASE("IRGenerator.FSharp.if_expr_type_mismatch_float_vs_str")
{
    CHECK(!generatesIRSuccessfully("let x = if true then 3.14 else \"oops\""));
}

TEST_CASE("IRGenerator.FSharp.if_expr_same_type_int")
{
    CHECK(generatesIRSuccessfully("let x = if true then 42 else 0"));
}

TEST_CASE("IRGenerator.FSharp.if_expr_same_type_str")
{
    CHECK(generatesIRSuccessfully("let x = if true then \"hello\" else \"world\""));
}

TEST_CASE("IRGenerator.FSharp.if_expr_type_mismatch_option_vs_result")
{
    CHECK(!generatesIRSuccessfully("let x = if true then Some 1 else Ok 2"));
}

TEST_CASE("IRGenerator.FSharp.if_expr_type_mismatch_option_vs_tuple")
{
    CHECK(!generatesIRSuccessfully("let x = if true then Some 1 else (1, 2)"));
}

TEST_CASE("IRGenerator.FSharp.if_expr_type_mismatch_option_vs_int")
{
    CHECK(!generatesIRSuccessfully("let x = if true then Some 1 else 42"));
}

TEST_CASE("IRGenerator.FSharp.if_expr_same_type_option")
{
    CHECK(generatesIRSuccessfully("let x = if true then Some 1 else None"));
}

TEST_CASE("IRGenerator.FSharp.if_expr_same_type_result")
{
    CHECK(generatesIRSuccessfully("let x = if true then Ok 1 else Error \"fail\""));
}

TEST_CASE("IRGenerator.FSharp.if_expr_type_mismatch_some_int_vs_some_str")
{
    CHECK(!generatesIRSuccessfully("let x = if true then Some 42 else Some \"hello\""));
}

TEST_CASE("IRGenerator.FSharp.if_expr_type_mismatch_ok_int_vs_ok_str")
{
    CHECK(!generatesIRSuccessfully("let x = if true then Ok 42 else Ok \"hello\""));
}

TEST_CASE("IRGenerator.FSharp.if_expr_type_mismatch_error_int_vs_error_str")
{
    CHECK(!generatesIRSuccessfully("let x = if true then Error 1 else Error \"fail\""));
}

TEST_CASE("IRGenerator.FSharp.if_expr_type_mismatch_tuple_int_int_vs_int_str")
{
    CHECK(!generatesIRSuccessfully("let x = if true then (1, 2) else (1, \"two\")"));
}

TEST_CASE("IRGenerator.FSharp.if_expr_same_type_tuple")
{
    CHECK(generatesIRSuccessfully("let x = if true then (1, 2) else (3, 4)"));
}

// =============================================================================
// Phase 2 — Mutable Variable Assignment
// =============================================================================

TEST_CASE("IRGenerator.FSharp.mutable_assignment_basic")
{
    CHECK(executesWithOutput("let mut x = 1; x <- 42; print x", "42"));
}

TEST_CASE("IRGenerator.FSharp.mutable_assignment_increment")
{
    CHECK(executesWithOutput("let mut counter = 0; counter <- counter + 1; print counter", "1"));
}

TEST_CASE("IRGenerator.FSharp.immutable_assignment_error")
{
    // Assigning to an immutable variable should produce an error
    CHECK(generatesIRWithError("let x = 1; x <- 42", "Cannot assign to immutable variable"));
}

// =============================================================================
// Phase 2 — Tuple Expressions and Pattern Matching
// =============================================================================

TEST_CASE("IRGenerator.FSharp.tuple_fst")
{
    CHECK(executesWithOutput("let fst t = match t with | (a, _) -> a; let t = (1, 2); print (fst t)", "1"));
}

TEST_CASE("IRGenerator.FSharp.tuple_snd")
{
    CHECK(executesWithOutput("let snd t = match t with | (_, b) -> b; let t = (1, 2); print (snd t)", "2"));
}

TEST_CASE("IRGenerator.FSharp.tuple_pattern_match")
{
    CHECK(executesWithOutput("let t = (3, 4); let r = match t with | (a, b) -> a + b; print r", "7"));
}

TEST_CASE("IRGenerator.FSharp.tuple_3_elements")
{
    CHECK(executesWithOutput("let fst t = match t with | (a, _) -> a; let t = (10, 20, 30); print (fst t)",
                             "10"));
}

TEST_CASE("IRGenerator.FSharp.tuple_swap")
{
    CHECK(executesWithOutput("let fst t = match t with | (a, _) -> a; "
                             "let snd t = match t with | (_, b) -> b; "
                             "let swap t = match t with | (a, b) -> (b, a); "
                             "let s = swap (1, 2); print (fst s); print (snd s)",
                             "21"));
}

TEST_CASE("IRGenerator.FSharp.tuple_sum_pair")
{
    CHECK(
        executesWithOutput("let sum_pair t = match t with | (a, b) -> a + b; print (sum_pair (3, 4))", "7"));
}

TEST_CASE("IRGenerator.FSharp.tuple_mixed_types")
{
    // Extract numeric element from mixed tuple via pattern matching
    CHECK(executesWithOutput(R"(let t = (42, "hello"); let r = match t with | (a, b) -> a; print r)", "42"));
}

TEST_CASE("IRGenerator.FSharp.tuple_numeric_snd")
{
    // Extract second numeric element via pattern matching
    CHECK(executesWithOutput(R"(let t = ("world", 99); let r = match t with | (_, b) -> b; print r)", "99"));
}

TEST_CASE("IRGenerator.FSharp.tuple_snd_via_function")
{
    // Extract numeric second element via user-defined function
    CHECK(executesWithOutput("let get_snd t = match t with | (_, b) -> b; print (get_snd (1, 42))", "42"));
}

// =============================================================================
// Phase 2 — Standard Library Builtins
// =============================================================================

TEST_CASE("IRGenerator.FSharp.builtin_string_length")
{
    CHECK(executesWithOutput(R"(print (string_length "hello"))", "5"));
}

TEST_CASE("IRGenerator.FSharp.builtin_string_of_int")
{
    CHECK(executesWithOutput(R"(print (string_of_int 42))", "42"));
}

TEST_CASE("IRGenerator.FSharp.builtin_int_of_string")
{
    CHECK(executesWithOutput(R"(let n = int_of_string "7"; print (n + 3))", "10"));
}

TEST_CASE("IRGenerator.FSharp.builtin_not_true")
{
    CHECK(executesWithOutput("let x = not true; print x", "false"));
}

TEST_CASE("IRGenerator.FSharp.builtin_not_false")
{
    CHECK(executesWithOutput("let x = not false; print x", "true"));
}

TEST_CASE("IRGenerator.FSharp.builtin_pipeline_string_of_int_length")
{
    CHECK(executesWithOutput("let r = 42 |> string_of_int |> string_length; print r", "2"));
}

TEST_CASE("IRGenerator.FSharp.tuple_fst_direct")
{
    CHECK(executesWithOutput("let fst t = match t with | (a, _) -> a; print (fst (1, 2))", "1"));
}

TEST_CASE("IRGenerator.FSharp.tuple_snd_direct")
{
    CHECK(executesWithOutput("let snd t = match t with | (_, b) -> b; print (snd (1, 2))", "2"));
}

// =============================================================================
// Float Literal and Arithmetic Tests
// =============================================================================

TEST_CASE("IRGenerator.FSharp.float_literal")
{
    CHECK(executesWithOutput("print 3.14", "3.14"));
}

TEST_CASE("IRGenerator.FSharp.float_arithmetic_add")
{
    CHECK(executesWithOutput("print (1.5 + 2.5)", "4"));
}

TEST_CASE("IRGenerator.FSharp.float_arithmetic_sub")
{
    CHECK(executesWithOutput("print (5.0 - 2.5)", "2.5"));
}

TEST_CASE("IRGenerator.FSharp.float_arithmetic_mul")
{
    CHECK(executesWithOutput("print (3.0 * 2.5)", "7.5"));
}

TEST_CASE("IRGenerator.FSharp.float_arithmetic_div")
{
    CHECK(executesWithOutput("print (7.0 / 2.0)", "3.5"));
}

TEST_CASE("IRGenerator.FSharp.float_negation")
{
    CHECK(executesWithOutput("print (-3.14)", "-3.14"));
}

TEST_CASE("IRGenerator.FSharp.float_mixed_int_promotion")
{
    CHECK(executesWithOutput("print (1 + 2.5)", "3.5"));
}

TEST_CASE("IRGenerator.FSharp.float_mixed_int_promotion_reverse")
{
    CHECK(executesWithOutput("print (2.5 + 1)", "3.5"));
}

TEST_CASE("IRGenerator.FSharp.float_comparison_lt")
{
    CHECK(executesWithOutput("let r = if 2.5 < 3.0 then 1 else 0; print r", "1"));
}

TEST_CASE("IRGenerator.FSharp.float_comparison_gt")
{
    CHECK(executesWithOutput("let r = if 3.0 > 2.5 then 1 else 0; print r", "1"));
}

TEST_CASE("IRGenerator.FSharp.float_string_concat")
{
    CHECK(executesWithOutput(R"(print ("pi=" + 3.14))", "pi=3.14"));
}

TEST_CASE("IRGenerator.FSharp.float_in_function")
{
    CHECK(executesWithOutput("let double x = x * 2.0; print (double 3.5)", "7"));
}

TEST_CASE("IRGenerator.FSharp.float_let_binding")
{
    CHECK(executesWithOutput("let pi = 3.14; print pi", "3.14"));
}

TEST_CASE("IRGenerator.FSharp.float_pow")
{
    CHECK(executesWithOutput("print (2.0 ** 3.0)", "8"));
}

TEST_CASE("IRGenerator.FSharp.float_mod")
{
    CHECK(executesWithOutput("print (7.5 % 2.0)", "1.5"));
}

// =============================================================================
// Non-entry-block alloca regression tests (DISCARD underflow fix)
// =============================================================================

TEST_CASE("IRGenerator.FSharp.exec_multiple_function_calls")
{
    // Calling a function twice triggers the second call in a non-entry block.
    // Previously this created allocas outside the entry block, causing DISCARD underflow.
    auto const source = R"(
        let double x = x * 2
        print (double 3)
        print " "
        print (double 5)
    )";
    CHECK(executeSourceAndGetOutput(source) == "6 10");
}

TEST_CASE("IRGenerator.FSharp.exec_function_call_after_match")
{
    // Function application in a match merge block exercises non-entry-block codegen.
    auto const source = R"(
        let id x = x
        let r = match Some 1 with | Some n -> n | None -> 0
        print (id r)
    )";
    CHECK(executeSourceAndGetOutput(source) == "1");
}

TEST_CASE("IRGenerator.FSharp.exec_chained_try_with")
{
    // Multiple try-with expressions with error handlers.
    auto const source = R"(
        let getErr x = Error x
        let r1 = try getErr 1 with | Error e -> e
        let r2 = try getErr 2 with | Error e -> e
        print r1
        print " "
        print r2
    )";
    CHECK(executeSourceAndGetOutput(source) == "1 2");
}

TEST_CASE("IRGenerator.FSharp.exec_try_with_multiple_handlers")
{
    // try-with with multiple error pattern arms.
    auto const source = R"(
        let getErr x = Error x
        let r1 = try getErr 1 with | Error 1 -> 10 | Error 2 -> 20 | Error _ -> 0
        let r2 = try getErr 2 with | Error 1 -> 10 | Error 2 -> 20 | Error _ -> 0
        let r3 = try getErr 99 with | Error 1 -> 10 | Error 2 -> 20 | Error _ -> 0
        print r1
        print " "
        print r2
        print " "
        print r3
    )";
    CHECK(executeSourceAndGetOutput(source) == "10 20 0");
}

TEST_CASE("IRGenerator.FSharp.exec_try_with_guards")
{
    // try-with with guard conditions on error patterns.
    auto const source = R"(
        let getErr x = Error x
        let r = try getErr 5 with | Error e when e > 3 -> 100 | Error _ -> 0
        print r
    )";
    CHECK(executeSourceAndGetOutput(source) == "100");
}

TEST_CASE("IRGenerator.FSharp.exec_mixed_function_calls_and_try")
{
    // Combination of match, function calls, and error handling.
    auto const source = R"(
        let id x = x
        let getErr x = Error x
        let r1 = match Some 42 with | Some n -> n | None -> 0
        let r2 = try getErr 99 with | Error e -> e
        print (id r1)
        print " "
        print (id r2)
    )";
    CHECK(executeSourceAndGetOutput(source) == "42 99");
}

TEST_CASE("IRGenerator.FSharp.exec_function_call_inside_match_arm")
{
    // Function application within a match arm body.
    auto const source = R"(
        let double x = x * 2
        let r = match Some 5 with | Some n -> double n | None -> 0
        print r
    )";
    CHECK(executeSourceAndGetOutput(source) == "10");
}

// =============================================================================
// Multi-line Expression Tests
// =============================================================================

TEST_CASE("IRGenerator.FSharp.exec_multiline_match")
{
    // Multi-line match expression with arms on separate lines
    auto const source = "let grade score = match score with\n"
                        "    | s when s >= 90 -> 1\n"
                        "    | s when s >= 80 -> 2\n"
                        "    | _ -> 3\n"
                        "print (grade 95)\n"
                        "print (grade 85)\n"
                        "print (grade 60)\n";
    CHECK(executeSourceAndGetOutput(source) == "123");
}

TEST_CASE("IRGenerator.FSharp.exec_multiline_match_followed_by_statement")
{
    // Multi-line match followed by another let binding
    auto const source = "let classify n = match n with\n"
                        "    | 0 -> 10\n"
                        "    | _ -> 20\n"
                        "print (classify 0)\n"
                        "print (classify 5)\n";
    CHECK(executeSourceAndGetOutput(source) == "1020");
}

TEST_CASE("IRGenerator.FSharp.exec_multiline_if_then_else")
{
    auto const source = "let r = if true\n"
                        "    then 42\n"
                        "    else 0\n"
                        "print r\n";
    CHECK(executeSourceAndGetOutput(source) == "42");
}

TEST_CASE("IRGenerator.FSharp.exec_multiline_if_then_else.2")
{
    auto const source = "let r = if true then\n"
                        "            42\n"
                        "        else\n"
                        "            0\n"
                        "print r\n";
    CHECK(executeSourceAndGetOutput(source) == "42");
}

// =============================================================================
// Numeric Base Literal Execution Tests
// =============================================================================

TEST_CASE("IRGenerator.FSharp.hex_literal")
{
    CHECK(executeSourceAndGetOutput("print 0xFF") == "255");
}

TEST_CASE("IRGenerator.FSharp.octal_literal")
{
    CHECK(executeSourceAndGetOutput("print 0o755") == "493");
}

TEST_CASE("IRGenerator.FSharp.binary_literal")
{
    CHECK(executeSourceAndGetOutput("print 0b1010") == "10");
}

TEST_CASE("IRGenerator.FSharp.scientific_notation")
{
    CHECK(executeSourceAndGetOutput("print 1e10") == "1e+10");
}

TEST_CASE("IRGenerator.FSharp.scientific_notation_decimal")
{
    CHECK(executeSourceAndGetOutput("print 2.5e2") == "250");
}

TEST_CASE("IRGenerator.FSharp.hex_arithmetic")
{
    CHECK(executeSourceAndGetOutput("print (0xFF + 1)") == "256");
}

TEST_CASE("IRGenerator.FSharp.octal_arithmetic")
{
    CHECK(executeSourceAndGetOutput("print (0o10 + 1)") == "9");
}

TEST_CASE("IRGenerator.FSharp.binary_arithmetic")
{
    CHECK(executeSourceAndGetOutput("print (0b1000 - 1)") == "7");
}

// =============================================================================
// Comment Execution Tests
// =============================================================================

TEST_CASE("IRGenerator.FSharp.comment_hash")
{
    CHECK(executeSourceAndGetOutput("print 42 # ignored") == "42");
}

TEST_CASE("IRGenerator.FSharp.comment_slash")
{
    CHECK(executeSourceAndGetOutput("let x = 42 // comment\nprint x") == "42");
}

TEST_CASE("IRGenerator.FSharp.comment_block")
{
    CHECK(executeSourceAndGetOutput("let x = (* inline *) 42\nprint x") == "42");
}

// =============================================================================
// Bare top-level F# function calls
// =============================================================================

TEST_CASE("IRGenerator.FSharp.bare_call_same_prompt")
{
    CHECK(executeSourceAndGetOutput("let f x = print x\nf 42") == "42");
}

TEST_CASE("IRGenerator.FSharp.bare_call_multi_arg")
{
    CHECK(executeSourceAndGetOutput("let add x y = print (x + y)\nadd 3 4") == "7");
}

TEST_CASE("IRGenerator.FSharp.bare_call_lambda_binding")
{
    CHECK(executeSourceAndGetOutput("let f = fun x -> print x\nf 99") == "99");
}

TEST_CASE("IRGenerator.FSharp.bare_call_cross_prompt")
{
    CHECK(sessionProducesOutput({ "let f x = print x", "f 42" }, "42"));
}

TEST_CASE("IRGenerator.FSharp.bare_call_mutual_recursion")
{
    CHECK(executeSourceAndGetOutput("let rec isEven n =\n"
                                    "  match n with | 0 -> 1 | _ -> isOdd (n - 1)\n"
                                    "and isOdd n =\n"
                                    "  match n with | 0 -> 0 | _ -> isEven (n - 1)\n"
                                    "print (isEven 4)")
          == "1");
}

TEST_CASE("IRGenerator.FSharp.bare_call_mutual_recursion_bare")
{
    CHECK(executeSourceAndGetOutput("let rec isEven n =\n"
                                    "  match n with | 0 -> 1 | _ -> isOdd (n - 1)\n"
                                    "and isOdd n =\n"
                                    "  match n with | 0 -> 0 | _ -> isEven (n - 1)\n"
                                    "print (isOdd 3)")
          == "1");
}

// =============================================================================
// F# Type Annotation Tests — Positive (correct annotations)
// =============================================================================

TEST_CASE("IRGenerator.FSharp.TypeAnnotation.let_int")
{
    CHECK(executeSourceAndGetOutput("let x: int = 42\nprint x") == "42");
}

TEST_CASE("IRGenerator.FSharp.TypeAnnotation.let_str")
{
    CHECK(executeSourceAndGetOutput("let s: str = \"hello\"\nprint s") == "hello");
}

TEST_CASE("IRGenerator.FSharp.TypeAnnotation.let_bool")
{
    CHECK(executeSourceAndGetOutput("let b: bool = true\nprint b") == "true");
}

TEST_CASE("IRGenerator.FSharp.TypeAnnotation.let_float")
{
    CHECK(executeSourceAndGetOutput("let f: float = 3.14\nprint f") == "3.14");
}

TEST_CASE("IRGenerator.FSharp.TypeAnnotation.function_params_and_return")
{
    CHECK(executeSourceAndGetOutput("let add (x: int) (y: int): int = x + y\nprint (add 3 4)") == "7");
}

TEST_CASE("IRGenerator.FSharp.TypeAnnotation.function_single_param")
{
    CHECK(executeSourceAndGetOutput("let double (x: int): int = x * 2\nprint (double 5)") == "10");
}

TEST_CASE("IRGenerator.FSharp.TypeAnnotation.lambda_param")
{
    CHECK(executeSourceAndGetOutput("let f = fun (x: int) -> x + 1\nprint (f 5)") == "6");
}

TEST_CASE("IRGenerator.FSharp.TypeAnnotation.mixed_annotated_and_bare")
{
    CHECK(executeSourceAndGetOutput("let f (x: int) y = x + y\nprint (f 3 4)") == "7");
}

TEST_CASE("IRGenerator.FSharp.TypeAnnotation.return_type_only")
{
    CHECK(executeSourceAndGetOutput("let double x: int = x * 2\nprint (double 5)") == "10");
}

TEST_CASE("IRGenerator.FSharp.TypeAnnotation.recursive_function")
{
    CHECK(executeSourceAndGetOutput("let rec fact (n: int) (acc: int): int =\n"
                                    "  match n with\n"
                                    "  | 0 -> acc\n"
                                    "  | _ -> fact (n - 1) (acc * n)\n"
                                    "print (fact 5 1)")
          == "120");
}

TEST_CASE("IRGenerator.FSharp.TypeAnnotation.let_in")
{
    CHECK(executeSourceAndGetOutput("let result = let x: int = 10 in x + 5\nprint result") == "15");
}

TEST_CASE("IRGenerator.FSharp.TypeAnnotation.option_type")
{
    CHECK(generatesIRSuccessfully("let x: option<int> = Some 42\nprint x"));
}

TEST_CASE("IRGenerator.FSharp.TypeAnnotation.result_type")
{
    CHECK(generatesIRSuccessfully("let x: result<int, str> = Ok 42\nprint x"));
}

TEST_CASE("IRGenerator.FSharp.TypeAnnotation.lambda_multiple_params")
{
    CHECK(executeSourceAndGetOutput("let f = fun (x: int) (y: int) -> x + y\nprint (f 3 4)") == "7");
}

TEST_CASE("IRGenerator.FSharp.TypeAnnotation.partial_application")
{
    CHECK(executeSourceAndGetOutput("let add (x: int) (y: int): int = x + y\n"
                                    "let add3 = add 3\n"
                                    "print (add3 4)")
          == "7");
}

TEST_CASE("IRGenerator.FSharp.TypeAnnotation.session_persistence")
{
    CHECK(sessionProducesOutput({ "let add (x: int) (y: int): int = x + y", "print (add 10 20)" }, "30"));
}

TEST_CASE("IRGenerator.FSharp.TypeAnnotation.session_handler_recursive")
{
    // Typed recursive function defined in one prompt, called in the next (compiled as handler)
    CHECK(sessionProducesOutput({ "let rec countdown (n: int): int = if n <= 0 then 0 else countdown (n - 1)",
                                  "print (countdown 5)" },
                                "0"));
}

TEST_CASE("IRGenerator.FSharp.TypeAnnotation.session_handler_with_closure")
{
    // Typed function capturing a persisted value binding (closure via handler)
    CHECK(sessionProducesOutput(
        { "let offset = 10", "let addOffset (x: int): int = x + offset", "print (addOffset 5)" }, "15"));
}

TEST_CASE("IRGenerator.FSharp.TypeAnnotation.session_handler_multiple_calls")
{
    // Typed handler function called across multiple prompts
    CHECK(sessionProducesOutput(
        { "let square (x: int): int = x * x", "print (square 4)", "print (square 7)" }, "49"));
}

// =============================================================================
// F# Type Annotation Tests — Negative (type mismatches)
// =============================================================================

TEST_CASE("IRGenerator.FSharp.TypeAnnotation.mismatch_int_vs_str")
{
    CHECK(!generatesIRSuccessfully("let x: int = \"hello\""));
}

TEST_CASE("IRGenerator.FSharp.TypeAnnotation.mismatch_str_vs_int")
{
    CHECK(!generatesIRSuccessfully("let x: str = 42"));
}

TEST_CASE("IRGenerator.FSharp.TypeAnnotation.mismatch_bool_vs_int")
{
    CHECK(!generatesIRSuccessfully("let x: bool = 42"));
}

TEST_CASE("IRGenerator.FSharp.TypeAnnotation.mismatch_float_vs_str")
{
    CHECK(!generatesIRSuccessfully("let x: float = \"hello\""));
}

TEST_CASE("IRGenerator.FSharp.TypeAnnotation.mismatch_param_type")
{
    CHECK(!generatesIRSuccessfully("let add (x: int) (y: int) = x + y\nadd \"a\" 1"));
}

TEST_CASE("IRGenerator.FSharp.TypeAnnotation.mismatch_return_type")
{
    CHECK(!generatesIRSuccessfully("let f (x: int): str = x + 1\nf 1"));
}

TEST_CASE("IRGenerator.FSharp.TypeAnnotation.mismatch_int_vs_bool")
{
    CHECK(!generatesIRSuccessfully("let x: int = true"));
}

TEST_CASE("IRGenerator.FSharp.TypeAnnotation.mismatch_int_vs_float")
{
    CHECK(!generatesIRSuccessfully("let x: int = 3.14"));
}

// ============================================================================
// F#-Style String Interpolation ($"...")
// ============================================================================

TEST_CASE("IRGenerator.FSharp.fstring_basic")
{
    CHECK(executeSourceAndGetOutput(R"(print $"Hello, World")") == "Hello, World");
}

TEST_CASE("IRGenerator.FSharp.fstring_variable")
{
    CHECK(executeSourceAndGetOutput(R"(let name = "User"; print $"Hello, {name}")") == "Hello, User");
}

TEST_CASE("IRGenerator.FSharp.fstring_arithmetic")
{
    CHECK(executeSourceAndGetOutput(R"(print $"Sum is {3 + 4}")") == "Sum is 7");
}

TEST_CASE("IRGenerator.FSharp.fstring_multiple_holes")
{
    CHECK(executeSourceAndGetOutput(R"(let a = 1; let b = 2; print $"a={a}, b={b}")") == "a=1, b=2");
}

TEST_CASE("IRGenerator.FSharp.fstring_conditional")
{
    CHECK(executeSourceAndGetOutput(R"(print $"val: {if true then 1 else 0}")") == "val: 1");
}

TEST_CASE("IRGenerator.FSharp.fstring_number_conversion")
{
    CHECK(executeSourceAndGetOutput(R"(print $"n={42}")") == "n=42");
}

TEST_CASE("IRGenerator.FSharp.fstring_float_conversion")
{
    CHECK(executeSourceAndGetOutput(R"(print $"pi={3.14}")") == "pi=3.14");
}

TEST_CASE("IRGenerator.FSharp.fstring_bool_conversion")
{
    CHECK(executeSourceAndGetOutput(R"(print $"flag={true}")") == "flag=true");
}

TEST_CASE("IRGenerator.FSharp.fstring_escaped_braces")
{
    CHECK(executeSourceAndGetOutput(R"(print $"{{literal}}")") == "{literal}");
}

TEST_CASE("IRGenerator.FSharp.fstring_empty")
{
    CHECK(executeSourceAndGetOutput(R"(print $"")") == "");
}

TEST_CASE("IRGenerator.FSharp.fstring_no_holes")
{
    CHECK(executeSourceAndGetOutput(R"(print $"just text")") == "just text");
}

TEST_CASE("IRGenerator.FSharp.fstring_function_application")
{
    CHECK(executeSourceAndGetOutput(R"(let f x = x * 2; print $"result: {f 5}")") == "result: 10");
}

TEST_CASE("IRGenerator.FSharp.fstring_adjacent_holes")
{
    CHECK(executeSourceAndGetOutput(R"(print $"{1}{2}{3}")") == "123");
}

TEST_CASE("IRGenerator.FSharp.fstring_as_value")
{
    CHECK(executeSourceAndGetOutput(R"(let s = $"hello"; print s)") == "hello");
}

TEST_CASE("IRGenerator.FSharp.fstring_concatenation")
{
    CHECK(executeSourceAndGetOutput(R"(let a = $"a"; let b = $"b"; print (a + b))") == "ab");
}

TEST_CASE("IRGenerator.FSharp.fstring_nested_string")
{
    CHECK(executeSourceAndGetOutput(R"(print $"say {"hello"}")") == "say hello");
}

TEST_CASE("IRGenerator.FSharp.fstring_pipeline_in_hole")
{
    CHECK(executeSourceAndGetOutput(R"(print $"len={"hello" |> string_length}")") == "len=5");
}

// =============================================================================
// env builtin — returns option<string> for environment variables
// =============================================================================

TEST_CASE("IRGenerator.FSharp.env_existing_var")
{
    auto& rt = TestRuntime::instance();
    rt.clearMockEnvVars();
    rt.setMockEnvVar("HOME", "/home/user");
    // env returns Some when var exists — match Some arm is taken
    CHECK(executeSourceAndGetOutput(
              R"(let r = match env "HOME" with | Some v -> "found" | None -> "none"; print r)")
          == "found");
    rt.clearMockEnvVars();
}

TEST_CASE("IRGenerator.FSharp.env_missing_var")
{
    auto& rt = TestRuntime::instance();
    rt.clearMockEnvVars();
    // env returns None when var is missing
    CHECK(executeSourceAndGetOutput(
              R"(let r = match env "NONEXISTENT" with | Some v -> "found" | None -> "none"; print r)")
          == "none");
}

TEST_CASE("IRGenerator.FSharp.env_match_some")
{
    auto& rt = TestRuntime::instance();
    rt.clearMockEnvVars();
    rt.setMockEnvVar("PATH", "/usr/bin");
    CHECK(executeSourceAndGetOutput(
              R"(let r = match env "PATH" with | Some v -> "present" | None -> "missing"; print r)")
          == "present");
    rt.clearMockEnvVars();
}

TEST_CASE("IRGenerator.FSharp.env_match_none")
{
    auto& rt = TestRuntime::instance();
    rt.clearMockEnvVars();
    CHECK(executeSourceAndGetOutput(
              R"(let r = match env "MISSING" with | Some v -> "found" | None -> "fallback"; print r)")
          == "fallback");
}

TEST_CASE("IRGenerator.FSharp.env_in_let")
{
    auto& rt = TestRuntime::instance();
    rt.clearMockEnvVars();
    rt.setMockEnvVar("HOME", "/home/user");
    // Bind env result to a variable, then match
    CHECK(
        executeSourceAndGetOutput(
            R"(let home = env "HOME"; let r = match home with | Some v -> "found" | None -> "none"; print r)")
        == "found");
    rt.clearMockEnvVars();
}

TEST_CASE("IRGenerator.FSharp.env_empty_value")
{
    auto& rt = TestRuntime::instance();
    rt.clearMockEnvVars();
    rt.setMockEnvVar("EMPTY", "");
    // Empty string value should still be Some (var exists)
    CHECK(executeSourceAndGetOutput(
              R"(let r = match env "EMPTY" with | Some v -> "exists" | None -> "none"; print r)")
          == "exists");
    rt.clearMockEnvVars();
}

TEST_CASE("IRGenerator.FSharp.env_multiple_vars")
{
    auto& rt = TestRuntime::instance();
    rt.clearMockEnvVars();
    rt.setMockEnvVar("A", "alpha");
    rt.setMockEnvVar("B", "beta");
    CHECK(executeSourceAndGetOutput(
              R"(let r = match env "A" with | Some v -> "a_found" | None -> "?"; print r)")
          == "a_found");
    CHECK(executeSourceAndGetOutput(
              R"(let r = match env "B" with | Some v -> "b_found" | None -> "?"; print r)")
          == "b_found");
    rt.clearMockEnvVars();
}

TEST_CASE("IRGenerator.FSharp.env_in_function")
{
    auto& rt = TestRuntime::instance();
    rt.clearMockEnvVars();
    rt.setMockEnvVar("X", "42");
    // Use env inside a user-defined function
    CHECK(
        executeSourceAndGetOutput(
            R"(let getEnv key = env key; let r = match getEnv "X" with | Some v -> "found" | None -> "none"; print r)")
        == "found");
    rt.clearMockEnvVars();
}

TEST_CASE("IRGenerator.FSharp.env_with_default")
{
    auto& rt = TestRuntime::instance();
    rt.clearMockEnvVars();
    // Pattern: provide a default value for missing env vars
    CHECK(executeSourceAndGetOutput(R"(let r = match env "MISSING" with | Some v -> 1 | None -> 0; print r)")
          == "0");
    rt.setMockEnvVar("KEY", "val");
    CHECK(executeSourceAndGetOutput(R"(let r = match env "KEY" with | Some v -> 1 | None -> 0; print r)")
          == "1");
    rt.clearMockEnvVars();
}

TEST_CASE("IRGenerator.FSharp.env_ir_generation")
{
    auto& rt = TestRuntime::instance();
    rt.clearMockEnvVars();
    // Verify env IR generates successfully
    CHECK(generatesIRSuccessfully(R"(let x = env "HOME")"));
    CHECK(generatesIRSuccessfully(R"(let x = env "HOME"; let r = match x with | Some v -> 1 | None -> 0)"));
}

TEST_CASE("IRGenerator.FSharp.env_question_operator")
{
    auto& rt = TestRuntime::instance();
    rt.clearMockEnvVars();
    rt.setMockEnvVar("HOME", "/home/user");
    // ? operator on env result: unwraps Some
    CHECK(executesWithOutput("let unwrap opt = opt?; let r = unwrap (env \"HOME\")", ""));
    // Verify the function generates IR successfully with env and ?
    CHECK(generatesIRSuccessfully(R"(let unwrap opt = opt?; let r = unwrap (env "HOME"))"));
    rt.clearMockEnvVars();
}

TEST_CASE("IRGenerator.FSharp.env_question_operator_none")
{
    auto& rt = TestRuntime::instance();
    rt.clearMockEnvVars();
    // ? operator on None from env: propagates None
    CHECK(executesSuccessfully(R"(let unwrap opt = opt?; let r = unwrap (env "MISSING"))"));
    rt.clearMockEnvVars();
}

// ============================================================================
// Phase 1 Foundation: Unit type ()
// ============================================================================

TEST_CASE("IRGenerator.FSharp.unit_type")
{
    CHECK(executesSuccessfully("let x = ()"));
}

TEST_CASE("IRGenerator.FSharp.unit_print")
{
    CHECK(executesWithOutput("print ()", "0"));
}

TEST_CASE("IRGenerator.FSharp.unit_let_binding")
{
    CHECK(executesWithOutput("let x = (); print x", "0"));
}

// ============================================================================
// Phase 1 Foundation: String repetition
// ============================================================================

TEST_CASE("IRGenerator.FSharp.string_repeat_basic")
{
    CHECK(executesWithOutput(R"(print ("ha" * 3))", "hahaha"));
}

TEST_CASE("IRGenerator.FSharp.string_repeat_commutative")
{
    CHECK(executesWithOutput(R"(print (3 * "ab"))", "ababab"));
}

TEST_CASE("IRGenerator.FSharp.string_repeat_zero")
{
    CHECK(executesWithOutput(R"(print ("x" * 0))", ""));
}

TEST_CASE("IRGenerator.FSharp.string_repeat_one")
{
    CHECK(executesWithOutput(R"(print ("y" * 1))", "y"));
}

// ============================================================================
// Phase 1 Foundation: Block scopes
// ============================================================================

TEST_CASE("IRGenerator.FSharp.block_scope_basic")
{
    CHECK(executesWithOutput("let r = { let x = 10; x + 5 }; print r", "15"));
}

TEST_CASE("IRGenerator.FSharp.block_scope_multiple_lets")
{
    CHECK(executesWithOutput("let r = { let x = 1; let y = 2; x + y }; print r", "3"));
}

TEST_CASE("IRGenerator.FSharp.block_scope_isolation")
{
    CHECK(executesWithOutput("let x = 1; let r = { let x = 99; x }; print r; print x", "991"));
}

// ============================================================================
// Phase 1 Foundation: Function composition >> and <<
// ============================================================================

TEST_CASE("IRGenerator.FSharp.compose_forward")
{
    CHECK(executesWithOutput("let double x = x * 2; let inc x = x + 1; let f = double >> inc; print (f 5)",
                             "11"));
}

TEST_CASE("IRGenerator.FSharp.compose_backward")
{
    CHECK(executesWithOutput("let double x = x * 2; let inc x = x + 1; let f = inc << double; print (f 5)",
                             "11"));
}

TEST_CASE("IRGenerator.FSharp.compose_chain")
{
    CHECK(executesWithOutput(
        "let a x = x + 1; let b x = x * 2; let c x = x - 3; let f = a >> b >> c; print (f 5)", "9"));
}

// ============================================================================
// Phase 1 Foundation: Tuple destructuring in let
// ============================================================================

TEST_CASE("IRGenerator.FSharp.tuple_destructure_basic")
{
    CHECK(executesWithOutput("let (x, y) = (10, 20); print x; print y", "1020"));
}

TEST_CASE("IRGenerator.FSharp.tuple_destructure_from_binding")
{
    CHECK(executesWithOutput("let t = (3, 4); let (a, b) = t; print (a + b)", "7"));
}

TEST_CASE("IRGenerator.FSharp.tuple_destructure_3_elements")
{
    CHECK(executesWithOutput("let (a, b, c) = (1, 2, 3); print (a + b + c)", "6"));
}

TEST_CASE("IRGenerator.FSharp.tuple_destructure_let_in")
{
    CHECK(executesWithOutput("let r = let (x, y) = (5, 6) in x * y; print r", "30"));
}

// =============================================================================
// F# List Tests
// =============================================================================

TEST_CASE("IRGenerator.FSharp.list_empty")
{
    REQUIRE(executesSuccessfully("let x = []"));
}

TEST_CASE("IRGenerator.FSharp.list_single")
{
    REQUIRE(executesSuccessfully("let x = [1]"));
}

TEST_CASE("IRGenerator.FSharp.list_multiple")
{
    REQUIRE(executesSuccessfully("let x = [1; 2; 3]"));
}

TEST_CASE("IRGenerator.FSharp.list_print_empty")
{
    CHECK(executesWithOutput("print []", "[]"));
}

TEST_CASE("IRGenerator.FSharp.list_print_single")
{
    CHECK(executesWithOutput("print [42]", "[42]"));
}

TEST_CASE("IRGenerator.FSharp.list_print_multiple")
{
    CHECK(executesWithOutput("print [1; 2; 3]", "[1; 2; 3]"));
}

TEST_CASE("IRGenerator.FSharp.list_print_binding")
{
    CHECK(executesWithOutput("let x = [10; 20; 30]; print x", "[10; 20; 30]"));
}

// =============================================================================
// F# Cons (::) Operator Tests
// =============================================================================

TEST_CASE("IRGenerator.FSharp.cons_single")
{
    CHECK(executesWithOutput("print (1 :: [])", "[1]"));
}

TEST_CASE("IRGenerator.FSharp.cons_multiple")
{
    CHECK(executesWithOutput("print (1 :: 2 :: 3 :: [])", "[1; 2; 3]"));
}

TEST_CASE("IRGenerator.FSharp.cons_prepend_to_list")
{
    CHECK(executesWithOutput("print (0 :: [1; 2; 3])", "[0; 1; 2; 3]"));
}

TEST_CASE("IRGenerator.FSharp.cons_with_binding")
{
    CHECK(executesWithOutput("let xs = [2; 3]; print (1 :: xs)", "[1; 2; 3]"));
}

// =============================================================================
// F# List Pattern Matching Tests
// =============================================================================

TEST_CASE("IRGenerator.FSharp.match_empty_list")
{
    CHECK(executesWithOutput("let x = []; let r = match x with | [] -> \"empty\" | _ -> \"not\"; print r",
                             "empty"));
}

TEST_CASE("IRGenerator.FSharp.match_cons_head")
{
    auto src = "let x = [1; 2; 3]; let r = match x with | h :: t -> h | _ -> 0; print r";
    auto actual = executeSourceAndGetOutput(src);
    INFO("Actual output: '" << actual << "'");
    CHECK(actual == "1");
}

TEST_CASE("IRGenerator.FSharp.match_nonempty_vs_empty")
{
    CHECK(executesWithOutput("let x = [1]; let r = match x with | [] -> 0 | h :: _ -> h; print r", "1"));
}

TEST_CASE("IRGenerator.FSharp.match_cons_second_element")
{
    CHECK(executesWithOutput(
        "let x = [1; 2; 3]; let r = match x with | _ :: second :: _ -> second | _ -> 0; print r", "2"));
}

TEST_CASE("IRGenerator.FSharp.match_fixed_length_list")
{
    CHECK(executesWithOutput("let x = [10; 20]; let r = match x with | [a; b] -> a + b | _ -> 0; print r",
                             "30"));
}

TEST_CASE("IRGenerator.FSharp.match_recursive_simple")
{
    // Simple 1-parameter recursive function with list pattern matching
    CHECK(executesWithOutput("let rec f xs = match xs with | [] -> 0 | h :: t -> f t; "
                             "print (f [1; 2])",
                             "0"));
}

TEST_CASE("IRGenerator.FSharp.match_recursive_sum")
{
    // Use accumulator style since non-tail recursion (h + sum t) is not supported
    CHECK(executesWithOutput(
        "let rec sumAcc acc xs = match xs with | [] -> acc | h :: t -> sumAcc (acc + h) t; "
        "print (sumAcc 0 [1; 2; 3; 4])",
        "10"));
}

TEST_CASE("IRGenerator.FSharp.match_recursive_length")
{
    // Use accumulator style since non-tail recursion (1 + len t) is not supported
    CHECK(executesWithOutput(
        "let rec lenAcc acc xs = match xs with | [] -> acc | _ :: t -> lenAcc (acc + 1) t; "
        "print (lenAcc 0 [10; 20; 30])",
        "3"));
}

// =============================================================================
// List range expressions [start..end] and [start..step..end]
// =============================================================================

TEST_CASE("IRGenerator.FSharp.list_range_simple")
{
    CHECK(executesWithOutput("print [1..3]", "[1; 2; 3]"));
}

TEST_CASE("IRGenerator.FSharp.list_range_single")
{
    CHECK(executesWithOutput("print [1..1]", "[1]"));
}

TEST_CASE("IRGenerator.FSharp.list_range_empty")
{
    CHECK(executesWithOutput("print [5..3]", "[]"));
}

TEST_CASE("IRGenerator.FSharp.list_range_step")
{
    CHECK(executesWithOutput("print [1..2..7]", "[1; 3; 5; 7]"));
}

TEST_CASE("IRGenerator.FSharp.list_range_negative_step")
{
    CHECK(executesWithOutput("print [10..-1..7]", "[10; 9; 8; 7]"));
}

TEST_CASE("IRGenerator.FSharp.list_range_even_step")
{
    CHECK(executesWithOutput("print [0..2..10]", "[0; 2; 4; 6; 8; 10]"));
}

TEST_CASE("IRGenerator.FSharp.list_range_with_pattern_matching")
{
    CHECK(executesWithOutput(
        "let rec sumAcc acc xs = match xs with | [] -> acc | h :: t -> sumAcc (acc + h) t\n"
        "print (sumAcc 0 [1..5])",
        "15"));
}

TEST_CASE("IRGenerator.FSharp.list_range_let_binding")
{
    CHECK(executesWithOutput("let xs = [1..5]\n"
                             "print xs",
                             "[1; 2; 3; 4; 5]"));
}

// =============================================================================
// List concatenation operator @
// =============================================================================

TEST_CASE("IRGenerator.FSharp.list_concat_basic")
{
    CHECK(executesWithOutput("print ([1; 2] @ [3; 4])", "[1; 2; 3; 4]"));
}

TEST_CASE("IRGenerator.FSharp.list_concat_empty_left")
{
    CHECK(executesWithOutput("print ([] @ [1; 2])", "[1; 2]"));
}

TEST_CASE("IRGenerator.FSharp.list_concat_empty_right")
{
    CHECK(executesWithOutput("print ([1; 2] @ [])", "[1; 2]"));
}

TEST_CASE("IRGenerator.FSharp.list_concat_both_empty")
{
    CHECK(executesWithOutput("print ([] @ [])", "[]"));
}

TEST_CASE("IRGenerator.FSharp.list_concat_chained")
{
    CHECK(executesWithOutput("print ([1] @ [2] @ [3])", "[1; 2; 3]"));
}

TEST_CASE("IRGenerator.FSharp.list_concat_with_cons")
{
    CHECK(executesWithOutput("print (0 :: [1; 2] @ [3; 4])", "[0; 1; 2; 3; 4]"));
}

TEST_CASE("IRGenerator.FSharp.list_concat_with_range")
{
    CHECK(executesWithOutput("print ([1..3] @ [4..6])", "[1; 2; 3; 4; 5; 6]"));
}

TEST_CASE("IRGenerator.FSharp.list_concat_bound_variables")
{
    CHECK(executesWithOutput("let a = [1; 2]; let b = [3; 4]; print (a @ b)", "[1; 2; 3; 4]"));
}

TEST_CASE("IRGenerator.FSharp.list_concat_single_elements")
{
    CHECK(executesWithOutput("print ([1] @ [2])", "[1; 2]"));
}

// =============================================================================
// Additional list execution edge cases
// =============================================================================

TEST_CASE("IRGenerator.FSharp.list_nested_in_option")
{
    // List inside Some/None
    CHECK(executesWithOutput("let x = Some [1; 2; 3]; match x with | Some xs -> print xs | None -> print 0",
                             "[1; 2; 3]"));
}

TEST_CASE("IRGenerator.FSharp.list_nested_in_result")
{
    // List inside Ok/Error
    CHECK(executesWithOutput("let x = Ok [10; 20]; match x with | Ok xs -> print xs | Error _ -> print 0",
                             "[10; 20]"));
}

TEST_CASE("IRGenerator.FSharp.list_in_tuple")
{
    // Tuple containing a list
    CHECK(executesSuccessfully("let t = ([1; 2], 42)"));
}

TEST_CASE("IRGenerator.FSharp.list_as_function_arg")
{
    // Pass list to function and operate on it
    CHECK(executesWithOutput("let first xs = match xs with | h :: _ -> h | [] -> 0; print (first [5; 6; 7])",
                             "5"));
}

TEST_CASE("IRGenerator.FSharp.list_as_function_return")
{
    // Return a list from a function
    CHECK(executesWithOutput("let make x = x :: []; print (make 99)", "[99]"));
}

TEST_CASE("IRGenerator.FSharp.list_match_wildcard_tail")
{
    // Wildcard in tail position
    CHECK(
        executesWithOutput("let x = [1; 2; 3]; let r = match x with | h :: _ -> h | [] -> 0; print r", "1"));
}

TEST_CASE("IRGenerator.FSharp.list_match_wildcard_head")
{
    // Wildcard in head position
    CHECK(executesWithOutput(
        "let rec last xs = match xs with | [x] -> x | _ :: t -> last t | [] -> 0; print (last [10; 20; 30])",
        "30"));
}

TEST_CASE("IRGenerator.FSharp.list_match_three_elements")
{
    // Fixed 3-element list pattern
    CHECK(executesWithOutput(
        "let x = [1; 2; 3]; let r = match x with | [a; b; c] -> a + b + c | _ -> 0; print r", "6"));
}

TEST_CASE("IRGenerator.FSharp.list_range_large")
{
    // Larger range
    CHECK(executesWithOutput(
        "let rec sumAcc acc xs = match xs with | [] -> acc | h :: t -> sumAcc (acc + h) t\n"
        "print (sumAcc 0 [1..10])",
        "55"));
}

TEST_CASE("IRGenerator.FSharp.list_range_step_not_aligned")
{
    // Step doesn't evenly divide range — should stop before overshooting
    CHECK(executesWithOutput("print [1..3..10]", "[1; 4; 7; 10]"));
}

TEST_CASE("IRGenerator.FSharp.list_range_step_overshoot")
{
    // Step overshoots end — should include only start (aligned to valid elements)
    CHECK(executesWithOutput("print [1..10..5]", "[1]"));
}

TEST_CASE("IRGenerator.FSharp.list_range_step_exact_fit")
{
    // Step exactly fits the range
    CHECK(executesWithOutput("print [0..5..10]", "[0; 5; 10]"));
}

TEST_CASE("IRGenerator.FSharp.list_cons_right_associative")
{
    // Verify :: is right-associative: 1 :: 2 :: 3 :: [] = 1 :: (2 :: (3 :: []))
    CHECK(executesWithOutput("print (1 :: 2 :: 3 :: [])", "[1; 2; 3]"));
}

TEST_CASE("IRGenerator.FSharp.list_concat_right_associative")
{
    // Verify @ is right-associative: [1] @ [2] @ [3] = [1] @ ([2] @ [3])
    CHECK(executesWithOutput("print ([1] @ [2] @ [3])", "[1; 2; 3]"));
}

TEST_CASE("IRGenerator.FSharp.list_cons_precedence_over_concat")
{
    // :: has same precedence as @, both right-associative
    // 0 :: [1; 2] @ [3; 4] should parse as 0 :: ([1; 2] @ [3; 4])
    CHECK(executesWithOutput("print (0 :: [1; 2] @ [3; 4])", "[0; 1; 2; 3; 4]"));
}

TEST_CASE("IRGenerator.FSharp.list_recursive_count_elements")
{
    // Count elements greater than a threshold
    CHECK(executesWithOutput("let rec countAbove threshold acc xs = match xs with\n"
                             "  | [] -> acc\n"
                             "  | h :: t -> if h > threshold then countAbove threshold (acc + 1) t else "
                             "countAbove threshold acc t\n"
                             "print (countAbove 3 0 [1; 5; 2; 7; 3; 8])",
                             "3"));
}

TEST_CASE("IRGenerator.FSharp.list_recursive_find_max")
{
    // Find maximum element in a list
    CHECK(executesWithOutput("let rec maxAcc acc xs = match xs with\n"
                             "  | [] -> acc\n"
                             "  | h :: t -> if h > acc then maxAcc h t else maxAcc acc t\n"
                             "print (maxAcc 0 [3; 7; 2; 9; 1])",
                             "9"));
}

TEST_CASE("IRGenerator.FSharp.list_rec_reverse_cons_in_tail_arg")
{
    // ConsExpr in tail call arguments — previously a known limitation.
    CHECK(executesWithOutput("let rec revAcc acc xs = match xs with\n"
                             "  | [] -> acc\n"
                             "  | h :: t -> revAcc (h :: acc) t\n"
                             "print (revAcc [] [1; 2; 3])",
                             "[3; 2; 1]"));
}

TEST_CASE("IRGenerator.FSharp.list_rec_reverse_longer")
{
    // Longer list to exercise multiple loop iterations with cons construction.
    CHECK(executesWithOutput("let rec revAcc acc xs = match xs with\n"
                             "  | [] -> acc\n"
                             "  | h :: t -> revAcc (h :: acc) t\n"
                             "print (revAcc [] [1; 2; 3; 4; 5])",
                             "[5; 4; 3; 2; 1]"));
}

TEST_CASE("IRGenerator.FSharp.list_match_nested_cons_depth3")
{
    // Match on 3-deep nested cons (a :: b :: c :: rest)
    CHECK(executesWithOutput("let x = [10; 20; 30; 40; 50]\n"
                             "let r = match x with | a :: b :: c :: _ -> a + b + c | _ -> 0\n"
                             "print r",
                             "60"));
}

TEST_CASE("IRGenerator.FSharp.list_match_fallthrough_to_empty")
{
    // Non-matching cons pattern falls through to empty pattern
    CHECK(executesWithOutput("let x = []; let r = match x with | h :: t -> h | [] -> 99; print r", "99"));
}

TEST_CASE("IRGenerator.FSharp.list_match_fallthrough_to_wildcard")
{
    // Non-matching specific pattern falls through to wildcard
    CHECK(executesWithOutput("let x = [1; 2; 3]\n"
                             "let r = match x with | [a] -> a | _ -> 42\n"
                             "print r",
                             "42"));
}

TEST_CASE("IRGenerator.FSharp.list_concat_preserves_order")
{
    // Verify concatenation preserves element order from both sides
    CHECK(executesWithOutput(
        "let rec sumAcc acc xs = match xs with | [] -> acc | h :: t -> sumAcc (acc + h) t\n"
        "let xs = [1; 2] @ [3; 4] @ [5; 6]\n"
        "print (sumAcc 0 xs)",
        "21"));
}

TEST_CASE("IRGenerator.FSharp.list_range_negative_descend_empty")
{
    // Negative step but start < end — should be empty
    CHECK(executesWithOutput("print [1..-1..5]", "[]"));
}

// =============================================================================
// List comprehensions
// =============================================================================

TEST_CASE("IRGenerator.FSharp.list_comprehension_basic")
{
    CHECK(executesWithOutput("print [for x in [1;2;3] -> x]", "[1; 2; 3]"));
}

TEST_CASE("IRGenerator.FSharp.list_comprehension_transform")
{
    CHECK(executesWithOutput("print [for x in [1;2;3] -> x * 2]", "[2; 4; 6]"));
}

TEST_CASE("IRGenerator.FSharp.list_comprehension_with_filter")
{
    CHECK(executesWithOutput("print [for x in [1;2;3;4;5] when x > 2 -> x]", "[3; 4; 5]"));
}

TEST_CASE("IRGenerator.FSharp.list_comprehension_filter_and_transform")
{
    CHECK(executesWithOutput("print [for x in [1;2;3;4;5] when x > 2 -> x * 10]", "[30; 40; 50]"));
}

TEST_CASE("IRGenerator.FSharp.list_comprehension_empty_source")
{
    CHECK(executesWithOutput("print [for x in [] -> x * 2]", "[]"));
}

TEST_CASE("IRGenerator.FSharp.list_comprehension_all_filtered")
{
    CHECK(executesWithOutput("print [for x in [1;2;3] when x > 10 -> x]", "[]"));
}

TEST_CASE("IRGenerator.FSharp.list_comprehension_from_range")
{
    CHECK(executesWithOutput("print [for x in [1..5] -> x * x]", "[1; 4; 9; 16; 25]"));
}

TEST_CASE("IRGenerator.FSharp.list_comprehension_let_binding")
{
    CHECK(executesWithOutput("let s = [for x in [1;2;3;4] -> x * x]\nprint s", "[1; 4; 9; 16]"));
}

TEST_CASE("IRGenerator.FSharp.list_comprehension_single_element")
{
    CHECK(executesWithOutput("print [for x in [42] -> x + 1]", "[43]"));
}

TEST_CASE("IRGenerator.FSharp.list_comprehension_preserves_order")
{
    CHECK(executesWithOutput("print [for x in [5;4;3;2;1] -> x]", "[5; 4; 3; 2; 1]"));
}

// =============================================================================
// Compile-time error tests (should fail IR generation)
// =============================================================================

TEST_CASE("IRGenerator.FSharp.list_if_type_mismatch_list_vs_int")
{
    // if-then-else branches: list vs int
    CHECK(generatesIRWithError("let x = if true then [1; 2] else 42", "Type mismatch in if-then-else"));
}

TEST_CASE("IRGenerator.FSharp.list_if_type_mismatch_list_vs_str")
{
    // if-then-else branches: list vs string
    CHECK(
        generatesIRWithError("let x = if true then [1; 2] else \"hello\"", "Type mismatch in if-then-else"));
}

TEST_CASE("IRGenerator.FSharp.list_if_type_mismatch_list_vs_option")
{
    // if-then-else branches: list vs option
    CHECK(generatesIRWithError("let x = if true then [1; 2] else Some 42", "Type mismatch in if-then-else"));
}

TEST_CASE("IRGenerator.FSharp.list_if_type_mismatch_list_vs_tuple")
{
    // if-then-else branches: list vs tuple
    CHECK(generatesIRWithError("let x = if true then [1; 2] else (1, 2)", "Type mismatch in if-then-else"));
}

TEST_CASE("IRGenerator.FSharp.list_if_same_type_both_lists")
{
    // if-then-else branches: both lists — should succeed
    CHECK(generatesIRSuccessfully("let x = if true then [1; 2] else [3; 4]"));
}

TEST_CASE("IRGenerator.FSharp.list_if_same_type_empty_and_nonempty")
{
    // if-then-else branches: empty list and non-empty list — should succeed (both List type)
    CHECK(generatesIRSuccessfully("let x = if true then [] else [1; 2]"));
}

TEST_CASE("IRGenerator.FSharp.list_function_arity_too_many_args")
{
    // Function taking one list arg, called with two args
    CHECK(generatesIRWithError("let f xs = match xs with | [] -> 0 | _ -> 1; f [1] [2]",
                               "expects 1 argument, got 2"));
}

TEST_CASE("IRGenerator.FSharp.list_recursive_non_tail_position")
{
    // Non-tail recursive call (h + (sum t)) — should error
    CHECK(generatesIRWithError(
        "let rec sum xs = match xs with | [] -> 0 | h :: t -> h + (sum t); print (sum [1; 2])",
        "Non-tail recursive call detected"));
}

// =============================================================================
// Higher-Order Function (HOF) Tests
// =============================================================================

TEST_CASE("IRGenerator.FSharp.hof_basic")
{
    // Basic HOF: pass a named function as an argument
    CHECK(executesWithOutput("let double x = x * 2\nlet apply f x = f x\nprint (apply double 5)", "10"));
}

TEST_CASE("IRGenerator.FSharp.hof_lambda_argument")
{
    // HOF with lambda argument
    CHECK(executesWithOutput("let apply f x = f x\nprint (apply (fun x -> x * 2) 5)", "10"));
}

TEST_CASE("IRGenerator.FSharp.hof_twice")
{
    // Apply function to result of itself
    CHECK(executesWithOutput("let double x = x * 2\nlet twice f x = f (f x)\nprint (twice double 3)", "12"));
}

TEST_CASE("IRGenerator.FSharp.hof_compose")
{
    // Function composition
    CHECK(executesWithOutput("let double x = x * 2\nlet add1 x = x + 1\nlet compose f g x = f (g x)\nprint "
                             "(compose double add1 5)",
                             "12"));
}

TEST_CASE("IRGenerator.FSharp.hof_partial_application")
{
    // Partial application of HOF
    CHECK(executesWithOutput("let double x = x * 2\nlet apply f x = f x\nlet g = apply double\nprint (g 5)",
                             "10"));
}

TEST_CASE("IRGenerator.FSharp.hof_returning_closure")
{
    // HOF returning closure that captures function ref
    CHECK(executesWithOutput(
        "let double x = x * 2\nlet make_caller f = fun x -> f x\nlet g = make_caller double\nprint (g 5)",
        "10"));
}

TEST_CASE("IRGenerator.FSharp.hof_multiple_function_args")
{
    // Multiple function arguments
    CHECK(executesWithOutput(
        "let double x = x * 2\nlet add1 x = x + 1\nlet apply2 f g x = g (f x)\nprint (apply2 double add1 3)",
        "7"));
}

TEST_CASE("IRGenerator.FSharp.hof_pipeline")
{
    // HOF in pipeline
    CHECK(executesWithOutput("let double x = x * 2\nlet apply f x = f x\nprint (5 |> apply double)", "10"));
}

TEST_CASE("IRGenerator.FSharp.hof_string_function")
{
    // HOF with string function
    CHECK(executesWithOutput("let shout s = s + \"!\"\nlet apply f x = f x\nprint (apply shout \"hello\")",
                             "hello!"));
}

TEST_CASE("IRGenerator.FSharp.hof_nested_partial_application")
{
    // Nested partial application: (add 1) passed as function arg
    CHECK(executesWithOutput("let add x y = x + y\nlet apply f x = f x\nlet g = apply (add 1)\nprint (g 5)",
                             "6"));
}

TEST_CASE("IRGenerator.FSharp.hof_function_alias")
{
    // Function alias through HOF (identity returning a function)
    CHECK(executesWithOutput("let double x = x * 2\nlet id f = f\nlet g = id double\nprint (g 5)", "10"));
}

// =============================================================================
// Record Types
// =============================================================================

TEST_CASE("IRGenerator.FSharp.record_type_def_and_field_access")
{
    // Basic record type definition, creation, and field access
    CHECK(executesWithOutput("type Point = { x: int; y: int }\n"
                             "let p = { x = 10; y = 20 }\n"
                             "print p.x",
                             "10"));
}

TEST_CASE("IRGenerator.FSharp.record_field_access_second_field")
{
    CHECK(executesWithOutput("type Point = { x: int; y: int }\n"
                             "let p = { x = 10; y = 20 }\n"
                             "print p.y",
                             "20"));
}

TEST_CASE("IRGenerator.FSharp.record_field_arithmetic")
{
    // Use record fields in arithmetic
    CHECK(executesWithOutput("type Point = { x: int; y: int }\n"
                             "let p = { x = 3; y = 4 }\n"
                             "print (p.x + p.y)",
                             "7"));
}

TEST_CASE("IRGenerator.FSharp.record_update_basic")
{
    // Record update creates a new record with one field changed
    CHECK(executesWithOutput("type Person = { name: int; age: int }\n"
                             "let p = { name = 1; age = 30 }\n"
                             "let q = { p with age = 31 }\n"
                             "print q.age",
                             "31"));
}

TEST_CASE("IRGenerator.FSharp.record_update_preserves_unchanged")
{
    // Record update preserves fields that aren't overridden
    CHECK(executesWithOutput("type Person = { name: int; age: int }\n"
                             "let p = { name = 42; age = 30 }\n"
                             "let q = { p with age = 31 }\n"
                             "print q.name",
                             "42"));
}

TEST_CASE("IRGenerator.FSharp.record_pattern_match_punning")
{
    // Record pattern matching with field punning
    CHECK(executesWithOutput("type Point = { x: int; y: int }\n"
                             "let p = { x = 5; y = 10 }\n"
                             "let r = match p with | { x; y } -> x + y\n"
                             "print r",
                             "15"));
}

TEST_CASE("IRGenerator.FSharp.record_pattern_match_explicit_binding")
{
    // Record pattern matching with explicit variable binding
    CHECK(executesWithOutput("type Point = { x: int; y: int }\n"
                             "let p = { x = 5; y = 10 }\n"
                             "let r = match p with | { x = a; y = b } -> a * b\n"
                             "print r",
                             "50"));
}

TEST_CASE("IRGenerator.FSharp.record_passed_to_function")
{
    // Record passed as function argument
    CHECK(executesWithOutput("type Point = { x: int; y: int }\n"
                             "let sum_point p = p.x + p.y\n"
                             "let p = { x = 3; y = 7 }\n"
                             "print (sum_point p)",
                             "10"));
}

TEST_CASE("IRGenerator.FSharp.record_returned_from_function")
{
    // Record created and returned from a function
    CHECK(executesWithOutput("type Point = { x: int; y: int }\n"
                             "let make_point a b = { x = a; y = b }\n"
                             "let p = make_point 100 200\n"
                             "print p.x",
                             "100"));
}

TEST_CASE("IRGenerator.FSharp.record_update_with_expression")
{
    // Record update using an expression involving the original record
    CHECK(executesWithOutput("type Counter = { value: int }\n"
                             "let c = { value = 10 }\n"
                             "let c2 = { c with value = c.value + 1 }\n"
                             "print c2.value",
                             "11"));
}

TEST_CASE("IRGenerator.FSharp.record_multiple_types")
{
    // Multiple record types in the same program
    CHECK(executesWithOutput("type Point = { x: int; y: int }\n"
                             "type Size = { w: int; h: int }\n"
                             "let p = { x = 1; y = 2 }\n"
                             "let s = { w = 10; h = 20 }\n"
                             "print (p.x + s.w)",
                             "11"));
}

TEST_CASE("IRGenerator.FSharp.record_let_destructure")
{
    // Record destructuring in let binding
    CHECK(executesWithOutput("type Point = { x: int; y: int }\n"
                             "let p = { x = 7; y = 8 }\n"
                             "let { x; y } = p\n"
                             "print (x + y)",
                             "15"));
}

TEST_CASE("IRGenerator.FSharp.record_string_field_access")
{
    // String field access via dot notation
    CHECK(executesWithOutput("type Person = { name: str; age: int }\n"
                             "let p = { name = \"Alice\"; age = 30 }\n"
                             "print p.name",
                             "Alice"));
}

TEST_CASE("IRGenerator.FSharp.record_string_field_destructure")
{
    // String field via let destructuring
    CHECK(executesWithOutput("type Person = { name: str; age: int }\n"
                             "let p = { name = \"Bob\"; age = 25 }\n"
                             "let { name; age } = p\n"
                             "print name",
                             "Bob"));
}

TEST_CASE("IRGenerator.FSharp.record_string_field_match")
{
    // String field via pattern matching
    CHECK(executesWithOutput("type Person = { name: str; age: int }\n"
                             "let p = { name = \"Charlie\"; age = 35 }\n"
                             "print (match p with | { name; age } -> name)",
                             "Charlie"));
}

TEST_CASE("IRGenerator.FSharp.record_string_field_fstring")
{
    // String field used in f-string interpolation
    CHECK(executesWithOutput("type Person = { name: str; age: int }\n"
                             "let p = { name = \"Diana\"; age = 28 }\n"
                             "print $\"Hello, {p.name}!\"",
                             "Hello, Diana!"));
}

TEST_CASE("IRGenerator.FSharp.record_println_whole_record")
{
    // Printing a whole record should produce formatted output
    CHECK(executesWithOutput("type Point = { x: int; y: int }\n"
                             "let p = { x = 3; y = 4 }\n"
                             "print p",
                             "{ x = 3; y = 4 }"));
}

TEST_CASE("IRGenerator.FSharp.record_update_println")
{
    // Print a record created via record update
    CHECK(executesWithOutput("type Point = { x: int; y: int }\n"
                             "let p1 = { x = 3; y = 4 }\n"
                             "let p2 = { p1 with x = p1.x + 1; y = p1.y + 2 }\n"
                             "print p2",
                             "{ x = 4; y = 6 }"));
}

TEST_CASE("IRGenerator.FSharp.record_update_field_access_after")
{
    // Access fields of a record created via record update
    CHECK(executesWithOutput("type Point = { x: int; y: int }\n"
                             "let p1 = { x = 3; y = 4 }\n"
                             "let p2 = { p1 with x = p1.x + 1; y = p1.y + 2 }\n"
                             "print p2.x\n"
                             "print p2.y",
                             "46"));
}

TEST_CASE("IRGenerator.FSharp.record_println_with_string_fields")
{
    // Printing a record with string fields should show string values, not pointers
    CHECK(executesWithOutput("type Person = { name: str; age: int }\n"
                             "let p = { name = \"Alice\"; age = 30 }\n"
                             "print p",
                             "{ name = Alice; age = 30 }"));
}

TEST_CASE("IRGenerator.FSharp.record_destructure_then_update")
{
    // Let destructuring followed by record update on the same type
    CHECK(executesWithOutput("type Point = { x: int; y: int }\n"
                             "let p1 = { x = 3; y = 4 }\n"
                             "let { x; y } = p1\n"
                             "let p2 = { p1 with x = p1.x + 1; y = p1.y + 2 }\n"
                             "print p2",
                             "{ x = 4; y = 6 }"));
}

TEST_CASE("IRGenerator.FSharp.record_destructure_two_types_then_update")
{
    // Let destructuring on one record type followed by record update on another type
    CHECK(executesWithOutput("type Person = { name: str; age: int }\n"
                             "let alice = { name = \"Alice\"; age = 30 }\n"
                             "let { name; age } = alice\n"
                             "type Point = { x: int; y: int }\n"
                             "let p1 = { x = 3; y = 4 }\n"
                             "let p2 = { p1 with x = p1.x + 1; y = p1.y + 2 }\n"
                             "print p2",
                             "{ x = 4; y = 6 }"));
}

TEST_CASE("IRGenerator.FSharp.record_update_with_inlined_match_function")
{
    // Inlined untyped function with match creates blocks during record update value codegen.
    CHECK(executesWithOutput("type Point = { x: int; y: int }\n"
                             "let double n = match n with | 0 -> 0 | n -> n * 2\n"
                             "let p1 = { x = 3; y = 4 }\n"
                             "let p2 = { p1 with x = double (p1.x) }\n"
                             "print p2",
                             "{ x = 6; y = 4 }"));
}

TEST_CASE("IRGenerator.FSharp.record_update_with_multiple_inlined_match_functions")
{
    // Multiple update fields each calling inlined match functions.
    CHECK(executesWithOutput("type Point = { x: int; y: int }\n"
                             "let double n = match n with | 0 -> 0 | n -> n * 2\n"
                             "let p1 = { x = 3; y = 4 }\n"
                             "let p2 = { p1 with x = double (p1.x); y = double (p1.y) }\n"
                             "print p2",
                             "{ x = 6; y = 8 }"));
}
