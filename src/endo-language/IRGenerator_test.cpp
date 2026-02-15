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
// F# Let Export Tests
// =============================================================================

TEST_CASE("IRGenerator.FSharp.let_export_number")
{
    // let export binds value and exports as environment variable
    CHECK(executeSourceAndGetOutput("let export X = 42; print X") == "42");
}

TEST_CASE("IRGenerator.FSharp.let_export_string")
{
    CHECK(executeSourceAndGetOutput("let export X = \"hello\"; print X") == "hello");
}

TEST_CASE("IRGenerator.FSharp.let_export_expression")
{
    CHECK(executeSourceAndGetOutput("let export X = 1 + 2; print X") == "3");
}

TEST_CASE("IRGenerator.FSharp.let_export_bool")
{
    CHECK(executeSourceAndGetOutput("let export X = true; print X") == "true");
}

TEST_CASE("IRGenerator.FSharp.let_export_mut")
{
    // let export mut should work
    CHECK(executeSourceAndGetOutput("let export mut X = 10; print X") == "10");
}

TEST_CASE("IRGenerator.FSharp.let_export_rec_error")
{
    // let export rec is disallowed
    CHECK(generatesIRWithError("let export rec f x = x + 1; f 1", "functions cannot be exported"));
}

TEST_CASE("IRGenerator.FSharp.let_export_function_error")
{
    // let export with function parameters is disallowed
    CHECK(generatesIRWithError("let export f x = x + 1; f 1",
                               "'let export' cannot be used with function definitions"));
}

TEST_CASE("IRGenerator.FSharp.let_export_lambda_error")
{
    // let export with lambda is disallowed
    CHECK(generatesIRWithError("let export f = fun x -> x + 1; f 1",
                               "'let export' cannot be used with lambda expressions"));
}

TEST_CASE("IRGenerator.FSharp.let_export_ir_generation")
{
    // Basic IR generation succeeds
    REQUIRE(generatesIRSuccessfully("let export X = 42"));
    REQUIRE(generatesIRSuccessfully("let export mut Y = \"test\""));
}

TEST_CASE("IRGenerator.FSharp.let_export_list_error")
{
    CHECK(generatesIRWithError("let export X = [1; 2; 3]", "'let export' requires a scalar type"));
}

TEST_CASE("IRGenerator.FSharp.let_export_tuple_error")
{
    CHECK(generatesIRWithError("let export X = (1, 2)", "'let export' requires a scalar type"));
}

TEST_CASE("IRGenerator.FSharp.let_export_option_error")
{
    CHECK(generatesIRWithError("let export X = Some 42", "'let export' requires a scalar type"));
}

TEST_CASE("IRGenerator.FSharp.let_export_result_error")
{
    CHECK(generatesIRWithError("let export X = Ok 42", "'let export' requires a scalar type"));
}

TEST_CASE("IRGenerator.FSharp.let_export_list_join")
{
    CHECK(executeSourceAndGetOutput(R"(let export X = ["/bin"; "/usr/bin"] |> join ":"; print X)")
          == "/bin:/usr/bin");
}

TEST_CASE("IRGenerator.FSharp.let_export_mut_updates_env_on_mutation")
{
    auto& testRuntime = endo::test::TestRuntime::instance();
    CHECK(executeSourceAndGetOutput("let export mut X = 10; X <- 100; print X") == "100");
    CHECK(testRuntime.env().at("X") == "100");
}

TEST_CASE("IRGenerator.FSharp.let_export_mut_env_initial_value")
{
    auto& testRuntime = endo::test::TestRuntime::instance();
    CHECK(executeSourceAndGetOutput("let export mut Y = 42; print Y") == "42");
    CHECK(testRuntime.env().at("Y") == "42");
}

TEST_CASE("IRGenerator.FSharp.let_export_mut_multiple_mutations")
{
    auto& testRuntime = endo::test::TestRuntime::instance();
    CHECK(executeSourceAndGetOutput("let export mut Z = 1; Z <- 2; Z <- 3; print Z") == "3");
    CHECK(testRuntime.env().at("Z") == "3");
}

TEST_CASE("IRGenerator.FSharp.let_export_immutable_env")
{
    auto& testRuntime = endo::test::TestRuntime::instance();
    CHECK(executeSourceAndGetOutput("let export W = 99; print W") == "99");
    CHECK(testRuntime.env().at("W") == "99");
}

TEST_CASE("IRGenerator.FSharp.let_export_mut_string_mutation")
{
    auto& testRuntime = endo::test::TestRuntime::instance();
    CHECK(executeSourceAndGetOutput(R"(let export mut S = "hello"; S <- "world"; print S)") == "world");
    CHECK(testRuntime.env().at("S") == "world");
}

TEST_CASE("IRGenerator.FSharp.list_multiline_join")
{
    CHECK(executeSourceAndGetOutput("let r = [\n  \"a\";\n  \"b\";\n  \"c\"\n] |> join \",\"\nprint r")
          == "a,b,c");
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
// F# Function-Compiled Tests (UCALL/URET)
// =============================================================================

TEST_CASE("IRGenerator.FSharp.function_typed_identity")
{
    // Identity function with type annotation compiles as function (UCALL/URET)
    CHECK(executeSourceAndGetOutput("let id (x: int) = x; print (id 42)") == "42");
}

TEST_CASE("IRGenerator.FSharp.function_typed_arithmetic")
{
    // Integer arithmetic with typed params compiles as function
    CHECK(executeSourceAndGetOutput("let add (x: int) (y: int) = x + y; print (add 3 4)") == "7");
    CHECK(executeSourceAndGetOutput("let sq (x: int) = x * x; print (sq 5)") == "25");
}

TEST_CASE("IRGenerator.FSharp.function_typed_if_then_else")
{
    // Branching with typed params in compiled function
    CHECK(executeSourceAndGetOutput("let abs (x: int) = if x < 0 then 0 - x else x; print (abs (0 - 7))")
          == "7");
    CHECK(executeSourceAndGetOutput("let abs (x: int) = if x < 0 then 0 - x else x; print (abs 3)") == "3");
}

TEST_CASE("IRGenerator.FSharp.function_typed_string_concat")
{
    // String concat with typed string param compiles as function
    CHECK(executeSourceAndGetOutput(R"(let greet (name: str) = "hello " + name; print (greet "world"))")
          == "hello world");
}

TEST_CASE("IRGenerator.FSharp.function_typed_multiple_calls")
{
    // Multiple calls to the same function
    CHECK(executeSourceAndGetOutput("let inc (x: int) = x + 1; print (inc (inc (inc 0)))") == "3");
}

TEST_CASE("IRGenerator.FSharp.function_typed_float")
{
    // Float function with type annotations compiles as function
    CHECK(executeSourceAndGetOutput("let double (x: float) = x * 2.0; print (double 3.5)") == "7");
}

TEST_CASE("IRGenerator.FSharp.function_typed_bool")
{
    // Boolean parameter with type annotation
    CHECK(executeSourceAndGetOutput(R"(let show (b: bool) = if b then "yes" else "no"; print (show true))")
          == "yes");
}

TEST_CASE("IRGenerator.FSharp.function_zero_params")
{
    // Zero-parameter function compiles as function
    CHECK(executeSourceAndGetOutput("let answer = 42; print answer") == "42");
}

TEST_CASE("IRGenerator.FSharp.function_fallback_untyped")
{
    // Functions without type annotations fall back to AST inlining but still work
    CHECK(executeSourceAndGetOutput("let id x = x; print (id 42)") == "42");
    CHECK(executeSourceAndGetOutput("let add x y = x + y; print (add 3 4)") == "7");
    CHECK(executeSourceAndGetOutput("let double x = x * 2.0; print (double 3.5)") == "7");
    CHECK(executeSourceAndGetOutput("let make x = x :: []; print (make 99)") == "[99]");
    CHECK(executeSourceAndGetOutput(R"(let greet name = "hello " + name; print (greet "world"))")
          == "hello world");
}

// =============================================================================
// F# Closure Tests (Function-compiled with captured variables)
// =============================================================================

TEST_CASE("IRGenerator.FSharp.closure_capture_int")
{
    // Closure captures an integer from enclosing scope
    CHECK(executeSourceAndGetOutput("let x = 10; let addx (y: int) = x + y; print (addx 5)") == "15");
}

TEST_CASE("IRGenerator.FSharp.closure_capture_multiple")
{
    // Closure captures multiple variables
    CHECK(executeSourceAndGetOutput("let a = 3; let b = 7; let f (x: int) = a + b + x; print (f 10)")
          == "20");
}

TEST_CASE("IRGenerator.FSharp.closure_capture_string")
{
    // Closure captures a string from enclosing scope (type annotation is `str` not `string`)
    CHECK(executeSourceAndGetOutput(
              R"(let prefix = "hello "; let greet (name: str) = prefix + name; print (greet "world"))")
          == "hello world");
}

TEST_CASE("IRGenerator.FSharp.closure_capture_float")
{
    // Closure captures a float
    CHECK(executeSourceAndGetOutput("let scale = 2.5; let f (x: float) = x * scale; print (f 4.0)") == "10");
}

TEST_CASE("IRGenerator.FSharp.closure_multiple_calls")
{
    // Same closure called multiple times with different arguments
    CHECK(executeSourceAndGetOutput("let offset = 100; let f (x: int) = x + offset; print (f 1); print (f 2)")
          == "101102");
}

TEST_CASE("IRGenerator.FSharp.closure_nested_lets")
{
    // Closure captures from nested let bindings
    CHECK(executeSourceAndGetOutput(
              "let x = 5; let y = 10; let z = 20; let f (n: int) = x + y + z + n; print (f 1)")
          == "36");
}

TEST_CASE("IRGenerator.FSharp.closure_zero_params_with_capture")
{
    // Thunk with capture — uses untyped parameter, falls back to AST inlining
    CHECK(executeSourceAndGetOutput("let x = 42; let f _dummy = x; print (f 0)") == "42");
}

TEST_CASE("IRGenerator.FSharp.closure_capture_untyped_fallback")
{
    // Untyped closure falls back to AST inlining but still works
    CHECK(executeSourceAndGetOutput("let x = 10; let addx y = x + y; print (addx 5)") == "15");
}

// =============================================================================
// F# Recursive Function Tests (UCALL/UTCALL-based with type annotations)
// =============================================================================

TEST_CASE("IRGenerator.FSharp.function_recursive_countdown")
{
    // Tail-recursive countdown using UTCALL
    CHECK(executeSourceAndGetOutput(
              "let rec countdown (n: int) = if n <= 0 then 0 else countdown (n - 1); print (countdown 10)")
          == "0");
}

TEST_CASE("IRGenerator.FSharp.function_recursive_factorial_tail")
{
    // Tail-recursive factorial with accumulator
    CHECK(executeSourceAndGetOutput(
              "let rec fact (n: int) (acc: int) = if n <= 1 then acc else fact (n - 1) (n * acc);"
              " print (fact 5 1)")
          == "120");
}

TEST_CASE("IRGenerator.FSharp.function_recursive_sum")
{
    // Tail-recursive sum using match
    CHECK(executeSourceAndGetOutput(
              "let rec sum (n: int) (acc: int) = match n with | 0 -> acc | _ -> sum (n - 1) (acc + n);"
              " print (sum 10 0)")
          == "55");
}

TEST_CASE("IRGenerator.FSharp.function_recursive_multiple_calls")
{
    // Recursive function called multiple times
    CHECK(executeSourceAndGetOutput("let rec countdown (n: int) = if n <= 0 then 0 else countdown (n - 1);"
                                    " print (countdown 5); print (countdown 3)")
          == "00");
}

TEST_CASE("IRGenerator.FSharp.function_recursive_with_capture")
{
    // Recursive function that captures a variable
    CHECK(
        executeSourceAndGetOutput(
            "let step = 2; let rec count (n: int) = if n <= 0 then 0 else count (n - step); print (count 10)")
        == "0");
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

TEST_CASE("IRGenerator.FSharp.pipeline_chain_execution_order")
{
    // Verify left-to-right execution order: 5 |> f |> g |> h
    // f(x) = x * 2, g(x) = x + 10, h(x) = x * 3
    // Correct (left-to-right): ((5 * 2) + 10) * 3 = (10 + 10) * 3 = 60
    // Wrong (right-to-left): would attempt to compose functions, not apply sequentially
    CHECK(executeSourceAndGetOutput("let f (x: int) = x * 2\n"
                                    "let g (x: int) = x + 10\n"
                                    "let h (x: int) = x * 3\n"
                                    "print (5 |> f |> g |> h)")
          == "60");

    // Also verify with string-producing functions to make order unambiguous:
    // f appends "-f", g appends "-g", h appends "-h"
    // "start" |> f |> g |> h → "start-f-g-h"
    CHECK(executeSourceAndGetOutput(R"(let f (s: str) = s + "-f")"
                                    "\n"
                                    R"(let g (s: str) = s + "-g")"
                                    "\n"
                                    R"(let h (s: str) = s + "-h")"
                                    "\n"
                                    R"(print ("start" |> f |> g |> h))")
          == "start-f-g-h");
}

TEST_CASE("IRGenerator.FSharp.pipeline_with_expression")
{
    // Pipeline with expression on left side
    REQUIRE(generatesIRSuccessfully("let double x = x * 2; let result = (3 + 2) |> double"));
}

TEST_CASE("IRGenerator.FSharp.pipeline_variable_to_function")
{
    // Variable binding piped into a function: x |> println
    CHECK(executeSourceAndGetOutput("let x = 'world' ; x |> println") == "world\n");
}

TEST_CASE("IRGenerator.FSharp.pipeline_variable_to_function_multi_prompt")
{
    // Variable binding in one prompt, piped in the next
    CHECK(sessionProducesOutput({ "let x = 'world'", "x |> print" }, "world"));
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
// not just that it compiles. Note: The main function always returns 0
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
    CHECK(executeSourceAndGetOutput("let f x = x?; "
                                    "let r = match (f (Ok 42)) with | Ok n -> n | Error e -> 0; "
                                    "print r")
          == "42");
}

TEST_CASE("IRGenerator.FSharp.exec_try_result_error_match")
{
    // Pattern match on ?-returning function with Error value (error propagation)
    CHECK(executeSourceAndGetOutput("let f x = x?; "
                                    "let r = match (f (Error 99)) with | Ok n -> n | Error e -> e; "
                                    "print r")
          == "99");
}

TEST_CASE("IRGenerator.FSharp.exec_try_option_some_match")
{
    // Pattern match on ?-returning function with Some value
    CHECK(executeSourceAndGetOutput("let f x = x?; "
                                    "let r = match (f (Some 42)) with | Some n -> n | None -> 0; "
                                    "print r")
          == "42");
}

TEST_CASE("IRGenerator.FSharp.exec_try_option_none_match")
{
    // Pattern match on ?-returning function with None value (error propagation)
    CHECK(executeSourceAndGetOutput("let f x = x?; "
                                    "let r = match (f None) with | Some n -> n | None -> 0; "
                                    "print r")
          == "0");
}

TEST_CASE("IRGenerator.FSharp.exec_try_in_let_in")
{
    // ? inside let-in expression
    CHECK(executeSourceAndGetOutput("let inc x = Some (x + 1); "
                                    "let f x = let v = (inc x)? in v * 2; "
                                    "let r = match (f 5) with | Some n -> n | None -> 0; "
                                    "print r")
          == "12");
}

TEST_CASE("IRGenerator.FSharp.exec_try_chained_with_match")
{
    // Chained ? with pattern match on result
    CHECK(executeSourceAndGetOutput("let wrap x = Ok x; "
                                    "let f x = (wrap (wrap x)?)?; "
                                    "let r = match (f 42) with | Ok n -> n | Error e -> 0; "
                                    "print r")
          == "42");
}

TEST_CASE("IRGenerator.FSharp.exec_try_function_call_with_match")
{
    // ? on function call result with pattern match
    CHECK(executeSourceAndGetOutput("let inc x = Ok (x + 1); "
                                    "let f x = (inc x)?; "
                                    "let r = match (f 5) with | Ok n -> n | Error e -> 0; "
                                    "print r")
          == "6");
}

TEST_CASE("IRGenerator.FSharp.exec_try_result_error_propagation_with_match")
{
    // Error propagation through ? with match on outer function
    CHECK(executeSourceAndGetOutput("let maybe_fail x = if x > 0 then Ok x else Error 0; "
                                    "let f x = (maybe_fail x)?; "
                                    "let r = match (f 0) with | Ok n -> n | Error e -> 99; "
                                    "print r")
          == "99");
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
    // Non-tail recursive calls now work via UCALL (function compilation).
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
    CHECK(executeSourceAndGetOutput("let rec isEven n =\n"
                                    "    match n with | 0 -> 1 | _ -> isOdd (n - 1)\n"
                                    "and isOdd n =\n"
                                    "    match n with | 0 -> 0 | _ -> isEven (n - 1);\n"
                                    "print (isEven 4)")
          == "1");
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
    CHECK(executeSourceAndGetOutput("let x = true || false; print x") == "true");
}

TEST_CASE("IRGenerator.FSharp.logical_or_false_true")
{
    CHECK(executeSourceAndGetOutput("let x = false || true; print x") == "true");
}

TEST_CASE("IRGenerator.FSharp.print_bool_true")
{
    CHECK(executeSourceAndGetOutput("let x = true; print x") == "true");
}

TEST_CASE("IRGenerator.FSharp.print_bool_false")
{
    CHECK(executeSourceAndGetOutput("let x = false; print x") == "false");
}

TEST_CASE("IRGenerator.FSharp.logical_or_false_false")
{
    CHECK(executeSourceAndGetOutput("let x = false || false; print x") == "false");
}

TEST_CASE("IRGenerator.FSharp.logical_or_true_true")
{
    CHECK(executeSourceAndGetOutput("let x = true || true; print x") == "true");
}

TEST_CASE("IRGenerator.FSharp.logical_and_true_false")
{
    CHECK(executeSourceAndGetOutput("let x = true && false; print x") == "false");
}

TEST_CASE("IRGenerator.FSharp.logical_and_true_true")
{
    CHECK(executeSourceAndGetOutput("let x = true && true; print x") == "true");
}

// =============================================================================
// Phase 2 — String Concatenation
// =============================================================================

TEST_CASE("IRGenerator.FSharp.string_concat_basic")
{
    CHECK(executeSourceAndGetOutput(R"(let s = "hello" + " world"; print s)") == "hello world");
}

TEST_CASE("IRGenerator.FSharp.string_concat_number_right")
{
    CHECK(executeSourceAndGetOutput(R"(let s = "count: " + 42; print s)") == "count: 42");
}

TEST_CASE("IRGenerator.FSharp.string_concat_number_left")
{
    CHECK(executeSourceAndGetOutput(R"(let s = 42 + " items"; print s)") == "42 items");
}

// =============================================================================
// Phase 2 — If-Then-Else Expressions
// =============================================================================

TEST_CASE("IRGenerator.FSharp.if_expr_true")
{
    CHECK(executeSourceAndGetOutput("let x = if true then 1 else 2; print x") == "1");
}

TEST_CASE("IRGenerator.FSharp.if_expr_false")
{
    CHECK(executeSourceAndGetOutput("let x = if false then 1 else 2; print x") == "2");
}

TEST_CASE("IRGenerator.FSharp.if_expr_string_true")
{
    CHECK(executeSourceAndGetOutput("print (if true then \"Hello\" else \"World\")") == "Hello");
}

TEST_CASE("IRGenerator.FSharp.if_expr_string_false")
{
    CHECK(executeSourceAndGetOutput("print (if false then \"Hello\" else \"World\")") == "World");
}

TEST_CASE("IRGenerator.FSharp.if_expr_float_true")
{
    CHECK(executeSourceAndGetOutput("print (if true then 3.14 else 2.81)") == "3.14");
}

TEST_CASE("IRGenerator.FSharp.if_expr_float_false")
{
    CHECK(executeSourceAndGetOutput("print (if false then 3.14 else 2.81)") == "2.81");
}

TEST_CASE("IRGenerator.FSharp.if_expr_with_comparison")
{
    CHECK(executeSourceAndGetOutput("let id x = x; print (id 5)") == "5");
    CHECK(executeSourceAndGetOutput("print (7)") == "7");
    CHECK(executeSourceAndGetOutput("let id x = x; print (id (7))") == "7");
    CHECK(executeSourceAndGetOutput("let id x = x; print (id (0 - 7))") == "-7");
    CHECK(executeSourceAndGetOutput("let abs x = if x < 0 then 0 - x else x; print (abs (0 - 7))") == "7");
}

TEST_CASE("IRGenerator.FSharp.if_expr_nested")
{
    CHECK(executeSourceAndGetOutput(
              "let clamp x = if x < 0 then 0 else if x > 100 then 100 else x; print (clamp 50)")
          == "50");
}

TEST_CASE("IRGenerator.FSharp.if_expr_nested_low")
{
    CHECK(executeSourceAndGetOutput(
              "let clamp x = if x < 0 then 0 else if x > 100 then 100 else x; print (clamp (0 - 5))")
          == "0");
}

TEST_CASE("IRGenerator.FSharp.if_expr_nested_high")
{
    CHECK(executeSourceAndGetOutput(
              "let clamp x = if x < 0 then 0 else if x > 100 then 100 else x; print (clamp 200)")
          == "100");
}

TEST_CASE("IRGenerator.FSharp.if_expr_recursive_factorial")
{
    CHECK(executeSourceAndGetOutput(
              "let rec fact n acc = if n <= 1 then acc else fact (n - 1) (acc * n); print (fact 5 1)")
          == "120");
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
// Multi-expression if-then-else branches (offside rule)
// =============================================================================

TEST_CASE("IRGenerator.FSharp.if_multi_expr_then_branch")
{
    // Multi-expression then branch with let bindings
    CHECK(executeSourceAndGetOutput(
              "let r =\n    if true then\n        let a = 3\n        let b = 4\n        a + b\n"
              "    else\n        0\nprint r")
          == "7");
}

TEST_CASE("IRGenerator.FSharp.if_multi_expr_else_branch")
{
    // Multi-expression else branch with let bindings
    CHECK(executeSourceAndGetOutput("let r =\n    if false then\n        0\n    else\n        let b = 10\n"
                                    "        let c = 20\n        b + c\nprint r")
          == "30");
}

TEST_CASE("IRGenerator.FSharp.if_multi_expr_both_branches")
{
    // Both branches have multiple expressions
    CHECK(executeSourceAndGetOutput(
              "let r =\n    if true then\n        let x = 2\n        let y = 3\n        x * y\n"
              "    else\n        let a = 10\n        let b = 20\n        a + b\nprint r")
          == "6");
}

TEST_CASE("IRGenerator.FSharp.if_multi_expr_backward_compat_single_line")
{
    // Single-line if-then-else still works
    CHECK(executeSourceAndGetOutput("let x = if true then 1 else 2; print x") == "1");
    CHECK(executeSourceAndGetOutput("let x = if false then 1 else 2; print x") == "2");
}

TEST_CASE("IRGenerator.FSharp.if_multi_expr_with_print_statements")
{
    // Multi-expression branches with side-effectful expressions (print)
    CHECK(executeSourceAndGetOutput("if true then\n    print 1\n    print 2\nelse\n    print 3\n") == "12");
}

TEST_CASE("IRGenerator.FSharp.if_multi_expr_nested")
{
    // Nested if-then-else inside outer else branch
    CHECK(
        executeSourceAndGetOutput(
            "let r =\n    if false then\n        0\n    else\n        if true then\n            let x = 42\n"
            "            x\n        else\n            99\nprint r")
        == "42");
}

TEST_CASE("IRGenerator.FSharp.if_multi_expr_recursive_function")
{
    // Recursive function with multi-expression branches (calc-1x1 pattern)
    CHECK(executeSourceAndGetOutput("let rec go (n: int) =\n    if n <= 0 then\n        let result = 42\n"
                                    "        result\n    else\n        go (n - 1)\nprint (go 3)")
          == "42");
}

// =============================================================================
// Phase 2 — Mutable Variable Assignment
// =============================================================================

TEST_CASE("IRGenerator.FSharp.mutable_assignment_basic")
{
    CHECK(executeSourceAndGetOutput("let mut x = 1; x <- 42; print x") == "42");
}

TEST_CASE("IRGenerator.FSharp.mutable_assignment_increment")
{
    CHECK(executeSourceAndGetOutput("let mut counter = 0; counter <- counter + 1; print counter") == "1");
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
    CHECK(executeSourceAndGetOutput("let fst t = match t with | (a, _) -> a; let t = (1, 2); print (fst t)")
          == "1");
}

TEST_CASE("IRGenerator.FSharp.tuple_snd")
{
    CHECK(executeSourceAndGetOutput("let snd t = match t with | (_, b) -> b; let t = (1, 2); print (snd t)")
          == "2");
}

TEST_CASE("IRGenerator.FSharp.tuple_pattern_match")
{
    CHECK(executeSourceAndGetOutput("let t = (3, 4); let r = match t with | (a, b) -> a + b; print r")
          == "7");
}

TEST_CASE("IRGenerator.FSharp.tuple_3_elements")
{
    CHECK(executeSourceAndGetOutput(
              "let fst t = match t with | (a, _) -> a; let t = (10, 20, 30); print (fst t)")
          == "10");
}

TEST_CASE("IRGenerator.FSharp.tuple_swap")
{
    CHECK(executeSourceAndGetOutput("let fst t = match t with | (a, _) -> a; "
                                    "let snd t = match t with | (_, b) -> b; "
                                    "let swap t = match t with | (a, b) -> (b, a); "
                                    "let s = swap (1, 2); print (fst s); print (snd s)")
          == "21");
}

TEST_CASE("IRGenerator.FSharp.tuple_sum_pair")
{
    CHECK(
        executeSourceAndGetOutput("let sum_pair t = match t with | (a, b) -> a + b; print (sum_pair (3, 4))")
        == "7");
}

TEST_CASE("IRGenerator.FSharp.tuple_mixed_types")
{
    // Extract numeric element from mixed tuple via pattern matching
    CHECK(executeSourceAndGetOutput(R"(let t = (42, "hello"); let r = match t with | (a, b) -> a; print r)")
          == "42");
}

TEST_CASE("IRGenerator.FSharp.tuple_numeric_snd")
{
    // Extract second numeric element via pattern matching
    CHECK(executeSourceAndGetOutput(R"(let t = ("world", 99); let r = match t with | (_, b) -> b; print r)")
          == "99");
}

TEST_CASE("IRGenerator.FSharp.tuple_snd_via_function")
{
    // Extract numeric second element via user-defined function
    CHECK(executeSourceAndGetOutput("let get_snd t = match t with | (_, b) -> b; print (get_snd (1, 42))")
          == "42");
}

// --- Bare tuple syntax in match expressions ---

TEST_CASE("IRGenerator.FSharp.match_bare_tuple_numeric")
{
    CHECK(executeSourceAndGetOutput("let r = match 3, 4 with | a, b -> a + b; print r") == "7");
}

TEST_CASE("IRGenerator.FSharp.match_bare_tuple_wildcard")
{
    CHECK(executeSourceAndGetOutput("let r = match 1, 2 with | _, b -> b; print r") == "2");
}

TEST_CASE("IRGenerator.FSharp.match_bare_tuple_constructor")
{
    CHECK(executeSourceAndGetOutput(
              "let r = match Some 1, Some 2 with | Some a, Some b -> a + b | _, _ -> 0; print r")
          == "3");
}

TEST_CASE("IRGenerator.FSharp.match_bare_tuple_or_pattern")
{
    CHECK(
        executeSourceAndGetOutput(
            R"(let r = match None, Some 5 with | None, _ | _, None -> "miss" | Some a, Some b -> "hit"; print r)")
        == "miss");
}

TEST_CASE("IRGenerator.FSharp.match_bare_tuple_3_elements")
{
    CHECK(executeSourceAndGetOutput("let r = match 1, 2, 3 with | a, b, c -> a + b + c; print r") == "6");
}

TEST_CASE("IRGenerator.FSharp.match_bare_tuple_backward_compat")
{
    // Parenthesized form still works
    CHECK(executeSourceAndGetOutput("let r = match (3, 4) with | (a, b) -> a + b; print r") == "7");
}

TEST_CASE("IRGenerator.FSharp.match_bare_tuple_with_which")
{
    auto& rt = TestRuntime::instance();
    rt.clearMockWhichPaths();
    rt.setMockWhichPath("echo", "/bin/echo");
    rt.setMockWhichPath("cat", "/bin/cat");
    CHECK(executeSourceAndGetOutput(R"(
match which "echo", which "cat" with
| Some f, Some l -> exec f "hello"
| Some f, None -> exec f "hello"
| None, Some l -> exec l
| None, None -> println "none"
)") == "hello\n");
    rt.clearMockWhichPaths();
}

// =============================================================================
// Phase 2 — Standard Library Builtins
// =============================================================================

TEST_CASE("IRGenerator.FSharp.builtin_string_length")
{
    CHECK(executeSourceAndGetOutput(R"(print (string_length "hello"))") == "5");
}

TEST_CASE("IRGenerator.FSharp.builtin_string_of_int")
{
    CHECK(executeSourceAndGetOutput(R"(print (string_of_int 42))") == "42");
}

TEST_CASE("IRGenerator.FSharp.builtin_int_of_string")
{
    CHECK(executeSourceAndGetOutput(R"(let n = int_of_string "7"; print (n + 3))") == "10");
}

TEST_CASE("IRGenerator.FSharp.builtin_not_true")
{
    CHECK(executeSourceAndGetOutput("let x = not true; print x") == "false");
}

TEST_CASE("IRGenerator.FSharp.builtin_not_false")
{
    CHECK(executeSourceAndGetOutput("let x = not false; print x") == "true");
}

TEST_CASE("IRGenerator.FSharp.builtin_pipeline_string_of_int_length")
{
    CHECK(executeSourceAndGetOutput("let r = 42 |> string_of_int |> string_length; print r") == "2");
}

TEST_CASE("IRGenerator.FSharp.tuple_fst_direct")
{
    CHECK(executeSourceAndGetOutput("let fst t = match t with | (a, _) -> a; print (fst (1, 2))") == "1");
}

TEST_CASE("IRGenerator.FSharp.tuple_snd_direct")
{
    CHECK(executeSourceAndGetOutput("let snd t = match t with | (_, b) -> b; print (snd (1, 2))") == "2");
}

// =============================================================================
// Float Literal and Arithmetic Tests
// =============================================================================

TEST_CASE("IRGenerator.FSharp.float_literal")
{
    CHECK(executeSourceAndGetOutput("print 3.14") == "3.14");
}

TEST_CASE("IRGenerator.FSharp.float_arithmetic_add")
{
    CHECK(executeSourceAndGetOutput("print (1.5 + 2.5)") == "4");
}

TEST_CASE("IRGenerator.FSharp.float_arithmetic_sub")
{
    CHECK(executeSourceAndGetOutput("print (5.0 - 2.5)") == "2.5");
}

TEST_CASE("IRGenerator.FSharp.float_arithmetic_mul")
{
    CHECK(executeSourceAndGetOutput("print (3.0 * 2.5)") == "7.5");
}

TEST_CASE("IRGenerator.FSharp.float_arithmetic_div")
{
    CHECK(executeSourceAndGetOutput("print (7.0 / 2.0)") == "3.5");
}

TEST_CASE("IRGenerator.FSharp.float_negation")
{
    CHECK(executeSourceAndGetOutput("print (-3.14)") == "-3.14");
}

TEST_CASE("IRGenerator.FSharp.float_mixed_int_promotion")
{
    CHECK(executeSourceAndGetOutput("print (1 + 2.5)") == "3.5");
}

TEST_CASE("IRGenerator.FSharp.float_mixed_int_promotion_reverse")
{
    CHECK(executeSourceAndGetOutput("print (2.5 + 1)") == "3.5");
}

TEST_CASE("IRGenerator.FSharp.float_comparison_lt")
{
    CHECK(executeSourceAndGetOutput("let r = if 2.5 < 3.0 then 1 else 0; print r") == "1");
}

TEST_CASE("IRGenerator.FSharp.float_comparison_gt")
{
    CHECK(executeSourceAndGetOutput("let r = if 3.0 > 2.5 then 1 else 0; print r") == "1");
}

TEST_CASE("IRGenerator.FSharp.float_string_concat")
{
    CHECK(executeSourceAndGetOutput(R"(print ("pi=" + 3.14))") == "pi=3.14");
}

TEST_CASE("IRGenerator.FSharp.float_in_function")
{
    CHECK(executeSourceAndGetOutput("let double x = x * 2.0; print (double 3.5)") == "7");
}

TEST_CASE("IRGenerator.FSharp.float_let_binding")
{
    CHECK(executeSourceAndGetOutput("let pi = 3.14; print pi") == "3.14");
}

TEST_CASE("IRGenerator.FSharp.float_pow")
{
    CHECK(executeSourceAndGetOutput("print (2.0 ** 3.0)") == "8");
}

TEST_CASE("IRGenerator.FSharp.float_mod")
{
    CHECK(executeSourceAndGetOutput("print (7.5 % 2.0)") == "1.5");
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

TEST_CASE("IRGenerator.FSharp.TypeAnnotation.session_function_recursive")
{
    // Typed recursive function defined in one prompt, called in the next (compiled as function)
    CHECK(sessionProducesOutput({ "let rec countdown (n: int): int = if n <= 0 then 0 else countdown (n - 1)",
                                  "print (countdown 5)" },
                                "0"));
}

TEST_CASE("IRGenerator.FSharp.TypeAnnotation.session_function_with_closure")
{
    // Typed function capturing a persisted value binding (closure via function)
    CHECK(sessionProducesOutput(
        { "let offset = 10", "let addOffset (x: int): int = x + offset", "print (addOffset 5)" }, "15"));
}

TEST_CASE("IRGenerator.FSharp.TypeAnnotation.session_function_multiple_calls")
{
    // Typed compiled function called across multiple prompts
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
    CHECK(executeSourceAndGetOutput("let unwrap opt = opt?; let r = unwrap (env \"HOME\")") == "");
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
// F# which builtin (returns Option<string>)
// ============================================================================

TEST_CASE("IRGenerator.FSharp.which_found")
{
    auto& rt = TestRuntime::instance();
    rt.clearMockWhichPaths();
    rt.setMockWhichPath("git", "/usr/bin/git");
    CHECK(
        executeSourceAndGetOutput(R"(let r = match which "git" with | Some p -> p | None -> "none"; print r)")
        == "/usr/bin/git");
    rt.clearMockWhichPaths();
}

TEST_CASE("IRGenerator.FSharp.which_not_found")
{
    auto& rt = TestRuntime::instance();
    rt.clearMockWhichPaths();
    CHECK(executeSourceAndGetOutput(
              R"(let r = match which "nonexistent" with | Some p -> "found" | None -> "none"; print r)")
          == "none");
    rt.clearMockWhichPaths();
}

TEST_CASE("IRGenerator.FSharp.which_let_binding")
{
    auto& rt = TestRuntime::instance();
    rt.clearMockWhichPaths();
    rt.setMockWhichPath("ls", "/bin/ls");
    CHECK(executeSourceAndGetOutput(
              R"(let w = which "ls"; let r = match w with | Some p -> p | None -> "none"; print r)")
          == "/bin/ls");
    rt.clearMockWhichPaths();
}

TEST_CASE("IRGenerator.FSharp.which_pipeline")
{
    auto& rt = TestRuntime::instance();
    rt.clearMockWhichPaths();
    rt.setMockWhichPath("cat", "/usr/bin/cat");
    CHECK(executeSourceAndGetOutput(
              R"(which "cat" |> fun opt -> match opt with | Some p -> print p | None -> print "none")")
          == "/usr/bin/cat");
    rt.clearMockWhichPaths();
}

TEST_CASE("IRGenerator.FSharp.which_default_operator")
{
    auto& rt = TestRuntime::instance();
    rt.clearMockWhichPaths();
    rt.setMockWhichPath("git", "/usr/bin/git");
    CHECK(executeSourceAndGetOutput(R"(print (which "git" ?| "/default"))") == "/usr/bin/git");
    rt.clearMockWhichPaths();
}

TEST_CASE("IRGenerator.FSharp.which_default_operator_none")
{
    auto& rt = TestRuntime::instance();
    rt.clearMockWhichPaths();
    CHECK(executeSourceAndGetOutput(R"(print (which "missing" ?| "/default"))") == "/default");
    rt.clearMockWhichPaths();
}

TEST_CASE("IRGenerator.FSharp.which_ir_generation")
{
    CHECK(generatesIRSuccessfully(R"(which "git")"));
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
    CHECK(executeSourceAndGetOutput("print ()") == "0");
}

TEST_CASE("IRGenerator.FSharp.unit_let_binding")
{
    CHECK(executeSourceAndGetOutput("let x = (); print x") == "0");
}

// ============================================================================
// Phase 1 Foundation: String repetition
// ============================================================================

TEST_CASE("IRGenerator.FSharp.string_repeat_basic")
{
    CHECK(executeSourceAndGetOutput(R"(print ("ha" * 3))") == "hahaha");
}

TEST_CASE("IRGenerator.FSharp.string_repeat_commutative")
{
    CHECK(executeSourceAndGetOutput(R"(print (3 * "ab"))") == "ababab");
}

TEST_CASE("IRGenerator.FSharp.string_repeat_zero")
{
    CHECK(executeSourceAndGetOutput(R"(print ("x" * 0))") == "");
}

TEST_CASE("IRGenerator.FSharp.string_repeat_one")
{
    CHECK(executeSourceAndGetOutput(R"(print ("y" * 1))") == "y");
}

// ============================================================================
// Phase 1 Foundation: Block scopes
// ============================================================================

TEST_CASE("IRGenerator.FSharp.block_scope_basic")
{
    CHECK(executeSourceAndGetOutput("let r = { let x = 10; x + 5 }; print r") == "15");
}

TEST_CASE("IRGenerator.FSharp.block_scope_multiple_lets")
{
    CHECK(executeSourceAndGetOutput("let r = { let x = 1; let y = 2; x + y }; print r") == "3");
}

TEST_CASE("IRGenerator.FSharp.block_scope_isolation")
{
    CHECK(executeSourceAndGetOutput("let x = 1; let r = { let x = 99; x }; print r; print x") == "991");
}

// ============================================================================
// Phase 1 Foundation: Function composition >> and <<
// ============================================================================

TEST_CASE("IRGenerator.FSharp.compose_forward")
{
    CHECK(executeSourceAndGetOutput(
              "let double x = x * 2; let inc x = x + 1; let f = double >> inc; print (f 5)")
          == "11");
}

TEST_CASE("IRGenerator.FSharp.compose_backward")
{
    CHECK(executeSourceAndGetOutput(
              "let double x = x * 2; let inc x = x + 1; let f = inc << double; print (f 5)")
          == "11");
}

TEST_CASE("IRGenerator.FSharp.compose_chain")
{
    CHECK(executeSourceAndGetOutput(
              "let a x = x + 1; let b x = x * 2; let c x = x - 3; let f = a >> b >> c; print (f 5)")
          == "9");
}

// ============================================================================
// Phase 1 Foundation: Tuple destructuring in let
// ============================================================================

TEST_CASE("IRGenerator.FSharp.tuple_destructure_basic")
{
    CHECK(executeSourceAndGetOutput("let (x, y) = (10, 20); print x; print y") == "1020");
}

TEST_CASE("IRGenerator.FSharp.tuple_destructure_from_binding")
{
    CHECK(executeSourceAndGetOutput("let t = (3, 4); let (a, b) = t; print (a + b)") == "7");
}

TEST_CASE("IRGenerator.FSharp.tuple_destructure_3_elements")
{
    CHECK(executeSourceAndGetOutput("let (a, b, c) = (1, 2, 3); print (a + b + c)") == "6");
}

TEST_CASE("IRGenerator.FSharp.tuple_destructure_let_in")
{
    CHECK(executeSourceAndGetOutput("let r = let (x, y) = (5, 6) in x * y; print r") == "30");
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
    CHECK(executeSourceAndGetOutput("print []") == "[]");
}

TEST_CASE("IRGenerator.FSharp.list_print_single")
{
    CHECK(executeSourceAndGetOutput("print [42]") == "[42]");
}

TEST_CASE("IRGenerator.FSharp.list_print_multiple")
{
    CHECK(executeSourceAndGetOutput("print [1; 2; 3]") == "[1; 2; 3]");
}

TEST_CASE("IRGenerator.FSharp.list_print_binding")
{
    CHECK(executeSourceAndGetOutput("let x = [10; 20; 30]; print x") == "[10; 20; 30]");
}

// =============================================================================
// F# Type Tag Slot Tests (proper element type propagation in containers)
// =============================================================================

TEST_CASE("IRGenerator.FSharp.type_tag.string_list")
{
    CHECK(executeSourceAndGetOutput("println ['a', 'b', 'c']") == "[\"a\"; \"b\"; \"c\"]\n");
}

TEST_CASE("IRGenerator.FSharp.type_tag.string_list_words")
{
    CHECK(executeSourceAndGetOutput(R"(println ["hello", "world"])") == "[\"hello\"; \"world\"]\n");
}

TEST_CASE("IRGenerator.FSharp.type_tag.nested_string_list")
{
    CHECK(executeSourceAndGetOutput(R"(println [["a", "b"], ["c"]])") == "[[\"a\"; \"b\"]; [\"c\"]]\n");
}

TEST_CASE("IRGenerator.FSharp.type_tag.option_with_string")
{
    CHECK(executeSourceAndGetOutput(R"(println (Some "hello"))") == "Some \"hello\"\n");
}

TEST_CASE("IRGenerator.FSharp.type_tag.result_with_string")
{
    CHECK(executeSourceAndGetOutput(R"(println (Ok "success"))") == "Ok \"success\"\n");
}

TEST_CASE("IRGenerator.FSharp.type_tag.tuple_with_string")
{
    CHECK(executeSourceAndGetOutput(R"(println (42, "hello"))") == "(42, \"hello\")\n");
}

TEST_CASE("IRGenerator.FSharp.type_tag.tuple3_mixed")
{
    CHECK(executeSourceAndGetOutput(R"(println ("hello", 42, true))") == "(\"hello\", 42, true)\n");
}

TEST_CASE("IRGenerator.FSharp.type_tag.list_of_options")
{
    CHECK(executeSourceAndGetOutput(R"(println [Some "a", None])") == "[Some \"a\"; None]\n");
}

// =============================================================================
// F# Cons (::) Operator Tests
// =============================================================================

TEST_CASE("IRGenerator.FSharp.cons_single")
{
    CHECK(executeSourceAndGetOutput("print (1 :: [])") == "[1]");
}

TEST_CASE("IRGenerator.FSharp.cons_multiple")
{
    CHECK(executeSourceAndGetOutput("print (1 :: 2 :: 3 :: [])") == "[1; 2; 3]");
}

TEST_CASE("IRGenerator.FSharp.cons_prepend_to_list")
{
    CHECK(executeSourceAndGetOutput("print (0 :: [1; 2; 3])") == "[0; 1; 2; 3]");
}

TEST_CASE("IRGenerator.FSharp.cons_with_binding")
{
    CHECK(executeSourceAndGetOutput("let xs = [2; 3]; print (1 :: xs)") == "[1; 2; 3]");
}

// =============================================================================
// F# List Pattern Matching Tests
// =============================================================================

TEST_CASE("IRGenerator.FSharp.match_empty_list")
{
    CHECK(executeSourceAndGetOutput(
              "let x = []; let r = match x with | [] -> \"empty\" | _ -> \"not\"; print r")
          == "empty");
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
    CHECK(executeSourceAndGetOutput("let x = [1]; let r = match x with | [] -> 0 | h :: _ -> h; print r")
          == "1");
}

TEST_CASE("IRGenerator.FSharp.match_cons_second_element")
{
    CHECK(executeSourceAndGetOutput(
              "let x = [1; 2; 3]; let r = match x with | _ :: second :: _ -> second | _ -> 0; print r")
          == "2");
}

TEST_CASE("IRGenerator.FSharp.match_fixed_length_list")
{
    CHECK(executeSourceAndGetOutput(
              "let x = [10; 20]; let r = match x with | [a; b] -> a + b | _ -> 0; print r")
          == "30");
}

TEST_CASE("IRGenerator.FSharp.match_recursive_simple")
{
    // Simple 1-parameter recursive function with list pattern matching
    CHECK(executeSourceAndGetOutput("let rec f xs = match xs with | [] -> 0 | h :: t -> f t; "
                                    "print (f [1; 2])")
          == "0");
}

TEST_CASE("IRGenerator.FSharp.match_recursive_sum")
{
    // Use accumulator style since non-tail recursion (h + sum t) is not supported
    CHECK(executeSourceAndGetOutput(
              "let rec sumAcc acc xs = match xs with | [] -> acc | h :: t -> sumAcc (acc + h) t; "
              "print (sumAcc 0 [1; 2; 3; 4])")
          == "10");
}

TEST_CASE("IRGenerator.FSharp.match_recursive_length")
{
    // Use accumulator style since non-tail recursion (1 + len t) is not supported
    CHECK(executeSourceAndGetOutput(
              "let rec lenAcc acc xs = match xs with | [] -> acc | _ :: t -> lenAcc (acc + 1) t; "
              "print (lenAcc 0 [10; 20; 30])")
          == "3");
}

// =============================================================================
// List range expressions [start..end] and [start..step..end]
// =============================================================================

TEST_CASE("IRGenerator.FSharp.list_range_simple")
{
    CHECK(executeSourceAndGetOutput("print [1..3]") == "[1; 2; 3]");
}

TEST_CASE("IRGenerator.FSharp.list_range_single")
{
    CHECK(executeSourceAndGetOutput("print [1..1]") == "[1]");
}

TEST_CASE("IRGenerator.FSharp.list_range_empty")
{
    CHECK(executeSourceAndGetOutput("print [5..3]") == "[]");
}

TEST_CASE("IRGenerator.FSharp.list_range_step")
{
    CHECK(executeSourceAndGetOutput("print [1..2..7]") == "[1; 3; 5; 7]");
}

TEST_CASE("IRGenerator.FSharp.list_range_negative_step")
{
    CHECK(executeSourceAndGetOutput("print [10..-1..7]") == "[10; 9; 8; 7]");
}

TEST_CASE("IRGenerator.FSharp.list_range_even_step")
{
    CHECK(executeSourceAndGetOutput("print [0..2..10]") == "[0; 2; 4; 6; 8; 10]");
}

TEST_CASE("IRGenerator.FSharp.list_range_with_pattern_matching")
{
    CHECK(executeSourceAndGetOutput(
              "let rec sumAcc acc xs = match xs with | [] -> acc | h :: t -> sumAcc (acc + h) t\n"
              "print (sumAcc 0 [1..5])")
          == "15");
}

TEST_CASE("IRGenerator.FSharp.list_range_let_binding")
{
    CHECK(executeSourceAndGetOutput("let xs = [1..5]\n"
                                    "print xs")
          == "[1; 2; 3; 4; 5]");
}

// =============================================================================
// List concatenation operator @
// =============================================================================

TEST_CASE("IRGenerator.FSharp.list_concat_basic")
{
    CHECK(executeSourceAndGetOutput("print ([1; 2] @ [3; 4])") == "[1; 2; 3; 4]");
}

TEST_CASE("IRGenerator.FSharp.list_concat_empty_left")
{
    CHECK(executeSourceAndGetOutput("print ([] @ [1; 2])") == "[1; 2]");
}

TEST_CASE("IRGenerator.FSharp.list_concat_empty_right")
{
    CHECK(executeSourceAndGetOutput("print ([1; 2] @ [])") == "[1; 2]");
}

TEST_CASE("IRGenerator.FSharp.list_concat_both_empty")
{
    CHECK(executeSourceAndGetOutput("print ([] @ [])") == "[]");
}

TEST_CASE("IRGenerator.FSharp.list_concat_chained")
{
    CHECK(executeSourceAndGetOutput("print ([1] @ [2] @ [3])") == "[1; 2; 3]");
}

TEST_CASE("IRGenerator.FSharp.list_concat_with_cons")
{
    CHECK(executeSourceAndGetOutput("print (0 :: [1; 2] @ [3; 4])") == "[0; 1; 2; 3; 4]");
}

TEST_CASE("IRGenerator.FSharp.list_concat_with_range")
{
    CHECK(executeSourceAndGetOutput("print ([1..3] @ [4..6])") == "[1; 2; 3; 4; 5; 6]");
}

TEST_CASE("IRGenerator.FSharp.list_concat_bound_variables")
{
    CHECK(executeSourceAndGetOutput("let a = [1; 2]; let b = [3; 4]; print (a @ b)") == "[1; 2; 3; 4]");
}

TEST_CASE("IRGenerator.FSharp.list_concat_single_elements")
{
    CHECK(executeSourceAndGetOutput("print ([1] @ [2])") == "[1; 2]");
}

// =============================================================================
// Additional list execution edge cases
// =============================================================================

TEST_CASE("IRGenerator.FSharp.list_nested_in_option")
{
    // List inside Some/None
    CHECK(executeSourceAndGetOutput(
              "let x = Some [1; 2; 3]; match x with | Some xs -> print xs | None -> print 0")
          == "[1; 2; 3]");
}

TEST_CASE("IRGenerator.FSharp.list_nested_in_result")
{
    // List inside Ok/Error
    CHECK(executeSourceAndGetOutput(
              "let x = Ok [10; 20]; match x with | Ok xs -> print xs | Error _ -> print 0")
          == "[10; 20]");
}

TEST_CASE("IRGenerator.FSharp.list_in_tuple")
{
    // Tuple containing a list
    CHECK(executesSuccessfully("let t = ([1; 2], 42)"));
}

TEST_CASE("IRGenerator.FSharp.list_as_function_arg")
{
    // Pass list to function and operate on it
    CHECK(executeSourceAndGetOutput(
              "let first xs = match xs with | h :: _ -> h | [] -> 0; print (first [5; 6; 7])")
          == "5");
}

TEST_CASE("IRGenerator.FSharp.list_as_function_return")
{
    // Return a list from a function
    CHECK(executeSourceAndGetOutput("let make x = x :: []; print (make 99)") == "[99]");
}

TEST_CASE("IRGenerator.FSharp.list_match_wildcard_tail")
{
    // Wildcard in tail position
    CHECK(
        executeSourceAndGetOutput("let x = [1; 2; 3]; let r = match x with | h :: _ -> h | [] -> 0; print r")
        == "1");
}

TEST_CASE("IRGenerator.FSharp.list_match_wildcard_head")
{
    // Wildcard in head position
    CHECK(executeSourceAndGetOutput("let rec last xs = match xs with | [x] -> x | _ :: t -> last t | [] -> "
                                    "0; print (last [10; 20; 30])")
          == "30");
}

TEST_CASE("IRGenerator.FSharp.list_match_three_elements")
{
    // Fixed 3-element list pattern
    CHECK(executeSourceAndGetOutput(
              "let x = [1; 2; 3]; let r = match x with | [a; b; c] -> a + b + c | _ -> 0; print r")
          == "6");
}

TEST_CASE("IRGenerator.FSharp.list_range_large")
{
    // Larger range
    CHECK(executeSourceAndGetOutput(
              "let rec sumAcc acc xs = match xs with | [] -> acc | h :: t -> sumAcc (acc + h) t\n"
              "print (sumAcc 0 [1..10])")
          == "55");
}

TEST_CASE("IRGenerator.FSharp.list_range_step_not_aligned")
{
    // Step doesn't evenly divide range — should stop before overshooting
    CHECK(executeSourceAndGetOutput("print [1..3..10]") == "[1; 4; 7; 10]");
}

TEST_CASE("IRGenerator.FSharp.list_range_step_overshoot")
{
    // Step overshoots end — should include only start (aligned to valid elements)
    CHECK(executeSourceAndGetOutput("print [1..10..5]") == "[1]");
}

TEST_CASE("IRGenerator.FSharp.list_range_step_exact_fit")
{
    // Step exactly fits the range
    CHECK(executeSourceAndGetOutput("print [0..5..10]") == "[0; 5; 10]");
}

TEST_CASE("IRGenerator.FSharp.list_cons_right_associative")
{
    // Verify :: is right-associative: 1 :: 2 :: 3 :: [] = 1 :: (2 :: (3 :: []))
    CHECK(executeSourceAndGetOutput("print (1 :: 2 :: 3 :: [])") == "[1; 2; 3]");
}

TEST_CASE("IRGenerator.FSharp.list_concat_right_associative")
{
    // Verify @ is right-associative: [1] @ [2] @ [3] = [1] @ ([2] @ [3])
    CHECK(executeSourceAndGetOutput("print ([1] @ [2] @ [3])") == "[1; 2; 3]");
}

TEST_CASE("IRGenerator.FSharp.list_cons_precedence_over_concat")
{
    // :: has same precedence as @, both right-associative
    // 0 :: [1; 2] @ [3; 4] should parse as 0 :: ([1; 2] @ [3; 4])
    CHECK(executeSourceAndGetOutput("print (0 :: [1; 2] @ [3; 4])") == "[0; 1; 2; 3; 4]");
}

TEST_CASE("IRGenerator.FSharp.list_recursive_count_elements")
{
    // Count elements greater than a threshold
    CHECK(
        executeSourceAndGetOutput("let rec countAbove threshold acc xs = match xs with\n"
                                  "  | [] -> acc\n"
                                  "  | h :: t -> if h > threshold then countAbove threshold (acc + 1) t else "
                                  "countAbove threshold acc t\n"
                                  "print (countAbove 3 0 [1; 5; 2; 7; 3; 8])")
        == "3");
}

TEST_CASE("IRGenerator.FSharp.list_recursive_find_max")
{
    // Find maximum element in a list
    CHECK(executeSourceAndGetOutput("let rec maxAcc acc xs = match xs with\n"
                                    "  | [] -> acc\n"
                                    "  | h :: t -> if h > acc then maxAcc h t else maxAcc acc t\n"
                                    "print (maxAcc 0 [3; 7; 2; 9; 1])")
          == "9");
}

TEST_CASE("IRGenerator.FSharp.list_rec_reverse_cons_in_tail_arg")
{
    // ConsExpr in tail call arguments — previously a known limitation.
    CHECK(executeSourceAndGetOutput("let rec revAcc acc xs = match xs with\n"
                                    "  | [] -> acc\n"
                                    "  | h :: t -> revAcc (h :: acc) t\n"
                                    "print (revAcc [] [1; 2; 3])")
          == "[3; 2; 1]");
}

TEST_CASE("IRGenerator.FSharp.list_rec_reverse_longer")
{
    // Longer list to exercise multiple loop iterations with cons construction.
    CHECK(executeSourceAndGetOutput("let rec revAcc acc xs = match xs with\n"
                                    "  | [] -> acc\n"
                                    "  | h :: t -> revAcc (h :: acc) t\n"
                                    "print (revAcc [] [1; 2; 3; 4; 5])")
          == "[5; 4; 3; 2; 1]");
}

TEST_CASE("IRGenerator.FSharp.list_match_nested_cons_depth3")
{
    // Match on 3-deep nested cons (a :: b :: c :: rest)
    CHECK(executeSourceAndGetOutput("let x = [10; 20; 30; 40; 50]\n"
                                    "let r = match x with | a :: b :: c :: _ -> a + b + c | _ -> 0\n"
                                    "print r")
          == "60");
}

TEST_CASE("IRGenerator.FSharp.list_match_fallthrough_to_empty")
{
    // Non-matching cons pattern falls through to empty pattern
    CHECK(executeSourceAndGetOutput("let x = []; let r = match x with | h :: t -> h | [] -> 99; print r")
          == "99");
}

TEST_CASE("IRGenerator.FSharp.list_match_fallthrough_to_wildcard")
{
    // Non-matching specific pattern falls through to wildcard
    CHECK(executeSourceAndGetOutput("let x = [1; 2; 3]\n"
                                    "let r = match x with | [a] -> a | _ -> 42\n"
                                    "print r")
          == "42");
}

TEST_CASE("IRGenerator.FSharp.list_concat_preserves_order")
{
    // Verify concatenation preserves element order from both sides
    CHECK(executeSourceAndGetOutput(
              "let rec sumAcc acc xs = match xs with | [] -> acc | h :: t -> sumAcc (acc + h) t\n"
              "let xs = [1; 2] @ [3; 4] @ [5; 6]\n"
              "print (sumAcc 0 xs)")
          == "21");
}

TEST_CASE("IRGenerator.FSharp.list_range_negative_descend_empty")
{
    // Negative step but start < end — should be empty
    CHECK(executeSourceAndGetOutput("print [1..-1..5]") == "[]");
}

// =============================================================================
// List comprehensions
// =============================================================================

TEST_CASE("IRGenerator.FSharp.list_comprehension_basic")
{
    CHECK(executeSourceAndGetOutput("print [for x in [1;2;3] -> x]") == "[1; 2; 3]");
}

TEST_CASE("IRGenerator.FSharp.list_comprehension_transform")
{
    CHECK(executeSourceAndGetOutput("print [for x in [1;2;3] -> x * 2]") == "[2; 4; 6]");
}

TEST_CASE("IRGenerator.FSharp.list_comprehension_with_filter")
{
    CHECK(executeSourceAndGetOutput("print [for x in [1;2;3;4;5] when x > 2 -> x]") == "[3; 4; 5]");
}

TEST_CASE("IRGenerator.FSharp.list_comprehension_filter_and_transform")
{
    CHECK(executeSourceAndGetOutput("print [for x in [1;2;3;4;5] when x > 2 -> x * 10]") == "[30; 40; 50]");
}

TEST_CASE("IRGenerator.FSharp.list_comprehension_empty_source")
{
    CHECK(executeSourceAndGetOutput("print [for x in [] -> x * 2]") == "[]");
}

TEST_CASE("IRGenerator.FSharp.list_comprehension_all_filtered")
{
    CHECK(executeSourceAndGetOutput("print [for x in [1;2;3] when x > 10 -> x]") == "[]");
}

TEST_CASE("IRGenerator.FSharp.list_comprehension_from_range")
{
    CHECK(executeSourceAndGetOutput("print [for x in [1..5] -> x * x]") == "[1; 4; 9; 16; 25]");
}

TEST_CASE("IRGenerator.FSharp.list_comprehension_let_binding")
{
    CHECK(executeSourceAndGetOutput("let s = [for x in [1;2;3;4] -> x * x]\nprint s") == "[1; 4; 9; 16]");
}

TEST_CASE("IRGenerator.FSharp.list_comprehension_single_element")
{
    CHECK(executeSourceAndGetOutput("print [for x in [42] -> x + 1]") == "[43]");
}

TEST_CASE("IRGenerator.FSharp.list_comprehension_preserves_order")
{
    CHECK(executeSourceAndGetOutput("print [for x in [5;4;3;2;1] -> x]") == "[5; 4; 3; 2; 1]");
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
    CHECK(executeSourceAndGetOutput("let double x = x * 2\nlet apply f x = f x\nprint (apply double 5)")
          == "10");
}

TEST_CASE("IRGenerator.FSharp.hof_lambda_argument")
{
    // HOF with lambda argument
    CHECK(executeSourceAndGetOutput("let apply f x = f x\nprint (apply (fun x -> x * 2) 5)") == "10");
}

TEST_CASE("IRGenerator.FSharp.hof_twice")
{
    // Apply function to result of itself
    CHECK(executeSourceAndGetOutput("let double x = x * 2\nlet twice f x = f (f x)\nprint (twice double 3)")
          == "12");
}

TEST_CASE("IRGenerator.FSharp.hof_compose")
{
    // Function composition
    CHECK(executeSourceAndGetOutput(
              "let double x = x * 2\nlet add1 x = x + 1\nlet compose f g x = f (g x)\nprint "
              "(compose double add1 5)")
          == "12");
}

TEST_CASE("IRGenerator.FSharp.hof_partial_application")
{
    // Partial application of HOF
    CHECK(executeSourceAndGetOutput(
              "let double x = x * 2\nlet apply f x = f x\nlet g = apply double\nprint (g 5)")
          == "10");
}

TEST_CASE("IRGenerator.FSharp.hof_returning_closure")
{
    // HOF returning closure that captures function ref
    CHECK(
        executeSourceAndGetOutput(
            "let double x = x * 2\nlet make_caller f = fun x -> f x\nlet g = make_caller double\nprint (g 5)")
        == "10");
}

TEST_CASE("IRGenerator.FSharp.hof_multiple_function_args")
{
    // Multiple function arguments
    CHECK(executeSourceAndGetOutput("let double x = x * 2\nlet add1 x = x + 1\nlet apply2 f g x = g (f "
                                    "x)\nprint (apply2 double add1 3)")
          == "7");
}

TEST_CASE("IRGenerator.FSharp.hof_pipeline")
{
    // HOF in pipeline
    CHECK(executeSourceAndGetOutput("let double x = x * 2\nlet apply f x = f x\nprint (5 |> apply double)")
          == "10");
}

TEST_CASE("IRGenerator.FSharp.hof_string_function")
{
    // HOF with string function
    CHECK(executeSourceAndGetOutput(
              "let shout s = s + \"!\"\nlet apply f x = f x\nprint (apply shout \"hello\")")
          == "hello!");
}

TEST_CASE("IRGenerator.FSharp.hof_nested_partial_application")
{
    // Nested partial application: (add 1) passed as function arg
    CHECK(executeSourceAndGetOutput(
              "let add x y = x + y\nlet apply f x = f x\nlet g = apply (add 1)\nprint (g 5)")
          == "6");
}

TEST_CASE("IRGenerator.FSharp.hof_function_alias")
{
    // Function alias through HOF (identity returning a function)
    CHECK(executeSourceAndGetOutput("let double x = x * 2\nlet id f = f\nlet g = id double\nprint (g 5)")
          == "10");
}

// =============================================================================
// Record Types
// =============================================================================

TEST_CASE("IRGenerator.FSharp.record_type_def_and_field_access")
{
    // Basic record type definition, creation, and field access
    CHECK(executeSourceAndGetOutput("type Point = { x: int; y: int }\n"
                                    "let p = { x = 10; y = 20 }\n"
                                    "print p.x")
          == "10");
}

TEST_CASE("IRGenerator.FSharp.record_field_access_second_field")
{
    CHECK(executeSourceAndGetOutput("type Point = { x: int; y: int }\n"
                                    "let p = { x = 10; y = 20 }\n"
                                    "print p.y")
          == "20");
}

TEST_CASE("IRGenerator.FSharp.record_field_arithmetic")
{
    // Use record fields in arithmetic
    CHECK(executeSourceAndGetOutput("type Point = { x: int; y: int }\n"
                                    "let p = { x = 3; y = 4 }\n"
                                    "print (p.x + p.y)")
          == "7");
}

TEST_CASE("IRGenerator.FSharp.record_update_basic")
{
    // Record update creates a new record with one field changed
    CHECK(executeSourceAndGetOutput("type Person = { name: int; age: int }\n"
                                    "let p = { name = 1; age = 30 }\n"
                                    "let q = { p with age = 31 }\n"
                                    "print q.age")
          == "31");
}

TEST_CASE("IRGenerator.FSharp.record_update_preserves_unchanged")
{
    // Record update preserves fields that aren't overridden
    CHECK(executeSourceAndGetOutput("type Person = { name: int; age: int }\n"
                                    "let p = { name = 42; age = 30 }\n"
                                    "let q = { p with age = 31 }\n"
                                    "print q.name")
          == "42");
}

TEST_CASE("IRGenerator.FSharp.record_pattern_match_punning")
{
    // Record pattern matching with field punning
    CHECK(executeSourceAndGetOutput("type Point = { x: int; y: int }\n"
                                    "let p = { x = 5; y = 10 }\n"
                                    "let r = match p with | { x; y } -> x + y\n"
                                    "print r")
          == "15");
}

TEST_CASE("IRGenerator.FSharp.record_pattern_match_explicit_binding")
{
    // Record pattern matching with explicit variable binding
    CHECK(executeSourceAndGetOutput("type Point = { x: int; y: int }\n"
                                    "let p = { x = 5; y = 10 }\n"
                                    "let r = match p with | { x = a; y = b } -> a * b\n"
                                    "print r")
          == "50");
}

TEST_CASE("IRGenerator.FSharp.record_passed_to_function")
{
    // Record passed as function argument
    CHECK(executeSourceAndGetOutput("type Point = { x: int; y: int }\n"
                                    "let sum_point p = p.x + p.y\n"
                                    "let p = { x = 3; y = 7 }\n"
                                    "print (sum_point p)")
          == "10");
}

TEST_CASE("IRGenerator.FSharp.record_returned_from_function")
{
    // Record created and returned from a function
    CHECK(executeSourceAndGetOutput("type Point = { x: int; y: int }\n"
                                    "let make_point a b = { x = a; y = b }\n"
                                    "let p = make_point 100 200\n"
                                    "print p.x")
          == "100");
}

TEST_CASE("IRGenerator.FSharp.record_update_with_expression")
{
    // Record update using an expression involving the original record
    CHECK(executeSourceAndGetOutput("type Counter = { value: int }\n"
                                    "let c = { value = 10 }\n"
                                    "let c2 = { c with value = c.value + 1 }\n"
                                    "print c2.value")
          == "11");
}

TEST_CASE("IRGenerator.FSharp.record_multiple_types")
{
    // Multiple record types in the same program
    CHECK(executeSourceAndGetOutput("type Point = { x: int; y: int }\n"
                                    "type Size = { w: int; h: int }\n"
                                    "let p = { x = 1; y = 2 }\n"
                                    "let s = { w = 10; h = 20 }\n"
                                    "print (p.x + s.w)")
          == "11");
}

TEST_CASE("IRGenerator.FSharp.record_let_destructure")
{
    // Record destructuring in let binding
    CHECK(executeSourceAndGetOutput("type Point = { x: int; y: int }\n"
                                    "let p = { x = 7; y = 8 }\n"
                                    "let { x; y } = p\n"
                                    "print (x + y)")
          == "15");
}

TEST_CASE("IRGenerator.FSharp.record_string_field_access")
{
    // String field access via dot notation
    CHECK(executeSourceAndGetOutput("type Person = { name: str; age: int }\n"
                                    "let p = { name = \"Alice\"; age = 30 }\n"
                                    "print p.name")
          == "Alice");
}

TEST_CASE("IRGenerator.FSharp.record_string_field_destructure")
{
    // String field via let destructuring
    CHECK(executeSourceAndGetOutput("type Person = { name: str; age: int }\n"
                                    "let p = { name = \"Bob\"; age = 25 }\n"
                                    "let { name; age } = p\n"
                                    "print name")
          == "Bob");
}

TEST_CASE("IRGenerator.FSharp.record_string_field_match")
{
    // String field via pattern matching
    CHECK(executeSourceAndGetOutput("type Person = { name: str; age: int }\n"
                                    "let p = { name = \"Charlie\"; age = 35 }\n"
                                    "print (match p with | { name; age } -> name)")
          == "Charlie");
}

TEST_CASE("IRGenerator.FSharp.record_string_field_fstring")
{
    // String field used in f-string interpolation
    CHECK(executeSourceAndGetOutput("type Person = { name: str; age: int }\n"
                                    "let p = { name = \"Diana\"; age = 28 }\n"
                                    "print $\"Hello, {p.name}!\"")
          == "Hello, Diana!");
}

TEST_CASE("IRGenerator.FSharp.record_println_whole_record")
{
    // Printing a whole record should produce formatted output
    CHECK(executeSourceAndGetOutput("type Point = { x: int; y: int }\n"
                                    "let p = { x = 3; y = 4 }\n"
                                    "print p")
          == "{ x = 3; y = 4 }");
}

TEST_CASE("IRGenerator.FSharp.record_update_println")
{
    // Print a record created via record update
    CHECK(executeSourceAndGetOutput("type Point = { x: int; y: int }\n"
                                    "let p1 = { x = 3; y = 4 }\n"
                                    "let p2 = { p1 with x = p1.x + 1; y = p1.y + 2 }\n"
                                    "print p2")
          == "{ x = 4; y = 6 }");
}

TEST_CASE("IRGenerator.FSharp.record_update_field_access_after")
{
    // Access fields of a record created via record update
    CHECK(executeSourceAndGetOutput("type Point = { x: int; y: int }\n"
                                    "let p1 = { x = 3; y = 4 }\n"
                                    "let p2 = { p1 with x = p1.x + 1; y = p1.y + 2 }\n"
                                    "print p2.x\n"
                                    "print p2.y")
          == "46");
}

TEST_CASE("IRGenerator.FSharp.record_println_with_string_fields")
{
    // Printing a record with string fields should show string values, not pointers
    CHECK(executeSourceAndGetOutput("type Person = { name: str; age: int }\n"
                                    "let p = { name = \"Alice\"; age = 30 }\n"
                                    "print p")
          == "{ name = Alice; age = 30 }");
}

TEST_CASE("IRGenerator.FSharp.record_destructure_then_update")
{
    // Let destructuring followed by record update on the same type
    CHECK(executeSourceAndGetOutput("type Point = { x: int; y: int }\n"
                                    "let p1 = { x = 3; y = 4 }\n"
                                    "let { x; y } = p1\n"
                                    "let p2 = { p1 with x = p1.x + 1; y = p1.y + 2 }\n"
                                    "print p2")
          == "{ x = 4; y = 6 }");
}

TEST_CASE("IRGenerator.FSharp.record_destructure_two_types_then_update")
{
    // Let destructuring on one record type followed by record update on another type
    CHECK(executeSourceAndGetOutput("type Person = { name: str; age: int }\n"
                                    "let alice = { name = \"Alice\"; age = 30 }\n"
                                    "let { name; age } = alice\n"
                                    "type Point = { x: int; y: int }\n"
                                    "let p1 = { x = 3; y = 4 }\n"
                                    "let p2 = { p1 with x = p1.x + 1; y = p1.y + 2 }\n"
                                    "print p2")
          == "{ x = 4; y = 6 }");
}

TEST_CASE("IRGenerator.FSharp.record_update_with_inlined_match_function")
{
    // Inlined untyped function with match creates blocks during record update value codegen.
    CHECK(executeSourceAndGetOutput("type Point = { x: int; y: int }\n"
                                    "let double n = match n with | 0 -> 0 | n -> n * 2\n"
                                    "let p1 = { x = 3; y = 4 }\n"
                                    "let p2 = { p1 with x = double (p1.x) }\n"
                                    "print p2")
          == "{ x = 6; y = 4 }");
}

TEST_CASE("IRGenerator.FSharp.record_update_with_multiple_inlined_match_functions")
{
    // Multiple update fields each calling inlined match functions.
    CHECK(executeSourceAndGetOutput("type Point = { x: int; y: int }\n"
                                    "let double n = match n with | 0 -> 0 | n -> n * 2\n"
                                    "let p1 = { x = 3; y = 4 }\n"
                                    "let p2 = { p1 with x = double (p1.x); y = double (p1.y) }\n"
                                    "print p2")
          == "{ x = 6; y = 8 }");
}

// =============================================================================
// Discriminated Unions (ADTs)
// =============================================================================

TEST_CASE("IRGenerator.FSharp.union_type_basic_enum")
{
    // Unit constructors only (enum-like ADT), match all variants
    CHECK(executeSourceAndGetOutput("type Color =\n"
                                    "    | Red\n"
                                    "    | Green\n"
                                    "    | Blue\n"
                                    "let c = Green\n"
                                    "let r = match c with\n"
                                    "    | Red -> 1\n"
                                    "    | Green -> 2\n"
                                    "    | Blue -> 3\n"
                                    "print r")
          == "2");
}

TEST_CASE("IRGenerator.FSharp.union_type_single_payload")
{
    // Single-payload constructor, match + extract
    CHECK(executeSourceAndGetOutput("type Shape =\n"
                                    "    | Circle of int\n"
                                    "    | Point\n"
                                    "let s = Circle 42\n"
                                    "let r = match s with\n"
                                    "    | Circle radius -> radius\n"
                                    "    | Point -> 0\n"
                                    "print r")
          == "42");
}

TEST_CASE("IRGenerator.FSharp.union_type_tuple_payload")
{
    // Multi-slot constructor (Rectangle of int * int), match + extract both slots
    CHECK(executeSourceAndGetOutput("type Shape =\n"
                                    "    | Rectangle of int * int\n"
                                    "    | Point\n"
                                    "let s = Rectangle (10, 20)\n"
                                    "let r = match s with\n"
                                    "    | Rectangle (w, h) -> w + h\n"
                                    "    | Point -> 0\n"
                                    "print r")
          == "30");
}

TEST_CASE("IRGenerator.FSharp.union_type_with_function")
{
    // Pass ADT to function, match inside function
    CHECK(executeSourceAndGetOutput("type Shape =\n"
                                    "    | Circle of int\n"
                                    "    | Point\n"
                                    "let area s = match s with\n"
                                    "    | Circle r -> r * r\n"
                                    "    | Point -> 0\n"
                                    "print (area (Circle 5))")
          == "25");
}

TEST_CASE("IRGenerator.FSharp.union_type_wildcard")
{
    // Match with wildcard fallback
    CHECK(executeSourceAndGetOutput("type Color =\n"
                                    "    | Red\n"
                                    "    | Green\n"
                                    "    | Blue\n"
                                    "let c = Blue\n"
                                    "let r = match c with\n"
                                    "    | Red -> 1\n"
                                    "    | _ -> 0\n"
                                    "print r")
          == "0");
}

TEST_CASE("IRGenerator.FSharp.union_type_mixed_with_option")
{
    // Ensure Option/Result still works alongside user ADTs
    CHECK(executeSourceAndGetOutput("type Color =\n"
                                    "    | Red\n"
                                    "    | Green\n"
                                    "let c = Red\n"
                                    "let o = Some 42\n"
                                    "let r = match o with\n"
                                    "    | Some x -> x\n"
                                    "    | None -> 0\n"
                                    "print r")
          == "42");
}

TEST_CASE("IRGenerator.FSharp.union_type_int_payload")
{
    // int payloads
    CHECK(executeSourceAndGetOutput("type Expr =\n"
                                    "    | Num of int\n"
                                    "    | Neg of int\n"
                                    "let e = Num 7\n"
                                    "let r = match e with\n"
                                    "    | Num n -> n\n"
                                    "    | Neg n -> 0 - n\n"
                                    "print r")
          == "7");
}

TEST_CASE("IRGenerator.FSharp.union_type_multiple_arms_with_payloads")
{
    // Direct match with different constructor variants — no function inlining
    CHECK(executeSourceAndGetOutput("type Shape =\n"
                                    "    | Circle of int\n"
                                    "    | Rectangle of int * int\n"
                                    "    | Point\n"
                                    "let s = Circle 5\n"
                                    "let r = match s with\n"
                                    "    | Circle r -> r * r\n"
                                    "    | Rectangle (w, h) -> w * h\n"
                                    "    | Point -> 0\n"
                                    "print r")
          == "25");
    CHECK(executeSourceAndGetOutput("type Shape =\n"
                                    "    | Circle of int\n"
                                    "    | Rectangle of int * int\n"
                                    "    | Point\n"
                                    "let s = Rectangle (3, 4)\n"
                                    "let r = match s with\n"
                                    "    | Circle r -> r * r\n"
                                    "    | Rectangle (w, h) -> w * h\n"
                                    "    | Point -> 0\n"
                                    "print r")
          == "12");
    CHECK(executeSourceAndGetOutput("type Shape =\n"
                                    "    | Circle of int\n"
                                    "    | Rectangle of int * int\n"
                                    "    | Point\n"
                                    "let s = Point\n"
                                    "let r = match s with\n"
                                    "    | Circle r -> r * r\n"
                                    "    | Rectangle (w, h) -> w * h\n"
                                    "    | Point -> 0\n"
                                    "print r")
          == "0");
}

TEST_CASE("IRGenerator.FSharp.union_type_unit_constructor_match")
{
    // Unit constructor matches correctly, no payload extraction
    CHECK(executeSourceAndGetOutput("type Option2 =\n"
                                    "    | Nothing\n"
                                    "    | Just of int\n"
                                    "let x = Nothing\n"
                                    "let r = match x with\n"
                                    "    | Nothing -> 0\n"
                                    "    | Just n -> n\n"
                                    "print r")
          == "0");
}

// =============================================================================
// List standard library: head, tail, length, isEmpty
// =============================================================================

TEST_CASE("IRGenerator.FSharp.list_head_cons")
{
    // head [1;2;3] → Some 1
    CHECK(executeSourceAndGetOutput("let r = match head [1;2;3] with\n"
                                    "    | Some v -> v\n"
                                    "    | None -> 0\n"
                                    "print r")
          == "1");
}

TEST_CASE("IRGenerator.FSharp.list_head_empty")
{
    // head [] → None
    CHECK(executeSourceAndGetOutput("let r = match head [] with\n"
                                    "    | Some v -> v\n"
                                    "    | None -> 0\n"
                                    "print r")
          == "0");
}

TEST_CASE("IRGenerator.FSharp.list_head_pipeline")
{
    // [42] |> head → Some 42
    CHECK(executeSourceAndGetOutput("let r = match [42] |> head with\n"
                                    "    | Some v -> v\n"
                                    "    | None -> 0\n"
                                    "print r")
          == "42");
}

TEST_CASE("IRGenerator.FSharp.list_tail_basic")
{
    CHECK(executeSourceAndGetOutput("print (tail [1;2;3])") == "[2; 3]");
}

TEST_CASE("IRGenerator.FSharp.list_tail_empty")
{
    CHECK(executeSourceAndGetOutput("print (tail [])") == "[]");
}

TEST_CASE("IRGenerator.FSharp.list_tail_single")
{
    CHECK(executeSourceAndGetOutput("print (tail [1])") == "[]");
}

TEST_CASE("IRGenerator.FSharp.list_tail_pipeline")
{
    CHECK(executeSourceAndGetOutput("print ([1;2;3] |> tail)") == "[2; 3]");
}

TEST_CASE("IRGenerator.FSharp.list_length_basic")
{
    CHECK(executeSourceAndGetOutput("print (length [1;2;3])") == "3");
}

TEST_CASE("IRGenerator.FSharp.list_length_empty")
{
    CHECK(executeSourceAndGetOutput("print (length [])") == "0");
}

TEST_CASE("IRGenerator.FSharp.list_length_pipeline")
{
    CHECK(executeSourceAndGetOutput("print ([1;2;3] |> length)") == "3");
}

TEST_CASE("IRGenerator.FSharp.list_isEmpty_true")
{
    CHECK(executeSourceAndGetOutput("print (isEmpty [])") == "true");
}

TEST_CASE("IRGenerator.FSharp.list_isEmpty_false")
{
    CHECK(executeSourceAndGetOutput("print (isEmpty [1;2])") == "false");
}

TEST_CASE("IRGenerator.FSharp.list_isEmpty_pipeline")
{
    CHECK(executeSourceAndGetOutput("print ([] |> isEmpty)") == "true");
}

TEST_CASE("IRGenerator.FSharp.list_pipeline_chained")
{
    // [1;2;3] |> tail |> length → 2
    CHECK(executeSourceAndGetOutput("print ([1;2;3] |> tail |> length)") == "2");
}

// =============================================================================
// nth Builtin
// =============================================================================

TEST_CASE("IRGenerator.FSharp.list_nth_first")
{
    CHECK(executeSourceAndGetOutput("let r = match nth 0 [10; 20; 30] with\n"
                                    "    | Some v -> v\n"
                                    "    | None -> -1\n"
                                    "print r")
          == "10");
}

TEST_CASE("IRGenerator.FSharp.list_nth_middle")
{
    CHECK(executeSourceAndGetOutput("let r = match nth 1 [10; 20; 30] with\n"
                                    "    | Some v -> v\n"
                                    "    | None -> -1\n"
                                    "print r")
          == "20");
}

TEST_CASE("IRGenerator.FSharp.list_nth_last")
{
    CHECK(executeSourceAndGetOutput("let r = match nth 2 [10; 20; 30] with\n"
                                    "    | Some v -> v\n"
                                    "    | None -> -1\n"
                                    "print r")
          == "30");
}

TEST_CASE("IRGenerator.FSharp.list_nth_out_of_bounds")
{
    CHECK(executeSourceAndGetOutput("let r = match nth 5 [10; 20; 30] with\n"
                                    "    | Some v -> v\n"
                                    "    | None -> -1\n"
                                    "print r")
          == "-1");
}

TEST_CASE("IRGenerator.FSharp.list_nth_empty")
{
    CHECK(executeSourceAndGetOutput("let r = match nth 0 [] with\n"
                                    "    | Some v -> v\n"
                                    "    | None -> -1\n"
                                    "print r")
          == "-1");
}

TEST_CASE("IRGenerator.FSharp.list_nth_pipeline")
{
    CHECK(executeSourceAndGetOutput("let r = match [1;2;3] |> nth 1 with\n"
                                    "    | Some v -> v\n"
                                    "    | None -> -1\n"
                                    "print r")
          == "2");
}

// =============================================================================
// last Builtin
// =============================================================================

TEST_CASE("IRGenerator.FSharp.list_last_basic")
{
    CHECK(executeSourceAndGetOutput("let r = match last [1; 2; 3] with\n"
                                    "    | Some v -> v\n"
                                    "    | None -> -1\n"
                                    "print r")
          == "3");
}

TEST_CASE("IRGenerator.FSharp.list_last_single")
{
    CHECK(executeSourceAndGetOutput("let r = match last [42] with\n"
                                    "    | Some v -> v\n"
                                    "    | None -> -1\n"
                                    "print r")
          == "42");
}

TEST_CASE("IRGenerator.FSharp.list_last_empty")
{
    CHECK(executeSourceAndGetOutput("let r = match last [] with\n"
                                    "    | Some v -> v\n"
                                    "    | None -> -1\n"
                                    "print r")
          == "-1");
}

TEST_CASE("IRGenerator.FSharp.list_last_pipeline")
{
    CHECK(executeSourceAndGetOutput("let r = match [1;2;3] |> last with\n"
                                    "    | Some v -> v\n"
                                    "    | None -> -1\n"
                                    "print r")
          == "3");
}

// =============================================================================
// replicate Builtin
// =============================================================================

TEST_CASE("IRGenerator.FSharp.list_replicate_basic")
{
    CHECK(executeSourceAndGetOutput("print (replicate 3 42)") == "[42; 42; 42]");
}

TEST_CASE("IRGenerator.FSharp.list_replicate_zero")
{
    CHECK(executeSourceAndGetOutput("print (replicate 0 1)") == "[]");
}

TEST_CASE("IRGenerator.FSharp.list_replicate_one")
{
    CHECK(executeSourceAndGetOutput("print (replicate 1 99)") == "[99]");
}

TEST_CASE("IRGenerator.FSharp.list_replicate_pipeline")
{
    // 7 |> replicate 3 → [7; 7; 7]
    CHECK(executeSourceAndGetOutput("print (7 |> replicate 3)") == "[7; 7; 7]");
}

// =============================================================================
// Character Ranges
// =============================================================================

TEST_CASE("IRGenerator.FSharp.char_range_basic")
{
    CHECK(executeSourceAndGetOutput("print ['a'..'e']") == "[\"a\"; \"b\"; \"c\"; \"d\"; \"e\"]");
}

TEST_CASE("IRGenerator.FSharp.char_range_uppercase")
{
    CHECK(executeSourceAndGetOutput("print ['A'..'E']") == "[\"A\"; \"B\"; \"C\"; \"D\"; \"E\"]");
}

TEST_CASE("IRGenerator.FSharp.char_range_digits")
{
    CHECK(executeSourceAndGetOutput("print ['0'..'5']") == "[\"0\"; \"1\"; \"2\"; \"3\"; \"4\"; \"5\"]");
}

TEST_CASE("IRGenerator.FSharp.char_range_single")
{
    CHECK(executeSourceAndGetOutput("print ['x'..'x']") == "[\"x\"]");
}

TEST_CASE("IRGenerator.FSharp.char_range_empty")
{
    // Reverse range with no step → empty list
    CHECK(executeSourceAndGetOutput("print ['z'..'a']") == "[]");
}

// =============================================================================
// String Standard Library
// =============================================================================

TEST_CASE("IRGenerator.FSharp.string_trim_basic")
{
    CHECK(executeSourceAndGetOutput(R"(print (trim "  hello  "))") == "hello");
}

TEST_CASE("IRGenerator.FSharp.string_trim_empty")
{
    CHECK(executeSourceAndGetOutput(R"(print (trim "   "))") == "");
}

TEST_CASE("IRGenerator.FSharp.string_trim_pipeline")
{
    CHECK(executeSourceAndGetOutput(R"(print ("  hello  " |> trim))") == "hello");
}

TEST_CASE("IRGenerator.FSharp.string_toLower_basic")
{
    CHECK(executeSourceAndGetOutput(R"(print (toLower "HELLO"))") == "hello");
}

TEST_CASE("IRGenerator.FSharp.string_toLower_pipeline")
{
    CHECK(executeSourceAndGetOutput(R"(print ("HELLO" |> toLower))") == "hello");
}

TEST_CASE("IRGenerator.FSharp.string_toUpper_basic")
{
    CHECK(executeSourceAndGetOutput(R"(print (toUpper "hello"))") == "HELLO");
}

TEST_CASE("IRGenerator.FSharp.string_toUpper_pipeline")
{
    CHECK(executeSourceAndGetOutput(R"(print ("hello" |> toUpper))") == "HELLO");
}

TEST_CASE("IRGenerator.FSharp.string_contains_true")
{
    CHECK(executeSourceAndGetOutput(R"(print (contains "hello world" "world"))") == "true");
}

TEST_CASE("IRGenerator.FSharp.string_contains_false")
{
    CHECK(executeSourceAndGetOutput(R"(print (contains "hello" "xyz"))") == "false");
}

TEST_CASE("IRGenerator.FSharp.string_startsWith_true")
{
    CHECK(executeSourceAndGetOutput(R"(print (startsWith "hello" "hel"))") == "true");
}

TEST_CASE("IRGenerator.FSharp.string_startsWith_false")
{
    CHECK(executeSourceAndGetOutput(R"(print (startsWith "hello" "xyz"))") == "false");
}

TEST_CASE("IRGenerator.FSharp.string_endsWith_true")
{
    CHECK(executeSourceAndGetOutput(R"(print (endsWith "hello" "llo"))") == "true");
}

TEST_CASE("IRGenerator.FSharp.string_endsWith_false")
{
    CHECK(executeSourceAndGetOutput(R"(print (endsWith "hello" "xyz"))") == "false");
}

TEST_CASE("IRGenerator.FSharp.string_replace_basic")
{
    CHECK(executeSourceAndGetOutput(R"(print (replace "l" "r" "hello"))") == "herro");
}

TEST_CASE("IRGenerator.FSharp.string_replace_multi")
{
    CHECK(executeSourceAndGetOutput(R"(print (replace "," "" "a,b,c"))") == "abc");
}

TEST_CASE("IRGenerator.FSharp.string_replace_no_match")
{
    CHECK(executeSourceAndGetOutput(R"(print (replace "x" "y" "hello"))") == "hello");
}

TEST_CASE("IRGenerator.FSharp.string_split_basic")
{
    CHECK(executeSourceAndGetOutput(R"(let parts = split "," "a,b,c"; print (length parts))") == "3");
}

TEST_CASE("IRGenerator.FSharp.string_split_head")
{
    CHECK(executeSourceAndGetOutput(R"(let parts = split "," "a,b,c"; print (head parts ?| ""))") == "a");
}

TEST_CASE("IRGenerator.FSharp.string_join_basic")
{
    CHECK(executeSourceAndGetOutput(R"(print (join "-" (split "," "a,b,c")))") == "a-b-c");
}

TEST_CASE("IRGenerator.FSharp.string_join_empty")
{
    CHECK(executeSourceAndGetOutput(R"(print (join "," []))") == "");
}

TEST_CASE("IRGenerator.FSharp.string_join_pipeline")
{
    CHECK(executeSourceAndGetOutput(R"(let r = ["a"; "b"; "c"] |> join "-"; print r)") == "a-b-c");
}

// =============================================================================
// Higher-Order List Functions: map
// =============================================================================

TEST_CASE("IRGenerator.FSharp.list_map_basic")
{
    CHECK(executeSourceAndGetOutput("print (map (fun x -> x * 2) [1; 2; 3])") == "[2; 4; 6]");
}

TEST_CASE("IRGenerator.FSharp.list_map_named_function")
{
    CHECK(executeSourceAndGetOutput("let double x = x * 2\nprint (map double [1; 2; 3])") == "[2; 4; 6]");
}

TEST_CASE("IRGenerator.FSharp.list_map_empty")
{
    CHECK(executeSourceAndGetOutput("print (map (fun x -> x * 2) [])") == "[]");
}

TEST_CASE("IRGenerator.FSharp.list_map_single")
{
    CHECK(executeSourceAndGetOutput("print (map (fun x -> x + 10) [5])") == "[15]");
}

TEST_CASE("IRGenerator.FSharp.list_map_identity")
{
    CHECK(executeSourceAndGetOutput("print (map (fun x -> x) [1; 2; 3])") == "[1; 2; 3]");
}

TEST_CASE("IRGenerator.FSharp.list_map_pipeline")
{
    CHECK(executeSourceAndGetOutput("print ([1; 2; 3] |> map (fun x -> x * 2))") == "[2; 4; 6]");
}

TEST_CASE("IRGenerator.FSharp.list_map_chained")
{
    CHECK(executeSourceAndGetOutput("print ([1; 2; 3] |> map (fun x -> x + 1) |> map (fun x -> x * 2))")
          == "[4; 6; 8]");
}

TEST_CASE("IRGenerator.FSharp.list_map_partial")
{
    CHECK(executeSourceAndGetOutput("let doubleAll = map (fun x -> x * 2)\nprint (doubleAll [1; 2; 3])")
          == "[2; 4; 6]");
}

TEST_CASE("IRGenerator.FSharp.list_map_closure")
{
    CHECK(executeSourceAndGetOutput("let offset = 10\nprint (map (fun x -> x + offset) [1; 2; 3])")
          == "[11; 12; 13]");
}

TEST_CASE("IRGenerator.FSharp.list_map_negate")
{
    CHECK(executeSourceAndGetOutput("print (map (fun x -> 0 - x) [1; 2; 3])") == "[-1; -2; -3]");
}

TEST_CASE("IRGenerator.FSharp.list_map_placeholder")
{
    CHECK(executeSourceAndGetOutput("print (map (_ * 2) [1; 2; 3])") == "[2; 4; 6]");
}

// =============================================================================
// Higher-Order List Functions: filter
// =============================================================================

TEST_CASE("IRGenerator.FSharp.list_filter_basic")
{
    CHECK(executeSourceAndGetOutput("print (filter (fun x -> x % 2 == 0) [1; 2; 3; 4; 5; 6])")
          == "[2; 4; 6]");
}

TEST_CASE("IRGenerator.FSharp.list_filter_all_pass")
{
    CHECK(executeSourceAndGetOutput("print (filter (fun x -> x > 0) [1; 2; 3])") == "[1; 2; 3]");
}

TEST_CASE("IRGenerator.FSharp.list_filter_none_pass")
{
    CHECK(executeSourceAndGetOutput("print (filter (fun x -> x > 10) [1; 2; 3])") == "[]");
}

TEST_CASE("IRGenerator.FSharp.list_filter_empty")
{
    CHECK(executeSourceAndGetOutput("print (filter (fun x -> x > 0) [])") == "[]");
}

TEST_CASE("IRGenerator.FSharp.list_filter_single_pass")
{
    CHECK(executeSourceAndGetOutput("print (filter (fun x -> x > 0) [5])") == "[5]");
}

TEST_CASE("IRGenerator.FSharp.list_filter_single_fail")
{
    CHECK(executeSourceAndGetOutput("print (filter (fun x -> x > 10) [5])") == "[]");
}

TEST_CASE("IRGenerator.FSharp.list_filter_pipeline")
{
    CHECK(executeSourceAndGetOutput("print ([1; 2; 3; 4; 5; 6] |> filter (fun x -> x % 2 == 0))")
          == "[2; 4; 6]");
}

TEST_CASE("IRGenerator.FSharp.list_filter_partial")
{
    CHECK(executeSourceAndGetOutput(
              "let getEvens = filter (fun x -> x % 2 == 0)\nprint (getEvens [1; 2; 3; 4])")
          == "[2; 4]");
}

TEST_CASE("IRGenerator.FSharp.list_filter_preserves_order")
{
    CHECK(executeSourceAndGetOutput("print (filter (fun x -> x != 3) [1; 2; 3; 4; 5])") == "[1; 2; 4; 5]");
}

TEST_CASE("IRGenerator.FSharp.list_filter_placeholder")
{
    CHECK(executeSourceAndGetOutput("print (filter (_ > 2) [1; 2; 3; 4; 5])") == "[3; 4; 5]");
}

// toBool uses shell semantics: 0/empty = truthy (exit code 0 = success)
// These tests verify toBool handles Float and String without crashing (SIGABRT).

TEST_CASE("IRGenerator.FSharp.toBool_float_nonzero")
{
    // non-zero float is falsy in shell semantics (1.5 != 0.0)
    CHECK(executeSourceAndGetOutput("if 1.5 then print \"yes\" else print \"no\"") == "no");
}

TEST_CASE("IRGenerator.FSharp.toBool_float_zero")
{
    // zero float is truthy in shell semantics (0.0 == 0.0)
    CHECK(executeSourceAndGetOutput("if 0.0 then print \"yes\" else print \"no\"") == "yes");
}

TEST_CASE("IRGenerator.FSharp.toBool_string_nonempty")
{
    // non-empty string is falsy in shell semantics ("hello" != "")
    CHECK(executeSourceAndGetOutput(R"(if "hello" then print "yes" else print "no")") == "no");
}

TEST_CASE("IRGenerator.FSharp.toBool_string_empty")
{
    // empty string is truthy in shell semantics ("" == "")
    CHECK(executeSourceAndGetOutput(R"(if "" then print "yes" else print "no")") == "yes");
}

TEST_CASE("IRGenerator.FSharp.toBool_not_float")
{
    // not(1.5): toBool(1.5) = false (falsy), BNot(false) = true
    CHECK(executeSourceAndGetOutput("print (not 1.5)") == "true");
}

TEST_CASE("IRGenerator.FSharp.toBool_not_string")
{
    // not("hello"): toBool("hello") = false (falsy), BNot(false) = true
    CHECK(executeSourceAndGetOutput(R"(print (not "hello"))") == "true");
}

// =============================================================================
// Higher-Order List Functions: fold
// =============================================================================

TEST_CASE("IRGenerator.FSharp.list_fold_sum")
{
    CHECK(executeSourceAndGetOutput("print (fold 0 (fun acc x -> acc + x) [1; 2; 3; 4; 5])") == "15");
}

TEST_CASE("IRGenerator.FSharp.list_fold_product")
{
    CHECK(executeSourceAndGetOutput("print (fold 1 (fun acc x -> acc * x) [1; 2; 3; 4; 5])") == "120");
}

TEST_CASE("IRGenerator.FSharp.list_fold_empty")
{
    CHECK(executeSourceAndGetOutput("print (fold 42 (fun acc x -> acc + x) [])") == "42");
}

TEST_CASE("IRGenerator.FSharp.list_fold_single")
{
    CHECK(executeSourceAndGetOutput("print (fold 0 (fun acc x -> acc + x) [7])") == "7");
}

TEST_CASE("IRGenerator.FSharp.list_fold_count")
{
    CHECK(executeSourceAndGetOutput("print (fold 0 (fun acc x -> acc + 1) [10; 20; 30])") == "3");
}

TEST_CASE("IRGenerator.FSharp.list_fold_max")
{
    CHECK(executeSourceAndGetOutput(
              "print (fold 0 (fun acc x -> if x > acc then x else acc) [3; 1; 4; 1; 5; 9])")
          == "9");
}

TEST_CASE("IRGenerator.FSharp.list_fold_pipeline")
{
    CHECK(executeSourceAndGetOutput("print ([1; 2; 3; 4; 5] |> fold 0 (fun acc x -> acc + x))") == "15");
}

TEST_CASE("IRGenerator.FSharp.list_fold_left_associative")
{
    // ((100 - 1) - 2) - 3 = 94
    CHECK(executeSourceAndGetOutput("print (fold 100 (fun acc x -> acc - x) [1; 2; 3])") == "94");
}

TEST_CASE("IRGenerator.FSharp.list_fold_partial")
{
    CHECK(executeSourceAndGetOutput("let sum = fold 0 (fun acc x -> acc + x)\nprint (sum [1; 2; 3])") == "6");
}

TEST_CASE("IRGenerator.FSharp.list_fold_partial_init_only")
{
    CHECK(
        executeSourceAndGetOutput("let sumFrom0 = fold 0\nprint (sumFrom0 (fun acc x -> acc + x) [1; 2; 3])")
        == "6");
}

// =============================================================================
// Higher-Order List Functions: reduce
// =============================================================================

TEST_CASE("IRGenerator.FSharp.list_reduce_sum")
{
    CHECK(executeSourceAndGetOutput("let r = reduce (fun a b -> a + b) [1; 2; 3; 4]\n"
                                    "match r with\n"
                                    "    | Some v -> print v\n"
                                    "    | None -> print \"none\"")
          == "10");
}

TEST_CASE("IRGenerator.FSharp.list_reduce_product")
{
    CHECK(executeSourceAndGetOutput("let r = reduce (fun a b -> a * b) [1; 2; 3; 4]\n"
                                    "match r with\n"
                                    "    | Some v -> print v\n"
                                    "    | None -> print \"none\"")
          == "24");
}

TEST_CASE("IRGenerator.FSharp.list_reduce_single")
{
    CHECK(executeSourceAndGetOutput("let r = reduce (fun a b -> a + b) [42]\n"
                                    "match r with\n"
                                    "    | Some v -> print v\n"
                                    "    | None -> print \"none\"")
          == "42");
}

TEST_CASE("IRGenerator.FSharp.list_reduce_empty")
{
    CHECK(executeSourceAndGetOutput("let r = reduce (fun a b -> a + b) []\n"
                                    "match r with\n"
                                    "    | Some v -> print v\n"
                                    "    | None -> print \"none\"")
          == "none");
}

TEST_CASE("IRGenerator.FSharp.list_reduce_max")
{
    CHECK(executeSourceAndGetOutput("let r = reduce (fun a b -> if a > b then a else b) [3; 1; 4; 1; 5; 9]\n"
                                    "match r with\n"
                                    "    | Some v -> print v\n"
                                    "    | None -> print \"none\"")
          == "9");
}

TEST_CASE("IRGenerator.FSharp.list_reduce_pipeline")
{
    CHECK(executeSourceAndGetOutput("let r = [1; 2; 3; 4; 5] |> reduce (fun a b -> a + b)\n"
                                    "match r with\n"
                                    "    | Some v -> print v\n"
                                    "    | None -> print \"none\"")
          == "15");
}

TEST_CASE("IRGenerator.FSharp.list_reduce_left_associative")
{
    // (10 - 3) - 2 = 5
    CHECK(executeSourceAndGetOutput("let r = reduce (fun a b -> a - b) [10; 3; 2]\n"
                                    "match r with\n"
                                    "    | Some v -> print v\n"
                                    "    | None -> print \"none\"")
          == "5");
}

// =============================================================================
// Higher-Order List Functions: reverse
// =============================================================================

TEST_CASE("IRGenerator.FSharp.list_reverse_basic")
{
    CHECK(executeSourceAndGetOutput("print (reverse [1; 2; 3])") == "[3; 2; 1]");
}

TEST_CASE("IRGenerator.FSharp.list_reverse_single")
{
    CHECK(executeSourceAndGetOutput("print (reverse [42])") == "[42]");
}

TEST_CASE("IRGenerator.FSharp.list_reverse_empty")
{
    CHECK(executeSourceAndGetOutput("print (reverse [])") == "[]");
}

TEST_CASE("IRGenerator.FSharp.list_reverse_pipeline")
{
    CHECK(executeSourceAndGetOutput("print ([1; 2; 3] |> reverse)") == "[3; 2; 1]");
}

TEST_CASE("IRGenerator.FSharp.list_reverse_double")
{
    CHECK(executeSourceAndGetOutput("print (reverse (reverse [1; 2; 3]))") == "[1; 2; 3]");
}

TEST_CASE("IRGenerator.FSharp.list_reverse_preserves_length")
{
    CHECK(executeSourceAndGetOutput("print (length (reverse [1; 2; 3; 4; 5]))") == "5");
}

// =============================================================================
// List Utility Functions: find, exists, forall, take, drop, zip, flatten
// =============================================================================

// --- find ---

TEST_CASE("IRGenerator.FSharp.list_find_match")
{
    CHECK(executeSourceAndGetOutput(
              "match find (fun x -> x > 3) [1;2;3;4;5] with | Some v -> print v | None -> print \"none\"")
          == "4");
}

TEST_CASE("IRGenerator.FSharp.list_find_no_match")
{
    CHECK(executeSourceAndGetOutput(
              "match find (fun x -> x > 10) [1;2;3] with | Some v -> print v | None -> print \"none\"")
          == "none");
}

TEST_CASE("IRGenerator.FSharp.list_find_empty")
{
    CHECK(executeSourceAndGetOutput(
              "match find (fun x -> x > 0) [] with | Some v -> print v | None -> print \"none\"")
          == "none");
}

TEST_CASE("IRGenerator.FSharp.list_find_first_match")
{
    CHECK(executeSourceAndGetOutput(
              "match find (fun x -> x % 2 == 0) [1;2;4;6] with | Some v -> print v | None -> print \"none\"")
          == "2");
}

TEST_CASE("IRGenerator.FSharp.list_find_pipeline")
{
    CHECK(
        executeSourceAndGetOutput(
            "match ([1;2;3;4;5] |> find (fun x -> x > 3)) with | Some v -> print v | None -> print \"none\"")
        == "4");
}

TEST_CASE("IRGenerator.FSharp.list_find_partial")
{
    CHECK(executeSourceAndGetOutput(
              "let findBig = find (fun x -> x > 3)\n"
              "match findBig [1;2;3;4;5] with | Some v -> print v | None -> print \"none\"")
          == "4");
}

// --- exists ---

TEST_CASE("IRGenerator.FSharp.list_exists_true")
{
    CHECK(executeSourceAndGetOutput("print (exists (fun x -> x > 3) [1;2;3;4;5])") == "true");
}

TEST_CASE("IRGenerator.FSharp.list_exists_false")
{
    CHECK(executeSourceAndGetOutput("print (exists (fun x -> x > 10) [1;2;3])") == "false");
}

TEST_CASE("IRGenerator.FSharp.list_exists_empty")
{
    CHECK(executeSourceAndGetOutput("print (exists (fun x -> x > 0) [])") == "false");
}

TEST_CASE("IRGenerator.FSharp.list_exists_pipeline")
{
    CHECK(executeSourceAndGetOutput("print ([1;2;3;4;5] |> exists (fun x -> x > 3))") == "true");
}

TEST_CASE("IRGenerator.FSharp.list_exists_partial")
{
    CHECK(executeSourceAndGetOutput("let hasBig = exists (fun x -> x > 10)\nprint (hasBig [1;2;3])")
          == "false");
}

// --- forall ---

TEST_CASE("IRGenerator.FSharp.list_forall_true")
{
    CHECK(executeSourceAndGetOutput("print (forall (fun x -> x > 0) [1;2;3])") == "true");
}

TEST_CASE("IRGenerator.FSharp.list_forall_false")
{
    CHECK(executeSourceAndGetOutput("print (forall (fun x -> x > 2) [1;2;3])") == "false");
}

TEST_CASE("IRGenerator.FSharp.list_forall_empty")
{
    CHECK(executeSourceAndGetOutput("print (forall (fun x -> x > 0) [])") == "true");
}

TEST_CASE("IRGenerator.FSharp.list_forall_pipeline")
{
    CHECK(executeSourceAndGetOutput("print ([1;2;3] |> forall (fun x -> x > 0))") == "true");
}

TEST_CASE("IRGenerator.FSharp.list_forall_partial")
{
    CHECK(executeSourceAndGetOutput("let allPositive = forall (fun x -> x > 0)\nprint (allPositive [1;2;3])")
          == "true");
}

// --- each ---

TEST_CASE("IRGenerator.FSharp.list_each_basic")
{
    CHECK(executeSourceAndGetOutput("each (fun x -> print x) [1; 2; 3]") == "123");
}

TEST_CASE("IRGenerator.FSharp.list_each_pipeline")
{
    CHECK(executeSourceAndGetOutput("let _ = [1; 2; 3] |> each (fun x -> print x)") == "123");
}

TEST_CASE("IRGenerator.FSharp.list_each_print_ref")
{
    CHECK(executeSourceAndGetOutput("let _ = [1; 2; 3] |> each print") == "123");
}

TEST_CASE("IRGenerator.FSharp.list_each_println")
{
    CHECK(executeSourceAndGetOutput("let _ = [1; 2; 3] |> each println") == "1\n2\n3\n");
}

TEST_CASE("IRGenerator.FSharp.list_each_empty")
{
    CHECK(executeSourceAndGetOutput("each (fun x -> print x) []") == "");
}

TEST_CASE("IRGenerator.FSharp.list_each_single")
{
    CHECK(executeSourceAndGetOutput("let _ = [42] |> each print") == "42");
}

TEST_CASE("IRGenerator.FSharp.list_each_returns_unit")
{
    CHECK(executeSourceAndGetOutput("let r = each (fun x -> print x) [1; 2]\nprint r") == "120");
}

TEST_CASE("IRGenerator.FSharp.list_each_chained_pipeline")
{
    CHECK(executeSourceAndGetOutput("let _ = [1;2;3;4] |> filter (fun x -> x % 2 == 0) |> each print")
          == "24");
}

TEST_CASE("IRGenerator.FSharp.list_each_with_lambda_body")
{
    CHECK(executeSourceAndGetOutput("let _ = [10; 20; 30] |> each (fun x -> println (x * 2))")
          == "20\n40\n60\n");
}

// --- take ---

TEST_CASE("IRGenerator.FSharp.list_take_basic")
{
    CHECK(executeSourceAndGetOutput("print (take 3 [1;2;3;4;5])") == "[1; 2; 3]");
}

TEST_CASE("IRGenerator.FSharp.list_take_zero")
{
    CHECK(executeSourceAndGetOutput("print (take 0 [1;2;3])") == "[]");
}

TEST_CASE("IRGenerator.FSharp.list_take_all")
{
    CHECK(executeSourceAndGetOutput("print (take 5 [1;2;3])") == "[1; 2; 3]");
}

TEST_CASE("IRGenerator.FSharp.list_take_more")
{
    CHECK(executeSourceAndGetOutput("print (take 10 [1;2;3])") == "[1; 2; 3]");
}

TEST_CASE("IRGenerator.FSharp.list_take_empty")
{
    CHECK(executeSourceAndGetOutput("print (take 3 [])") == "[]");
}

TEST_CASE("IRGenerator.FSharp.list_take_pipeline")
{
    CHECK(executeSourceAndGetOutput("print ([1;2;3;4;5] |> take 3)") == "[1; 2; 3]");
}

TEST_CASE("IRGenerator.FSharp.list_take_partial")
{
    CHECK(executeSourceAndGetOutput("let first3 = take 3\nprint (first3 [1;2;3;4;5])") == "[1; 2; 3]");
}

// --- drop ---

TEST_CASE("IRGenerator.FSharp.list_drop_basic")
{
    CHECK(executeSourceAndGetOutput("print (drop 3 [1;2;3;4;5])") == "[4; 5]");
}

TEST_CASE("IRGenerator.FSharp.list_drop_zero")
{
    CHECK(executeSourceAndGetOutput("print (drop 0 [1;2;3])") == "[1; 2; 3]");
}

TEST_CASE("IRGenerator.FSharp.list_drop_all")
{
    CHECK(executeSourceAndGetOutput("print (drop 3 [1;2;3])") == "[]");
}

TEST_CASE("IRGenerator.FSharp.list_drop_more")
{
    CHECK(executeSourceAndGetOutput("print (drop 10 [1;2;3])") == "[]");
}

TEST_CASE("IRGenerator.FSharp.list_drop_empty")
{
    CHECK(executeSourceAndGetOutput("print (drop 3 [])") == "[]");
}

TEST_CASE("IRGenerator.FSharp.list_drop_pipeline")
{
    CHECK(executeSourceAndGetOutput("print ([1;2;3;4;5] |> drop 2)") == "[3; 4; 5]");
}

TEST_CASE("IRGenerator.FSharp.list_drop_partial")
{
    CHECK(executeSourceAndGetOutput("let skipFirst2 = drop 2\nprint (skipFirst2 [1;2;3;4;5])")
          == "[3; 4; 5]");
}

// --- zip ---

TEST_CASE("IRGenerator.FSharp.list_zip_basic")
{
    CHECK(executeSourceAndGetOutput("print (zip [1;2;3] [4;5;6])") == "[(1, 4); (2, 5); (3, 6)]");
}

TEST_CASE("IRGenerator.FSharp.list_zip_unequal_left")
{
    CHECK(executeSourceAndGetOutput("print (zip [1;2] [4;5;6])") == "[(1, 4); (2, 5)]");
}

TEST_CASE("IRGenerator.FSharp.list_zip_unequal_right")
{
    CHECK(executeSourceAndGetOutput("print (zip [1;2;3] [4;5])") == "[(1, 4); (2, 5)]");
}

TEST_CASE("IRGenerator.FSharp.list_zip_empty_left")
{
    CHECK(executeSourceAndGetOutput("print (zip [] [1;2;3])") == "[]");
}

TEST_CASE("IRGenerator.FSharp.list_zip_empty_right")
{
    CHECK(executeSourceAndGetOutput("print (zip [1;2;3] [])") == "[]");
}

TEST_CASE("IRGenerator.FSharp.list_zip_single")
{
    CHECK(executeSourceAndGetOutput("print (zip [1] [2])") == "[(1, 2)]");
}

TEST_CASE("IRGenerator.FSharp.list_zip_pipeline")
{
    // Pipeline pipes into last param (__ys), so [4;5;6] |> zip [1;2;3] means zip [1;2;3] [4;5;6]
    CHECK(executeSourceAndGetOutput("print ([4;5;6] |> zip [1;2;3])") == "[(1, 4); (2, 5); (3, 6)]");
}

// --- flatten ---

TEST_CASE("IRGenerator.FSharp.list_flatten_basic")
{
    CHECK(executeSourceAndGetOutput("print (flatten [[1;2]; [3;4]; [5;6]])") == "[1; 2; 3; 4; 5; 6]");
}

TEST_CASE("IRGenerator.FSharp.list_flatten_empty_outer")
{
    CHECK(executeSourceAndGetOutput("print (flatten [])") == "[]");
}

TEST_CASE("IRGenerator.FSharp.list_flatten_empty_inner")
{
    CHECK(executeSourceAndGetOutput("print (flatten [[]; [1;2]; []])") == "[1; 2]");
}

TEST_CASE("IRGenerator.FSharp.list_flatten_single")
{
    CHECK(executeSourceAndGetOutput("print (flatten [[1;2;3]])") == "[1; 2; 3]");
}

TEST_CASE("IRGenerator.FSharp.list_flatten_pipeline")
{
    CHECK(executeSourceAndGetOutput("print ([[1;2]; [3;4]] |> flatten)") == "[1; 2; 3; 4]");
}

// --- Composition tests ---

TEST_CASE("IRGenerator.FSharp.list_util_take_then_map")
{
    CHECK(executeSourceAndGetOutput("print ([1;2;3;4;5] |> take 3 |> map (fun x -> x * 10))")
          == "[10; 20; 30]");
}

TEST_CASE("IRGenerator.FSharp.list_util_drop_then_fold")
{
    CHECK(executeSourceAndGetOutput("print ([1;2;3;4;5] |> drop 2 |> fold 0 (fun a x -> a + x))") == "12");
}

TEST_CASE("IRGenerator.FSharp.list_util_filter_then_find")
{
    CHECK(executeSourceAndGetOutput(
              "match ([1;2;3;4;5] |> filter (fun x -> x > 2) |> find (fun x -> x % 2 == 0)) with"
              " | Some v -> print v | None -> print \"none\"")
          == "4");
}

TEST_CASE("IRGenerator.FSharp.list_util_map_flatten")
{
    CHECK(executeSourceAndGetOutput("print (flatten (map (fun x -> [x; x * 10]) [1;2;3]))")
          == "[1; 10; 2; 20; 3; 30]");
}

// =============================================================================
// Higher-Order List Functions: Composition / Integration
// =============================================================================

TEST_CASE("IRGenerator.FSharp.list_hof_filter_then_map")
{
    CHECK(executeSourceAndGetOutput(
              "print ([1; 2; 3; 4; 5; 6] |> filter (fun x -> x % 2 == 0) |> map (fun x -> x * 10))")
          == "[20; 40; 60]");
}

TEST_CASE("IRGenerator.FSharp.list_hof_map_then_fold")
{
    CHECK(executeSourceAndGetOutput(
              "print ([1; 2; 3] |> map (fun x -> x * x) |> fold 0 (fun acc x -> acc + x))")
          == "14");
}

TEST_CASE("IRGenerator.FSharp.list_hof_filter_map_fold")
{
    CHECK(executeSourceAndGetOutput(
              "print ([1; 2; 3; 4; 5; 6] |> filter (fun x -> x % 2 == 0) |> map (fun x -> x * "
              "x) |> fold 0 (fun acc x -> acc + x))")
          == "56");
}

TEST_CASE("IRGenerator.FSharp.list_hof_map_reverse")
{
    CHECK(executeSourceAndGetOutput("print ([1; 2; 3] |> map (fun x -> x * 2) |> reverse)") == "[6; 4; 2]");
}

TEST_CASE("IRGenerator.FSharp.list_hof_with_let_binding")
{
    CHECK(executeSourceAndGetOutput("let xs = [1; 2; 3; 4; 5]\n"
                                    "let evens = filter (fun x -> x % 2 == 0) xs\n"
                                    "let doubled = map (fun x -> x * 2) evens\n"
                                    "print doubled")
          == "[4; 8]");
}

TEST_CASE("IRGenerator.FSharp.list_hof_nested_map")
{
    CHECK(executeSourceAndGetOutput(
              "let double x = x * 2\nlet add1 x = x + 1\nprint (map add1 (map double [1; 2; 3]))")
          == "[3; 5; 7]");
}

TEST_CASE("IRGenerator.FSharp.list_hof_fold_with_if")
{
    CHECK(executeSourceAndGetOutput(
              "print (fold 0 (fun acc x -> if x > 3 then acc + x else acc) [1; 2; 3; 4; 5])")
          == "9");
}

TEST_CASE("IRGenerator.FSharp.list_hof_filter_length")
{
    CHECK(executeSourceAndGetOutput("print (length (filter (fun x -> x > 3) [1; 2; 3; 4; 5]))") == "2");
}

TEST_CASE("IRGenerator.FSharp.list_hof_map_with_range")
{
    CHECK(executeSourceAndGetOutput("print (map (fun x -> x * x) [1..5])") == "[1; 4; 9; 16; 25]");
}

// =============================================================================
// Placeholder Lambda Sugar Tests (`_`)
// =============================================================================

TEST_CASE("IRGenerator.FSharp.placeholder_increment")
{
    // (_ + 1) desugars to fun __x -> __x + 1
    CHECK(executeSourceAndGetOutput("let inc = (_ + 1); print (inc 5)") == "6");
}

TEST_CASE("IRGenerator.FSharp.placeholder_multiply")
{
    // (_ * 2) desugars to fun __x -> __x * 2
    CHECK(executeSourceAndGetOutput("let double = (_ * 2); print (double 7)") == "14");
}

TEST_CASE("IRGenerator.FSharp.placeholder_same_param_twice")
{
    // (_ + _) desugars to fun __x -> __x + __x — same parameter used twice
    CHECK(executeSourceAndGetOutput("let f = (_ + _); print (f 7)") == "14");
}

TEST_CASE("IRGenerator.FSharp.placeholder_complex_expr")
{
    // (_ * _ + _) desugars to fun __x -> __x * __x + __x
    CHECK(executeSourceAndGetOutput("let f = (_ * _ + _); print (f 3)") == "12");
}

TEST_CASE("IRGenerator.FSharp.placeholder_pipeline")
{
    // Pipeline with placeholder lambda
    CHECK(executeSourceAndGetOutput("print (5 |> (_ + 1))") == "6");
}

TEST_CASE("IRGenerator.FSharp.placeholder_composition")
{
    // Two independent placeholder lambdas
    CHECK(executeSourceAndGetOutput("let f = (_ * 2); let g = (_ + 1); print (g (f 3))") == "7");
}

TEST_CASE("IRGenerator.FSharp.placeholder_pipeline_chained")
{
    // Chained pipelines with placeholder lambdas
    CHECK(executeSourceAndGetOutput("print (10 |> (_ * 2) |> (_ + 1))") == "21");
}

TEST_CASE("IRGenerator.FSharp.placeholder_subtraction")
{
    // (_ - 3)
    CHECK(executeSourceAndGetOutput("let f = (_ - 3); print (f 10)") == "7");
}

TEST_CASE("IRGenerator.FSharp.placeholder_unparenthesized_multiply")
{
    // _ * 2 (no parens) desugars to fun __x -> __x * 2
    CHECK(executeSourceAndGetOutput("let double = _ * 2; print (double 5)") == "10");
}

TEST_CASE("IRGenerator.FSharp.placeholder_unparenthesized_add")
{
    // _ + 1 (no parens) desugars to fun __x -> __x + 1
    CHECK(executeSourceAndGetOutput("let incr = _ + 1; print (incr 41)") == "42");
}

TEST_CASE("IRGenerator.FSharp.placeholder_unparenthesized_comparison")
{
    // _ > 0 (no parens) desugars to fun __x -> __x > 0
    CHECK(executeSourceAndGetOutput("let isPositive = _ > 0; print (isPositive 5)") == "true");
}

TEST_CASE("IRGenerator.FSharp.placeholder_unparenthesized_filter")
{
    // filter (_ > 2) with list — tests unparenthesized placeholder in pipeline context
    CHECK(executeSourceAndGetOutput("let pred = _ > 2; print (filter pred [1; 2; 3; 4; 5])") == "[3; 4; 5]");
}

// ============================================================================
// Placeholder Lambda Sugar with Structured Records (ps)
// ============================================================================

TEST_CASE("IRGenerator.FSharp.placeholder_ps_map_pid")
{
    // _.pid desugars to fun __x -> __x.pid
    CHECK(executeSourceAndGetOutput("let pids = ps |> map _.pid\nprint pids") == "[1; 42; 100]");
}

TEST_CASE("IRGenerator.FSharp.placeholder_ps_map_command")
{
    CHECK(executeSourceAndGetOutput("let cmds = ps |> map _.command\nprint cmds")
          == "[/sbin/init; firefox; vim]");
}

TEST_CASE("IRGenerator.FSharp.placeholder_ps_map_user")
{
    CHECK(executeSourceAndGetOutput("let users = ps |> map _.user\nprint users") == "[root; alice; bob]");
}

TEST_CASE("IRGenerator.FSharp.placeholder_ps_filter_map")
{
    // (_.pid > 10) desugars to fun __x -> __x.pid > 10
    CHECK(executeSourceAndGetOutput("let pids = ps |> filter (_.pid > 10) |> map _.pid\nprint pids")
          == "[42; 100]");
}

TEST_CASE("IRGenerator.FSharp.placeholder_ps_filter_string_eq")
{
    CHECK(executeSourceAndGetOutput(
              "let cmds = ps |> filter (_.user == \"alice\") |> map _.command\nprint cmds")
          == "[firefox]");
}

TEST_CASE("IRGenerator.FSharp.placeholder_ps_filter_map_command")
{
    CHECK(executeSourceAndGetOutput("let cmds = ps |> filter (_.pid > 10) |> map _.command\nprint cmds")
          == "[firefox; vim]");
}

TEST_CASE("IRGenerator.FSharp.placeholder_ps_sortBy")
{
    CHECK(executeSourceAndGetOutput("let pids = ps |> sortBy _.pid |> map _.pid\nprint pids")
          == "[1; 42; 100]");
}

TEST_CASE("IRGenerator.FSharp.placeholder_ps_exists")
{
    CHECK(executeSourceAndGetOutput("let r = ps |> exists (_.pid > 50)\nprint r") == "true");
}

// ============================================================================
// Option Default Operator (?|)
// ============================================================================

TEST_CASE("IRGenerator.FSharp.option_default_some")
{
    // Some value ?| default → unwraps to the Some value
    CHECK(executeSourceAndGetOutput("print (Some 42 ?| 0)") == "42");
}

TEST_CASE("IRGenerator.FSharp.option_default_none")
{
    // None ?| default → evaluates to the default
    CHECK(executeSourceAndGetOutput("print (None ?| 99)") == "99");
}

TEST_CASE("IRGenerator.FSharp.option_default_string_some")
{
    // String Some unwraps correctly
    CHECK(executeSourceAndGetOutput("print (Some \"hello\" ?| \"world\")") == "hello");
}

TEST_CASE("IRGenerator.FSharp.option_default_string_none")
{
    // Typed None with string default
    CHECK(executeSourceAndGetOutput("let x: option<str> = None; print (x ?| \"default\")") == "default");
}

TEST_CASE("IRGenerator.FSharp.option_default_nested")
{
    // Nested ?| — inner None falls back, then result used as default for outer
    CHECK(executeSourceAndGetOutput("print (None ?| (Some 5 ?| 0))") == "5");
}

TEST_CASE("IRGenerator.FSharp.option_default_with_expr")
{
    // Default is an expression, not just a literal
    CHECK(executeSourceAndGetOutput("print (Some 10 ?| (3 + 4))") == "10");
}

TEST_CASE("IRGenerator.FSharp.option_default_in_let")
{
    // ?| in a let binding
    CHECK(executeSourceAndGetOutput("let x = Some 7 ?| 0; print x") == "7");
}

TEST_CASE("IRGenerator.FSharp.option_default_none_in_let")
{
    // ?| with None in a let binding
    CHECK(executeSourceAndGetOutput("let x = None ?| 42; print x") == "42");
}

TEST_CASE("IRGenerator.FSharp.option_default_find")
{
    // find returns Option — ?| provides fallback
    CHECK(executeSourceAndGetOutput("print (find (fun x -> x > 10) [1;2;3] ?| -1)") == "-1");
}

TEST_CASE("IRGenerator.FSharp.option_default_find_found")
{
    // find returns Some when element found — ?| unwraps
    CHECK(executeSourceAndGetOutput("print (find (fun x -> x > 1) [1;2;3] ?| -1)") == "2");
}

// ============================================================================
// Option Combinators: Option.map, Option.bind, Option.defaultValue
// ============================================================================

TEST_CASE("IRGenerator.FSharp.option_map_some")
{
    // Option.map applies function to Some value, wraps result in Some
    CHECK(executeSourceAndGetOutput("print (Option.map (fun x -> x * 2) (Some 21) ?| 0)") == "42");
}

TEST_CASE("IRGenerator.FSharp.option_map_none")
{
    // Option.map on None returns None
    CHECK(executeSourceAndGetOutput("print (Option.map (fun x -> x * 2) None ?| 0)") == "0");
}

TEST_CASE("IRGenerator.FSharp.option_map_named")
{
    // Option.map with a named function
    CHECK(executeSourceAndGetOutput("let double (x: int) = x * 2\nprint (Option.map double (Some 5) ?| 0)")
          == "10");
}

TEST_CASE("IRGenerator.FSharp.option_map_method")
{
    // Method-style: opt.map f
    CHECK(executeSourceAndGetOutput("let r = (Some 21).map (fun x -> x * 2)\nprint (r ?| 0)") == "42");
}

TEST_CASE("IRGenerator.FSharp.option_map_pipeline")
{
    // Pipeline: value |> Option.map f |> Option.defaultValue def
    CHECK(executeSourceAndGetOutput("print (Some 21 |> Option.map (fun x -> x * 2) |> Option.defaultValue 0)")
          == "42");
}

TEST_CASE("IRGenerator.FSharp.option_bind_some")
{
    // Option.bind applies function that returns Option
    CHECK(executeSourceAndGetOutput("let half (x: int) = if x % 2 == 0 then Some (x / 2) else None\n"
                                    "print (Option.bind half (Some 10) ?| 0)")
          == "5");
}

TEST_CASE("IRGenerator.FSharp.option_bind_none")
{
    // Option.bind on None returns None
    CHECK(executeSourceAndGetOutput("let half (x: int) = if x % 2 == 0 then Some (x / 2) else None\n"
                                    "print (Option.bind half None ?| 0)")
          == "0");
}

TEST_CASE("IRGenerator.FSharp.option_bind_returns_none")
{
    // Option.bind where f returns None
    CHECK(executeSourceAndGetOutput("let half (x: int) = if x % 2 == 0 then Some (x / 2) else None\n"
                                    "print (Option.bind half (Some 3) ?| 0)")
          == "0");
}

TEST_CASE("IRGenerator.FSharp.option_bind_method")
{
    // Method-style: opt.bind f
    CHECK(executeSourceAndGetOutput("let half (x: int) = if x % 2 == 0 then Some (x / 2) else None\n"
                                    "print ((Some 10).bind half ?| 0)")
          == "5");
}

TEST_CASE("IRGenerator.FSharp.option_defaultValue_some")
{
    // Option.defaultValue with Some returns inner value
    CHECK(executeSourceAndGetOutput("print (Option.defaultValue 0 (Some 42))") == "42");
}

TEST_CASE("IRGenerator.FSharp.option_defaultValue_none")
{
    // Option.defaultValue with None returns default
    CHECK(executeSourceAndGetOutput("print (Option.defaultValue 0 None)") == "0");
}

TEST_CASE("IRGenerator.FSharp.option_defaultValue_method")
{
    // Method-style: opt.defaultValue def
    CHECK(executeSourceAndGetOutput("print ((Some 42).defaultValue 0)") == "42");
}

TEST_CASE("IRGenerator.FSharp.option_defaultValue_pipeline")
{
    // Pipeline: value |> Option.defaultValue def
    CHECK(executeSourceAndGetOutput("print (Some 42 |> Option.defaultValue 0)") == "42");
    CHECK(executeSourceAndGetOutput("print (None |> Option.defaultValue 99)") == "99");
}

TEST_CASE("IRGenerator.FSharp.option_map_string")
{
    // Option.map with strings
    CHECK(executeSourceAndGetOutput(R"(print (Option.map (fun s -> s + "!") (Some "hi") ?| ""))") == "hi!");
}

TEST_CASE("IRGenerator.FSharp.option_chained_pipeline")
{
    // Chaining multiple Option.map calls in pipeline with Option.defaultValue
    CHECK(executeSourceAndGetOutput(
              "print (Some 5 |> Option.map (fun x -> x * 2) |> Option.map (fun x -> x + 1) |> "
              "Option.defaultValue 0)")
          == "11");
}

// ============================================================================
// For-in with pattern destructuring

TEST_CASE("IRGenerator.FSharp.for_in_tuple_destructure")
{
    CHECK(executeSourceAndGetOutput("for (x, y) in [(1, 2); (3, 4)] do\nprint x\nend") == "13");
}

TEST_CASE("IRGenerator.FSharp.for_in_tuple_both")
{
    CHECK(executeSourceAndGetOutput(
              "let mut r = 0\nfor (x, y) in [(1, 2); (3, 4)] do\nr <- r + x + y\nend\nprint r")
          == "10");
}

TEST_CASE("IRGenerator.FSharp.for_in_wildcard")
{
    CHECK(executeSourceAndGetOutput("for (x, _) in [(1, 2); (3, 4)] do\nprint x\nend") == "13");
}

TEST_CASE("IRGenerator.FSharp.for_in_empty_list")
{
    // Empty list — body never executes
    CHECK(executeSourceAndGetOutput("for (x, y) in [] do\nprint x\nend") == "");
}

TEST_CASE("IRGenerator.FSharp.for_in_accumulator")
{
    CHECK(executeSourceAndGetOutput(
              "let mut sum = 0\nfor (x, _) in [(1, 10); (2, 20); (3, 30)] do\nsum <- sum + x\nend\nprint sum")
          == "6");
}

TEST_CASE("IRGenerator.FSharp.for_in_three_elements")
{
    // Iterates through all 3 elements
    CHECK(executeSourceAndGetOutput(
              "let mut sum = 0\nfor (x, _) in [(10, 0); (20, 0); (30, 0)] do\nsum <- sum + x\nend\nprint sum")
          == "60");
}

TEST_CASE("IRGenerator.FSharp.for_in_second_element")
{
    // Access second tuple element
    CHECK(executeSourceAndGetOutput("for (_, y) in [(1, 2); (3, 4)] do\nprint y\nend") == "24");
}

TEST_CASE("IRGenerator.FSharp.for_in_single_element")
{
    CHECK(executeSourceAndGetOutput("for (x, y) in [(42, 7)] do\nprint x\nprint y\nend") == "427");
}

TEST_CASE("IRGenerator.FSharp.for_in_simple_variable")
{
    CHECK(executeSourceAndGetOutput("for (x, _) in [(10, 0); (20, 0); (30, 0)] do\nprint x\nend")
          == "102030");
}

// ============================================================================
// Optional chaining (?.)
// ============================================================================

TEST_CASE("IRGenerator.FSharp.optional_chain_some")
{
    CHECK(executeSourceAndGetOutput("type P = { name: str; age: int }\n"
                                    "let x = Some { name = \"Alice\"; age = 30 }\n"
                                    "print (x?.name ?| \"none\")")
          == "Alice");
}

TEST_CASE("IRGenerator.FSharp.optional_chain_none")
{
    auto output = executeSourceAndGetOutput("type P = { name: str; age: int }\n"
                                            "let x = None\n"
                                            "print (x?.name ?| \"none\")");
    CAPTURE(output);
    CHECK(output == "none");
}

TEST_CASE("IRGenerator.FSharp.optional_chain_int_field")
{
    CHECK(executeSourceAndGetOutput("type P = { age: int }\n"
                                    "let x = Some { age = 30 }\n"
                                    "print (x?.age ?| 0)")
          == "30");
}

TEST_CASE("IRGenerator.FSharp.optional_chain_none_int")
{
    CHECK(executeSourceAndGetOutput("type P = { age: int }\n"
                                    "let x = None\n"
                                    "print (x?.age ?| 0)")
          == "0");
}

TEST_CASE("IRGenerator.FSharp.optional_chain_with_let")
{
    CHECK(executeSourceAndGetOutput("type P = { age: int }\n"
                                    "let x = Some { age = 30 }\n"
                                    "let result = x?.age ?| 0\n"
                                    "print result")
          == "30");
}

// =============================================================================
// Higher-Order List Functions: sort
// =============================================================================

TEST_CASE("IRGenerator.FSharp.list_sort_basic")
{
    CHECK(executeSourceAndGetOutput("print (sort [3; 1; 2])") == "[1; 2; 3]");
}

TEST_CASE("IRGenerator.FSharp.list_sort_empty")
{
    CHECK(executeSourceAndGetOutput("print (sort [])") == "[]");
}

TEST_CASE("IRGenerator.FSharp.list_sort_single")
{
    CHECK(executeSourceAndGetOutput("print (sort [42])") == "[42]");
}

TEST_CASE("IRGenerator.FSharp.list_sort_already_sorted")
{
    CHECK(executeSourceAndGetOutput("print (sort [1; 2; 3; 4; 5])") == "[1; 2; 3; 4; 5]");
}

TEST_CASE("IRGenerator.FSharp.list_sort_reverse_sorted")
{
    CHECK(executeSourceAndGetOutput("print (sort [5; 4; 3; 2; 1])") == "[1; 2; 3; 4; 5]");
}

TEST_CASE("IRGenerator.FSharp.list_sort_duplicates")
{
    CHECK(executeSourceAndGetOutput("print (sort [3; 1; 2; 1; 3])") == "[1; 1; 2; 3; 3]");
}

TEST_CASE("IRGenerator.FSharp.list_sort_negative")
{
    CHECK(executeSourceAndGetOutput("print (sort [3; -1; 0; -5; 2])") == "[-5; -1; 0; 2; 3]");
}

TEST_CASE("IRGenerator.FSharp.list_sort_pipeline")
{
    CHECK(executeSourceAndGetOutput("print ([3; 1; 2] |> sort)") == "[1; 2; 3]");
}

// =============================================================================
// Higher-Order List Functions: distinct
// =============================================================================

TEST_CASE("IRGenerator.FSharp.list_distinct_basic")
{
    CHECK(executeSourceAndGetOutput("print (distinct [1; 2; 3; 2; 1])") == "[1; 2; 3]");
}

TEST_CASE("IRGenerator.FSharp.list_distinct_no_dupes")
{
    CHECK(executeSourceAndGetOutput("print (distinct [1; 2; 3])") == "[1; 2; 3]");
}

TEST_CASE("IRGenerator.FSharp.list_distinct_all_same")
{
    CHECK(executeSourceAndGetOutput("print (distinct [5; 5; 5; 5])") == "[5]");
}

TEST_CASE("IRGenerator.FSharp.list_distinct_empty")
{
    CHECK(executeSourceAndGetOutput("print (distinct [])") == "[]");
}

TEST_CASE("IRGenerator.FSharp.list_distinct_single")
{
    CHECK(executeSourceAndGetOutput("print (distinct [42])") == "[42]");
}

TEST_CASE("IRGenerator.FSharp.list_distinct_pipeline")
{
    CHECK(executeSourceAndGetOutput("print ([1; 2; 3; 2; 1] |> distinct)") == "[1; 2; 3]");
}

TEST_CASE("IRGenerator.FSharp.list_distinct_preserves_order")
{
    CHECK(executeSourceAndGetOutput("print (distinct [3; 1; 4; 1; 5; 9; 2; 6; 5; 3])")
          == "[3; 1; 4; 5; 9; 2; 6]");
}

// =============================================================================
// Higher-Order List Functions: sortBy
// =============================================================================

TEST_CASE("IRGenerator.FSharp.list_sortBy_identity")
{
    CHECK(executeSourceAndGetOutput("print (sortBy (fun x -> x) [3; 1; 2])") == "[1; 2; 3]");
}

TEST_CASE("IRGenerator.FSharp.list_sortBy_descending")
{
    CHECK(executeSourceAndGetOutput("print (sortBy (fun x -> 0 - x) [3; 1; 2])") == "[3; 2; 1]");
}

TEST_CASE("IRGenerator.FSharp.list_sortBy_empty")
{
    CHECK(executeSourceAndGetOutput("print (sortBy (fun x -> x) [])") == "[]");
}

TEST_CASE("IRGenerator.FSharp.list_sortBy_single")
{
    CHECK(executeSourceAndGetOutput("print (sortBy (fun x -> x) [42])") == "[42]");
}

TEST_CASE("IRGenerator.FSharp.list_sortBy_already_sorted")
{
    CHECK(executeSourceAndGetOutput("print (sortBy (fun x -> x) [1; 2; 3])") == "[1; 2; 3]");
}

TEST_CASE("IRGenerator.FSharp.list_sortBy_duplicates")
{
    CHECK(executeSourceAndGetOutput("print (sortBy (fun x -> x) [3; 1; 2; 1; 3])") == "[1; 1; 2; 3; 3]");
}

TEST_CASE("IRGenerator.FSharp.list_sortBy_pipeline")
{
    CHECK(executeSourceAndGetOutput("print ([3; 1; 2] |> sortBy (fun x -> x))") == "[1; 2; 3]");
}

TEST_CASE("IRGenerator.FSharp.list_sortBy_named_function")
{
    CHECK(executeSourceAndGetOutput("let negate x = 0 - x\nprint (sortBy negate [3; 1; 2])") == "[3; 2; 1]");
}

TEST_CASE("IRGenerator.FSharp.list_sortBy_stable")
{
    // stable_sort preserves relative order for equal keys
    // x % 3: [5→2, 3→0, 1→1, 4→1, 2→2, 6→0] → group 0:[3,6], group 1:[1,4], group 2:[5,2]
    CHECK(executeSourceAndGetOutput("print (sortBy (fun x -> x % 3) [5; 3; 1; 4; 2; 6])")
          == "[3; 6; 1; 4; 5; 2]");
}

TEST_CASE("IRGenerator.FSharp.list_sortBy_partial")
{
    CHECK(executeSourceAndGetOutput("let sortAsc = sortBy (fun x -> x)\nprint (sortAsc [3; 1; 2])")
          == "[1; 2; 3]");
}

// =============================================================================
// Higher-Order List Functions: groupBy
// =============================================================================

TEST_CASE("IRGenerator.FSharp.list_groupBy_basic")
{
    CHECK(executeSourceAndGetOutput("print (groupBy (fun x -> x % 2) [1; 2; 3; 4; 5])")
          == "[(1, [1; 3; 5]); (0, [2; 4])]");
}

TEST_CASE("IRGenerator.FSharp.list_groupBy_identity")
{
    CHECK(executeSourceAndGetOutput("print (groupBy (fun x -> x) [1; 2; 3])")
          == "[(1, [1]); (2, [2]); (3, [3])]");
}

TEST_CASE("IRGenerator.FSharp.list_groupBy_empty")
{
    CHECK(executeSourceAndGetOutput("print (groupBy (fun x -> x) [])") == "[]");
}

TEST_CASE("IRGenerator.FSharp.list_groupBy_single")
{
    CHECK(executeSourceAndGetOutput("print (groupBy (fun x -> x) [42])") == "[(42, [42])]");
}

TEST_CASE("IRGenerator.FSharp.list_groupBy_all_same_key")
{
    CHECK(executeSourceAndGetOutput("print (groupBy (fun x -> 0) [1; 2; 3])") == "[(0, [1; 2; 3])]");
}

TEST_CASE("IRGenerator.FSharp.list_groupBy_pipeline")
{
    CHECK(executeSourceAndGetOutput("print ([1; 2; 3; 4] |> groupBy (fun x -> x % 2))")
          == "[(1, [1; 3]); (0, [2; 4])]");
}

TEST_CASE("IRGenerator.FSharp.list_groupBy_named_function")
{
    CHECK(executeSourceAndGetOutput("let parity x = x % 2\nprint (groupBy parity [1; 2; 3; 4])")
          == "[(1, [1; 3]); (0, [2; 4])]");
}

TEST_CASE("IRGenerator.FSharp.list_groupBy_partial")
{
    CHECK(executeSourceAndGetOutput("let byParity = groupBy (fun x -> x % 2)\nprint (byParity [1; 2; 3])")
          == "[(1, [1; 3]); (0, [2])]");
}

// =============================================================================
// Composition tests: sort, distinct, sortBy, groupBy
// =============================================================================

TEST_CASE("IRGenerator.FSharp.list_sort_then_distinct")
{
    CHECK(executeSourceAndGetOutput("print ([3; 1; 2; 1; 3] |> sort |> distinct)") == "[1; 2; 3]");
}

TEST_CASE("IRGenerator.FSharp.list_filter_then_sort")
{
    CHECK(executeSourceAndGetOutput("print ([5; 3; 8; 1; 4] |> filter (fun x -> x > 2) |> sort)")
          == "[3; 4; 5; 8]");
}

// =============================================================================
// Structured commands: ps (built-in process listing)
// =============================================================================

TEST_CASE("IRGenerator.FSharp.ps.basic_call")
{
    // ps returns a list — check length
    CHECK(executeSourceAndGetOutput("let procs = ps\nprint (length procs)") == "3");
}

TEST_CASE("IRGenerator.FSharp.ps.field_access_pid")
{
    // Extract pid from first element via head + match
    CHECK(executeSourceAndGetOutput("match head ps with\n"
                                    "| Some p -> print p.pid\n"
                                    "| None -> print \"empty\"")
          == "1");
}

TEST_CASE("IRGenerator.FSharp.ps.field_access_command")
{
    // Extract command from first element
    CHECK(executeSourceAndGetOutput("match head ps with\n"
                                    "| Some p -> print p.command\n"
                                    "| None -> print \"empty\"")
          == "/sbin/init");
}

TEST_CASE("IRGenerator.FSharp.ps.filter_by_pid")
{
    // Filter processes with pid > 10 — should get 2 (pid=42, pid=100)
    CHECK(executeSourceAndGetOutput("let filtered = ps |> filter (fun p -> p.pid > 10)\n"
                                    "print (length filtered)")
          == "2");
}

TEST_CASE("IRGenerator.FSharp.ps.map_pid")
{
    // Map to extract pids from all processes
    CHECK(executeSourceAndGetOutput("let pids = ps |> map (fun p -> p.pid)\n"
                                    "print pids")
          == "[1; 42; 100]");
}

TEST_CASE("IRGenerator.FSharp.ps.map_command")
{
    // Map to extract commands
    CHECK(executeSourceAndGetOutput("let cmds = ps |> map (fun p -> p.command)\n"
                                    "print cmds")
          == "[/sbin/init; firefox; vim]");
}

TEST_CASE("IRGenerator.FSharp.ps.filter_and_map")
{
    // Pipeline composition: filter by pid then map to command
    CHECK(executeSourceAndGetOutput(
              "let cmds = ps |> filter (fun p -> p.pid > 10) |> map (fun p -> p.command)\n"
              "print cmds")
          == "[firefox; vim]");
}

TEST_CASE("IRGenerator.FSharp.ps.field_access_user")
{
    // Access user field from second element
    CHECK(executeSourceAndGetOutput("match head (tail ps) with\n"
                                    "| Some p -> print p.user\n"
                                    "| None -> print \"empty\"")
          == "alice");
}

TEST_CASE("IRGenerator.FSharp.ps.field_access_mem")
{
    // Map to extract memory values
    CHECK(executeSourceAndGetOutput("let mems = ps |> map (fun p -> p.mem)\n"
                                    "print mems")
          == "[1024; 4096; 2048]");
}

TEST_CASE("IRGenerator.FSharp.ps.exists_high_cpu")
{
    // Check if any process has cpu > 10
    CHECK(executeSourceAndGetOutput("let r = ps |> exists (fun p -> p.pid > 50)\n"
                                    "print r")
          == "true");
}

TEST_CASE("IRGenerator.FSharp.ps.pipeline_with_let")
{
    // Store ps result, then pipeline
    CHECK(executeSourceAndGetOutput(
              "let procs = ps\n"
              "let highPid = procs |> filter (fun p -> p.pid > 1) |> map (fun p -> p.pid)\n"
              "print highPid")
          == "[42; 100]");
}

TEST_CASE("IRGenerator.FSharp.ps.fold_mem_sum")
{
    // fold to sum all memory values: 1024 + 4096 + 2048 = 7168
    CHECK(executeSourceAndGetOutput("print (ps |> fold 0 (fun acc p -> acc + p.mem))") == "7168");
}

TEST_CASE("IRGenerator.FSharp.ps.fold_pid_sum")
{
    // fold to sum all pids: 1 + 42 + 100 = 143
    CHECK(executeSourceAndGetOutput("print (ps |> fold 0 (fun acc p -> acc + p.pid))") == "143");
}

TEST_CASE("IRGenerator.FSharp.ps.reduce_pid_sum")
{
    // reduce via mapped pids: 1 + 42 + 100 = 143
    CHECK(executeSourceAndGetOutput("match ps |> map (fun p -> p.pid) |> reduce (fun a b -> a + b) with\n"
                                    "| Some v -> print v\n"
                                    "| None -> print \"none\"")
          == "143");
}

TEST_CASE("IRGenerator.FSharp.ps.find_by_pid")
{
    // find the process with pid 42, print its command
    CHECK(executeSourceAndGetOutput("match ps |> find (fun p -> p.pid == 42) with\n"
                                    "| Some p -> print p.command\n"
                                    "| None -> print \"none\"")
          == "firefox");
}

TEST_CASE("IRGenerator.FSharp.ps.find_none")
{
    // find a process with pid > 1000 — should be None
    CHECK(executeSourceAndGetOutput("match ps |> find (fun p -> p.pid > 1000) with\n"
                                    "| Some p -> print p.pid\n"
                                    "| None -> print \"none\"")
          == "none");
}

TEST_CASE("IRGenerator.FSharp.ps.forall_positive_pid")
{
    // all pids are > 0
    CHECK(executeSourceAndGetOutput("print (ps |> forall (fun p -> p.pid > 0))") == "true");
}

TEST_CASE("IRGenerator.FSharp.ps.forall_high_mem")
{
    // not all memory values are > 2000 (init has 1024)
    CHECK(executeSourceAndGetOutput("print (ps |> forall (fun p -> p.mem > 2000))") == "false");
}

TEST_CASE("IRGenerator.FSharp.ps.sortBy_mem")
{
    // sortBy memory ascending: 1024(pid=1), 2048(pid=100), 4096(pid=42)
    CHECK(executeSourceAndGetOutput("print (ps |> sortBy (fun p -> p.mem) |> map (fun p -> p.pid))")
          == "[1; 100; 42]");
}

TEST_CASE("IRGenerator.FSharp.ps.reverse_pids")
{
    // reverse the list and map to pids
    CHECK(executeSourceAndGetOutput("print (ps |> reverse |> map (fun p -> p.pid))") == "[100; 42; 1]");
}

TEST_CASE("IRGenerator.FSharp.ps.take_2")
{
    // take first 2 processes, map to pids
    CHECK(executeSourceAndGetOutput("print (ps |> take 2 |> map (fun p -> p.pid))") == "[1; 42]");
}

TEST_CASE("IRGenerator.FSharp.ps.drop_1")
{
    // drop first process, map to pids
    CHECK(executeSourceAndGetOutput("print (ps |> drop 1 |> map (fun p -> p.pid))") == "[42; 100]");
}

TEST_CASE("IRGenerator.FSharp.ps.filter_sortBy_map")
{
    // 3-stage pipeline: filter pid > 1, sortBy mem, map to command
    // pid=42(mem=4096,"firefox"), pid=100(mem=2048,"vim") → sorted: vim, firefox
    CHECK(executeSourceAndGetOutput("print (ps |> filter (fun p -> p.pid > 1) |> sortBy (fun p -> p.mem) |> "
                                    "map (fun p -> p.command))")
          == "[vim; firefox]");
}

TEST_CASE("IRGenerator.FSharp.ps.filter_reverse_map")
{
    // filter ppid == 1 (alice, bob), reverse, map to user
    CHECK(executeSourceAndGetOutput(
              "print (ps |> filter (fun p -> p.ppid == 1) |> reverse |> map (fun p -> p.user))")
          == "[bob; alice]");
}

TEST_CASE("IRGenerator.FSharp.ps.isEmpty_after_filter_true")
{
    // no process has pid > 1000
    CHECK(executeSourceAndGetOutput("print (ps |> filter (fun p -> p.pid > 1000) |> isEmpty)") == "true");
}

TEST_CASE("IRGenerator.FSharp.ps.isEmpty_after_filter_false")
{
    // all processes have pid > 0
    CHECK(executeSourceAndGetOutput("print (ps |> filter (fun p -> p.pid > 0) |> isEmpty)") == "false");
}

TEST_CASE("IRGenerator.FSharp.ps.length_after_filter")
{
    // 2 processes with mem > 1024 (alice=4096, bob=2048)
    CHECK(executeSourceAndGetOutput("print (ps |> filter (fun p -> p.mem > 1024) |> length)") == "2");
}

TEST_CASE("IRGenerator.FSharp.ps.map_users")
{
    // map to extract user names
    CHECK(executeSourceAndGetOutput("print (ps |> map (fun p -> p.user))") == "[root; alice; bob]");
}

TEST_CASE("IRGenerator.FSharp.ps.head_after_filter")
{
    // head of filtered list (pid > 10) — first match is firefox (pid=42)
    CHECK(executeSourceAndGetOutput("match ps |> filter (fun p -> p.pid > 10) |> head with\n"
                                    "| Some p -> print p.command\n"
                                    "| None -> print \"none\"")
          == "firefox");
}

// =============================================================================
// ls (FileInfo) Tests
// =============================================================================

TEST_CASE("IRGenerator.FSharp.ls.basic_call")
{
    // ls returns a list — check length (3 mock files)
    CHECK(executeSourceAndGetOutput("let files = ls\nprint (length files)") == "3");
}

TEST_CASE("IRGenerator.FSharp.ls.with_path_argument")
{
    // ls with explicit path argument
    CHECK(executeSourceAndGetOutput("let files = ls \"/tmp\"\nprint (length files)") == "3");
}

TEST_CASE("IRGenerator.FSharp.ls.field_access_name")
{
    // Extract name from first element via head + match
    CHECK(executeSourceAndGetOutput("match head ls with\n"
                                    "| Some f -> print f.name\n"
                                    "| None -> print \"empty\"")
          == "docs");
}

TEST_CASE("IRGenerator.FSharp.ls.field_access_size")
{
    // Map to extract sizes from all files
    CHECK(executeSourceAndGetOutput("let sizes = ls |> map (fun f -> f.size)\n"
                                    "print sizes")
          == "[4096; 42; 256]");
}

TEST_CASE("IRGenerator.FSharp.ls.field_access_isDir")
{
    // Extract isDir from first element (docs is a directory)
    CHECK(executeSourceAndGetOutput("match head ls with\n"
                                    "| Some f -> print f.isDir\n"
                                    "| None -> print \"empty\"")
          == "true");
}

TEST_CASE("IRGenerator.FSharp.ls.field_access_mode")
{
    // Map to extract mode values
    CHECK(executeSourceAndGetOutput("let modes = ls |> map (fun f -> f.mode)\n"
                                    "print modes")
          == "[493; 420; 493]");
}

TEST_CASE("IRGenerator.FSharp.ls.field_access_mtime")
{
    // Map to extract mtime values
    CHECK(executeSourceAndGetOutput("let mtimes = ls |> map (fun f -> f.mtime)\n"
                                    "print mtimes")
          == "[1700000000; 1700001000; 1700002000]");
}

TEST_CASE("IRGenerator.FSharp.ls.filter_dirs")
{
    // Filter to only directories — should get 1 (docs)
    CHECK(executeSourceAndGetOutput("let dirs = ls |> filter (fun f -> f.isDir == true)\n"
                                    "print (length dirs)")
          == "1");
}

TEST_CASE("IRGenerator.FSharp.ls.filter_files")
{
    // Filter to only files (not dirs) — should get 2
    CHECK(executeSourceAndGetOutput("let files = ls |> filter (fun f -> f.isDir == false)\n"
                                    "print (length files)")
          == "2");
}

TEST_CASE("IRGenerator.FSharp.ls.map_name")
{
    // Map to extract names from all files
    CHECK(executeSourceAndGetOutput("let names = ls |> map (fun f -> f.name)\n"
                                    "print names")
          == "[docs; hello.txt; script.sh]");
}

TEST_CASE("IRGenerator.FSharp.ls.filter_and_map")
{
    // Pipeline: filter by size > 100 then map to name
    CHECK(executeSourceAndGetOutput(
              "let names = ls |> filter (fun f -> f.size > 100) |> map (fun f -> f.name)\n"
              "print names")
          == "[docs; script.sh]");
}

TEST_CASE("IRGenerator.FSharp.ls.sortBy_size")
{
    // sortBy size ascending: 42(hello.txt), 256(script.sh), 4096(docs)
    CHECK(executeSourceAndGetOutput("print (ls |> sortBy (fun f -> f.size) |> map (fun f -> f.name))")
          == "[hello.txt; script.sh; docs]");
}

TEST_CASE("IRGenerator.FSharp.ls.fold_size_sum")
{
    // fold to sum all sizes: 4096 + 42 + 256 = 4394
    CHECK(executeSourceAndGetOutput("print (ls |> fold 0 (fun acc f -> acc + f.size))") == "4394");
}

TEST_CASE("IRGenerator.FSharp.ls.find_by_name")
{
    // find the file named "hello.txt", print its size
    CHECK(executeSourceAndGetOutput("match ls |> find (fun f -> f.name == \"hello.txt\") with\n"
                                    "| Some f -> print f.size\n"
                                    "| None -> print \"none\"")
          == "42");
}

TEST_CASE("IRGenerator.FSharp.ls.find_none")
{
    // find a file with size > 10000 — should be None
    CHECK(executeSourceAndGetOutput("match ls |> find (fun f -> f.size > 10000) with\n"
                                    "| Some f -> print f.name\n"
                                    "| None -> print \"none\"")
          == "none");
}

TEST_CASE("IRGenerator.FSharp.ls.exists_large_file")
{
    // check if any file has size > 1000
    CHECK(executeSourceAndGetOutput("print (ls |> exists (fun f -> f.size > 1000))") == "true");
}

TEST_CASE("IRGenerator.FSharp.ls.forall_positive_size")
{
    // all sizes are > 0
    CHECK(executeSourceAndGetOutput("print (ls |> forall (fun f -> f.size > 0))") == "true");
}

TEST_CASE("IRGenerator.FSharp.ls.forall_large_size")
{
    // not all sizes are > 100 (hello.txt has 42)
    CHECK(executeSourceAndGetOutput("print (ls |> forall (fun f -> f.size > 100))") == "false");
}

TEST_CASE("IRGenerator.FSharp.ls.take_2")
{
    // take first 2 files, map to names
    CHECK(executeSourceAndGetOutput("print (ls |> take 2 |> map (fun f -> f.name))") == "[docs; hello.txt]");
}

TEST_CASE("IRGenerator.FSharp.ls.drop_1")
{
    // drop first file, map to names
    CHECK(executeSourceAndGetOutput("print (ls |> drop 1 |> map (fun f -> f.name))")
          == "[hello.txt; script.sh]");
}

TEST_CASE("IRGenerator.FSharp.ls.reverse")
{
    // reverse the list and map to names
    CHECK(executeSourceAndGetOutput("print (ls |> reverse |> map (fun f -> f.name))")
          == "[script.sh; hello.txt; docs]");
}

TEST_CASE("IRGenerator.FSharp.ls.head_after_filter")
{
    // head of filtered list (size < 300) — first match is hello.txt (size=42)
    CHECK(executeSourceAndGetOutput("match ls |> filter (fun f -> f.size < 300) |> head with\n"
                                    "| Some f -> print f.name\n"
                                    "| None -> print \"none\"")
          == "hello.txt");
}

TEST_CASE("IRGenerator.FSharp.ls.isEmpty_after_filter_true")
{
    // no file has size > 10000
    CHECK(executeSourceAndGetOutput("print (ls |> filter (fun f -> f.size > 10000) |> isEmpty)") == "true");
}

TEST_CASE("IRGenerator.FSharp.ls.isEmpty_after_filter_false")
{
    // all files have size > 0
    CHECK(executeSourceAndGetOutput("print (ls |> filter (fun f -> f.size > 0) |> isEmpty)") == "false");
}

TEST_CASE("IRGenerator.FSharp.ls.length_after_filter")
{
    // 2 files with size <= 256 (hello.txt=42, script.sh=256)
    CHECK(executeSourceAndGetOutput("print (ls |> filter (fun f -> f.size <= 256) |> length)") == "2");
}

TEST_CASE("IRGenerator.FSharp.ls.pipeline_with_let")
{
    // Store ls result, then pipeline
    CHECK(executeSourceAndGetOutput(
              "let files = ls\n"
              "let bigFiles = files |> filter (fun f -> f.size > 100) |> map (fun f -> f.name)\n"
              "print bigFiles")
          == "[docs; script.sh]");
}

TEST_CASE("IRGenerator.FSharp.ls.filter_sortBy_map")
{
    // 3-stage pipeline: filter size > 0, sortBy size, map to name
    CHECK(
        executeSourceAndGetOutput(
            "print (ls |> filter (fun f -> f.size > 0) |> sortBy (fun f -> f.size) |> map (fun f -> f.name))")
        == "[hello.txt; script.sh; docs]");
}

// --- formatMode helper tests ---

TEST_CASE("IRGenerator.FSharp.formatMode.rwxr_xr_x")
{
    // 0755 = 493 decimal → "rwxr-xr-x"
    CHECK(executeSourceAndGetOutput("print (formatMode 493)") == "rwxr-xr-x");
}

TEST_CASE("IRGenerator.FSharp.formatMode.rw_r__r__")
{
    // 0644 = 420 decimal → "rw-r--r--"
    CHECK(executeSourceAndGetOutput("print (formatMode 420)") == "rw-r--r--");
}

TEST_CASE("IRGenerator.FSharp.formatMode.no_permissions")
{
    // 0000 → "---------"
    CHECK(executeSourceAndGetOutput("print (formatMode 0)") == "---------");
}

TEST_CASE("IRGenerator.FSharp.formatMode.all_permissions")
{
    // 0777 = 511 decimal → "rwxrwxrwx"
    CHECK(executeSourceAndGetOutput("print (formatMode 511)") == "rwxrwxrwx");
}

// --- formatDateTime helper tests ---

TEST_CASE("IRGenerator.FSharp.formatDateTime.epoch_1700000000")
{
    // 1700000000 = 2023-11-14 22:13:20 UTC
    CHECK(executeSourceAndGetOutput("print (formatDateTime 1700000000)") == "2023-11-14 22:13:20");
}

TEST_CASE("IRGenerator.FSharp.formatDateTime.epoch_zero")
{
    // 0 = 1970-01-01 00:00:00 UTC
    CHECK(executeSourceAndGetOutput("print (formatDateTime 0)") == "1970-01-01 00:00:00");
}

// --- isReadable/isWritable/isExecutable helper tests ---

TEST_CASE("IRGenerator.FSharp.isReadable.true")
{
    // 0644 = 420 — has read bits
    CHECK(executeSourceAndGetOutput("print (isReadable 420)") == "true");
}

TEST_CASE("IRGenerator.FSharp.isReadable.false")
{
    // 0000 — no permissions
    CHECK(executeSourceAndGetOutput("print (isReadable 0)") == "false");
}

TEST_CASE("IRGenerator.FSharp.isWritable.true")
{
    // 0644 = 420 — has write bits
    CHECK(executeSourceAndGetOutput("print (isWritable 420)") == "true");
}

TEST_CASE("IRGenerator.FSharp.isWritable.false")
{
    // 0444 = 292 — read-only
    CHECK(executeSourceAndGetOutput("print (isWritable 292)") == "false");
}

TEST_CASE("IRGenerator.FSharp.isExecutable.true")
{
    // 0755 = 493 — has execute bits
    CHECK(executeSourceAndGetOutput("print (isExecutable 493)") == "true");
}

TEST_CASE("IRGenerator.FSharp.isExecutable.false")
{
    // 0644 = 420 — no execute bits
    CHECK(executeSourceAndGetOutput("print (isExecutable 420)") == "false");
}

// --- Combined ls + helpers tests ---

TEST_CASE("IRGenerator.FSharp.ls.map_formatMode")
{
    // Map over ls results to format mode as rwx strings
    CHECK(executeSourceAndGetOutput("print (ls |> map (fun f -> formatMode f.mode))")
          == "[rwxr-xr-x; rw-r--r--; rwxr-xr-x]");
}

TEST_CASE("IRGenerator.FSharp.ls.filter_executable")
{
    // Filter files where mode is executable
    CHECK(executeSourceAndGetOutput(
              "print (ls |> filter (fun f -> isExecutable f.mode) |> map (fun f -> f.name))")
          == "[docs; script.sh]");
}

// =============================================================================
// Structured Pipeline Tests (Output Recognition Files)
// =============================================================================

// --- docker ps tests ---

TEST_CASE("IRGenerator.StructuredPipeline.docker_ps.basic")
{
    CHECK(structuredExecutesWithOutput("docker ps |> map (fun c -> c.names) |> print",
                                       "[web-server; db-main; cache]"));
}

TEST_CASE("IRGenerator.StructuredPipeline.docker_ps.filter_status")
{
    CHECK(structuredExecutesWithOutput(
        "docker ps |> filter (fun c -> c.status |> contains \"Up\") |> map (fun c -> c.names) |> print",
        "[web-server; db-main]"));
}

TEST_CASE("IRGenerator.StructuredPipeline.docker_ps.field_access_image")
{
    CHECK(structuredExecutesWithOutput("docker ps |> map (fun c -> c.image) |> print",
                                       "[nginx:latest; postgres:16; redis:7]"));
}

TEST_CASE("IRGenerator.StructuredPipeline.docker_ps.pipeline_chain")
{
    CHECK(structuredExecutesWithOutput("docker ps |> filter (fun c -> c.status |> contains \"Up\") |> map "
                                       "(fun c -> c.id) |> take 1 |> print",
                                       "[abc123def]"));
}

TEST_CASE("IRGenerator.StructuredPipeline.docker_ps.count_with_length")
{
    CHECK(structuredExecutesWithOutput(
        "docker ps |> filter (fun c -> c.ports |> contains \"tcp\") |> length |> fun n -> print n", "2"));
}

TEST_CASE("IRGenerator.StructuredPipeline.docker_ps.exists")
{
    CHECK(structuredExecutesWithOutput(
        "docker ps |> exists (fun c -> c.names == \"db-main\") |> fun r -> print r", "true"));
}

TEST_CASE("IRGenerator.StructuredPipeline.docker_ps.find")
{
    CHECK(structuredExecutesWithOutput(
        "docker ps |> find (fun c -> c.names == \"cache\") |> fun x -> match x with\n"
        "| Some c -> print c.image\n"
        "| None -> print \"not found\"",
        "redis:7"));
}

// --- docker images tests ---

TEST_CASE("IRGenerator.StructuredPipeline.docker_images.basic")
{
    CHECK(structuredExecutesWithOutput("docker images |> map (fun i -> i.repository) |> print",
                                       "[nginx; postgres; redis]"));
}

TEST_CASE("IRGenerator.StructuredPipeline.docker_images.filter_by_tag")
{
    CHECK(structuredExecutesWithOutput(
        "docker images |> filter (fun i -> i.tag == \"latest\") |> map (fun i -> i.repository) |> print",
        "[nginx]"));
}

TEST_CASE("IRGenerator.StructuredPipeline.docker_images.field_access")
{
    CHECK(structuredExecutesWithOutput("docker images |> map (fun i -> i.tag) |> print", "[latest; 16; 7]"));
}

// --- git log tests ---

TEST_CASE("IRGenerator.StructuredPipeline.git_log.basic")
{
    CHECK(structuredExecutesWithOutput("git log |> map (fun c -> c.message) |> print",
                                       "[feat: add login; fix: null check; docs: update README]"));
}

TEST_CASE("IRGenerator.StructuredPipeline.git_log.filter_by_author")
{
    CHECK(structuredExecutesWithOutput(
        "git log |> filter (fun c -> c.author == \"Alice\") |> map (fun c -> c.sha) |> print",
        "[abc123; ghi789]"));
}

TEST_CASE("IRGenerator.StructuredPipeline.git_log.take")
{
    CHECK(structuredExecutesWithOutput("git log |> take 2 |> map (fun c -> c.author) |> print",
                                       "[Alice; Bob]"));
}

// --- git status tests ---

TEST_CASE("IRGenerator.StructuredPipeline.git_status.basic")
{
    CHECK(structuredExecutesWithOutput("git status |> map (fun e -> e.path) |> print",
                                       "[src/main.cpp; README.md; .gitignore]"));
}

TEST_CASE("IRGenerator.StructuredPipeline.git_status.filter_modified")
{
    CHECK(structuredExecutesWithOutput(
        "git status |> filter (fun e -> e.status |> contains \"M\") |> map (fun e -> e.path) |> print",
        "[src/main.cpp]"));
}

// --- Edge cases ---

TEST_CASE("IRGenerator.StructuredPipeline.no_definition_fallback")
{
    // Unknown command with |> should still parse and generate IR successfully
    CHECK(generatesIRSuccessfully("echo hello |> print"));
}

TEST_CASE("IRGenerator.StructuredPipeline.pipeline_to_print")
{
    CHECK(structuredExecutesWithOutput("docker ps |> map (fun c -> c.names) |> print",
                                       "[web-server; db-main; cache]"));
}

TEST_CASE("IRGenerator.StructuredPipeline.docker_ps.each")
{
    CHECK(
        structuredExecutesWithOutput("docker ps |> each (fun c -> print c.names)", "web-serverdb-maincache"));
}

TEST_CASE("IRGenerator.StructuredPipeline.each_println_records")
{
    // each println on a list of records should format each record, not print raw pointers
    CHECK(executeSourceAndGetOutput("let _ = jobs |> each println")
          == "{ id = 1; state = Running; command = sleep 100; pid = 1234 }\n"
             "{ id = 2; state = Stopped; command = vim; pid = 5678 }\n"
             "{ id = 3; state = Done; command = make build; pid = 9012 }\n");
}

TEST_CASE("IRGenerator.StructuredPipeline.docker_ps.each_println")
{
    // Structured command records should also format correctly via each println
    CHECK(structuredExecutesWithOutput(
        "docker ps |> each println",
        "{ id = abc123def; image = nginx:latest; command = /docker-entrypoint…; "
        "created = 2024-01-15 10:00:00; status = Up 3 hours; ports = 80/tcp; names = web-server }\n"
        "{ id = def456ghi; image = postgres:16; command = docker-entrypoint.s…; "
        "created = 2024-01-14 08:00:00; status = Up 2 days; ports = 5432/tcp; names = db-main }\n"
        "{ id = ghi789jkl; image = redis:7; command = docker-entrypoint.s…; "
        "created = 2024-01-13 12:00:00; status = Exited (0) 1 hour ago; ports = ; names = cache }\n"));
}

// --- Placeholder lambda sugar with structured records ---

TEST_CASE("IRGenerator.StructuredPipeline.placeholder.docker_ps_map_names")
{
    CHECK(structuredExecutesWithOutput("docker ps |> map _.names |> print", "[web-server; db-main; cache]"));
}

TEST_CASE("IRGenerator.StructuredPipeline.placeholder.docker_ps_filter_status")
{
    CHECK(structuredExecutesWithOutput(
        "docker ps |> filter (_.status |> contains \"Up\") |> map _.names |> print",
        "[web-server; db-main]"));
}

TEST_CASE("IRGenerator.StructuredPipeline.placeholder.docker_ps_map_image")
{
    CHECK(structuredExecutesWithOutput("docker ps |> map _.image |> print",
                                       "[nginx:latest; postgres:16; redis:7]"));
}

TEST_CASE("IRGenerator.StructuredPipeline.placeholder.docker_images_filter_tag")
{
    CHECK(structuredExecutesWithOutput(
        "docker images |> filter (_.tag == \"latest\") |> map _.repository |> print", "[nginx]"));
}

TEST_CASE("IRGenerator.StructuredPipeline.placeholder.git_log_map_message")
{
    CHECK(structuredExecutesWithOutput("git log |> map _.message |> print",
                                       "[feat: add login; fix: null check; docs: update README]"));
}

TEST_CASE("IRGenerator.StructuredPipeline.placeholder.git_log_filter_author")
{
    CHECK(structuredExecutesWithOutput("git log |> filter (_.author == \"Alice\") |> map _.sha |> print",
                                       "[abc123; ghi789]"));
}

TEST_CASE("IRGenerator.StructuredPipeline.placeholder.git_status_filter_modified")
{
    CHECK(structuredExecutesWithOutput(
        "git status |> filter (_.status |> contains \"M\") |> map _.path |> print", "[src/main.cpp]"));
}

// =============================================================================
// Bare Expression Evaluation Tests
// =============================================================================

TEST_CASE("IRGenerator.BareExpr.number")
{
    CHECK(executeSourceAndGetOutput("42") == "42\n");
}

TEST_CASE("IRGenerator.BareExpr.arithmetic")
{
    CHECK(executeSourceAndGetOutput("(3 + 4)") == "7\n");
}

TEST_CASE("IRGenerator.BareExpr.list")
{
    // List literals at shell prompt need parentheses since [ is a shell identifier char
    CHECK(executeSourceAndGetOutput("([1; 2; 3])") == "[1; 2; 3]\n");
}

TEST_CASE("IRGenerator.BareExpr.option_some")
{
    CHECK(executeSourceAndGetOutput("Some 42") == "Some 42\n");
}

TEST_CASE("IRGenerator.BareExpr.option_none")
{
    CHECK(executeSourceAndGetOutput("None") == "None\n");
}

TEST_CASE("IRGenerator.BareExpr.result_ok")
{
    CHECK(executeSourceAndGetOutput("Ok 5") == "Ok 5\n");
}

TEST_CASE("IRGenerator.BareExpr.tuple")
{
    CHECK(executeSourceAndGetOutput("(1, 2)") == "(1, 2)\n");
}

TEST_CASE("IRGenerator.BareExpr.bool_true")
{
    CHECK(executeSourceAndGetOutput("(true)") == "true\n");
}

TEST_CASE("IRGenerator.BareExpr.bool_false")
{
    CHECK(executeSourceAndGetOutput("(false)") == "false\n");
}

TEST_CASE("IRGenerator.BareExpr.lambda_apply")
{
    CHECK(executeSourceAndGetOutput("(fun x -> x + 1) 5") == "6\n");
}

TEST_CASE("IRGenerator.BareExpr.nested_multiply")
{
    // Use let to bind intermediate result since (( is shell arithmetic in shell mode
    CHECK(executeSourceAndGetOutput("(2 + 3)") == "5\n");
}

TEST_CASE("IRGenerator.BareExpr.result_error")
{
    CHECK(executeSourceAndGetOutput("Error 99") == "Error 99\n");
}

TEST_CASE("IRGenerator.BareExpr.negative_number")
{
    CHECK(executeSourceAndGetOutput("(-7)") == "-7\n");
}

TEST_CASE("IRGenerator.BareExpr.float_literal")
{
    CHECK(executeSourceAndGetOutput("(3.14)") == "3.14\n");
}

// ========================================================================
// Statement-level F# control flow (unified — replaces shell control flow)
// ========================================================================

TEST_CASE("IRGenerator.FSharp.stmt_if_then_else")
{
    CHECK(executeSourceAndGetOutput("if true then print 1 else print 0") == "1");
    CHECK(executeSourceAndGetOutput("if false then print 1 else print 0") == "0");
    CHECK(executeSourceAndGetOutput("if 3 > 1 then print \"yes\" else print \"no\"") == "yes");
}

TEST_CASE("IRGenerator.FSharp.stmt_if_in_let")
{
    CHECK(executeSourceAndGetOutput("let x = 5; if x > 3 then print x else print 0") == "5");
}

TEST_CASE("IRGenerator.FSharp.stmt_if_without_else")
{
    // if-then without else returns unit (F# style)
    CHECK(executeSourceAndGetOutput("if true then print 1") == "1");
    CHECK(executeSourceAndGetOutput("if false then print 1") == "");
    CHECK(executeSourceAndGetOutput("let mut x = 0\nif true then x <- 42\nprint x") == "42");
    CHECK(executeSourceAndGetOutput("let mut x = 0\nif false then x <- 42\nprint x") == "0");
}

TEST_CASE("IRGenerator.FSharp.stmt_while_basic")
{
    CHECK(generatesIRSuccessfully("while false do end"));
    CHECK(generatesIRSuccessfully("while false do echo hi; end"));
    CHECK(generatesIRSuccessfully("while true do break end"));
    CHECK(executeSourceAndGetOutput("let mut x = 3\nwhile x > 0 do\nprint x\nx <- x - 1\nend") == "321");
    CHECK(executeSourceAndGetOutput("let mut x = 3; while x > 0 do print x; x <- x - 1; end") == "321");
}

TEST_CASE("IRGenerator.FSharp.stmt_while_break")
{
    CHECK(executeSourceAndGetOutput(
              "let mut x = 0; while true do x <- x + 1; if x == 3 then break else print x; end")
          == "12");
    CHECK(executeSourceAndGetOutput(
              "let mut x = 0\nwhile true do\nx <- x + 1\nif x == 3 then break else print x\nend")
          == "12");
}

TEST_CASE("IRGenerator.FSharp.stmt_for_simple_variable")
{
    CHECK(executeSourceAndGetOutput("for x in [1; 2; 3] do print x end") == "123");
}

TEST_CASE("IRGenerator.FSharp.stmt_for_destructuring")
{
    CHECK(executeSourceAndGetOutput("for (a, b) in [(1, 2); (3, 4)] do print a end") == "13");
}

TEST_CASE("IRGenerator.FSharp.stmt_for_break")
{
    CHECK(executeSourceAndGetOutput("for x in [1; 2; 3; 4; 5] do\nif x == 3 then break else print x\nend")
          == "12");
}

TEST_CASE("IRGenerator.FSharp.stmt_for_continue")
{
    CHECK(executeSourceAndGetOutput("for x in [1; 2; 3] do\nif x == 2 then continue else print x\nend")
          == "13");
}

TEST_CASE("IRGenerator.FSharp.stmt_for_bare_range")
{
    // Bare range expression (without brackets) in for-in loop
    CHECK(executeSourceAndGetOutput("for i in 1..5 do print i end") == "12345");
}

TEST_CASE("IRGenerator.FSharp.stmt_for_bare_range_step")
{
    // Bare range with step
    CHECK(executeSourceAndGetOutput("for i in 1..2..10 do print i end") == "13579");
}

TEST_CASE("IRGenerator.FSharp.stmt_for_bracketed_range")
{
    // Bracketed range still works
    CHECK(executeSourceAndGetOutput("for i in [1..5] do print i end") == "12345");
}

TEST_CASE("IRGenerator.FSharp.bare_range_let_binding")
{
    // Bare range as let binding
    CHECK(generatesIRSuccessfully("let xs = 1..5"));
}

// ============================================================================
// Structured jobs command
// ============================================================================

TEST_CASE("IRGenerator.FSharp.structured.jobs_map_id")
{
    CHECK(executeSourceAndGetOutput("let ids = jobs |> map (fun j -> j.id)\nprint ids") == "[1; 2; 3]");
}

TEST_CASE("IRGenerator.FSharp.structured.jobs_map_command")
{
    CHECK(executeSourceAndGetOutput("let cmds = jobs |> map (fun j -> j.command)\nprint cmds")
          == "[sleep 100; vim; make build]");
}

TEST_CASE("IRGenerator.FSharp.structured.jobs_map_state")
{
    CHECK(executeSourceAndGetOutput("let states = jobs |> map (fun j -> j.state)\nprint states")
          == "[Running; Stopped; Done]");
}

TEST_CASE("IRGenerator.FSharp.structured.jobs_map_pid")
{
    CHECK(executeSourceAndGetOutput("let pids = jobs |> map (fun j -> j.pid)\nprint pids")
          == "[1234; 5678; 9012]");
}

TEST_CASE("IRGenerator.FSharp.structured.jobs_placeholder_state")
{
    CHECK(executeSourceAndGetOutput("let states = jobs |> map _.state\nprint states")
          == "[Running; Stopped; Done]");
}

TEST_CASE("IRGenerator.FSharp.structured.jobs_placeholder_command")
{
    CHECK(executeSourceAndGetOutput("let cmds = jobs |> map _.command\nprint cmds")
          == "[sleep 100; vim; make build]");
}

TEST_CASE("IRGenerator.FSharp.structured.jobs_placeholder_pid")
{
    CHECK(executeSourceAndGetOutput("let pids = jobs |> map _.pid\nprint pids") == "[1234; 5678; 9012]");
}

TEST_CASE("IRGenerator.FSharp.structured.jobs_filter_state")
{
    CHECK(executeSourceAndGetOutput(
              "let ids = jobs |> filter (fun j -> j.state == \"Running\") |> map (fun j -> j.id)\nprint ids")
          == "[1]");
}

TEST_CASE("IRGenerator.FSharp.structured.jobs_filter_placeholder")
{
    CHECK(executeSourceAndGetOutput(
              "let cmds = jobs |> filter (_.state == \"Done\") |> map _.command\nprint cmds")
          == "[make build]");
}

TEST_CASE("IRGenerator.FSharp.structured.jobs_filter_stopped")
{
    CHECK(
        executeSourceAndGetOutput("let ids = jobs |> filter (_.state == \"Stopped\") |> map _.id\nprint ids")
        == "[2]");
}

TEST_CASE("IRGenerator.FSharp.structured.jobs_exists")
{
    CHECK(executeSourceAndGetOutput("let r = jobs |> exists (fun j -> j.state == \"Stopped\")\nprint r")
          == "true");
}

TEST_CASE("IRGenerator.FSharp.structured.jobs_exists_false")
{
    CHECK(executeSourceAndGetOutput("let r = jobs |> exists (fun j -> j.state == \"Terminated\")\nprint r")
          == "false");
}

TEST_CASE("IRGenerator.FSharp.structured.jobs_find")
{
    CHECK(executeSourceAndGetOutput("match jobs |> find (fun j -> j.id == 2) with\n"
                                    "| Some j -> print j.command\n"
                                    "| None -> print \"none\"")
          == "vim");
}

TEST_CASE("IRGenerator.FSharp.structured.jobs_take")
{
    CHECK(executeSourceAndGetOutput("let ids = jobs |> take 2 |> map _.id\nprint ids") == "[1; 2]");
}

TEST_CASE("IRGenerator.FSharp.structured.jobs_chained")
{
    CHECK(executeSourceAndGetOutput(
              "let cmds = jobs |> filter (_.state == \"Running\") |> map _.command\nprint cmds")
          == "[sleep 100]");
}

// =============================================================================
// Data Source Wrapper Tests (Phase 6.1.3)
// =============================================================================

TEST_CASE("IRGenerator.DataSource.open_json_parses")
{
    CHECK(generatesIRSuccessfully("open-json \"test.json\" as { name: string; age: int }"));
}

TEST_CASE("IRGenerator.DataSource.open_csv_parses")
{
    CHECK(generatesIRSuccessfully("open-csv \"test.csv\" as { name: string; age: int }"));
}

TEST_CASE("IRGenerator.DataSource.from_json_parses")
{
    // Standalone from-json reads from stdin
    CHECK(generatesIRSuccessfully("from-json as { name: string; age: int }"));
}

TEST_CASE("IRGenerator.DataSource.from_csv_parses")
{
    CHECK(generatesIRSuccessfully("from-csv as { name: string; age: int }"));
}

TEST_CASE("IRGenerator.DataSource.open_json_named_type")
{
    // Using a named type reference
    CHECK(generatesIRSuccessfully(
        "type Person = { name: string; age: int }\nopen-json \"test.json\" as Person"));
}

TEST_CASE("IRGenerator.DataSource.open_csv_named_type")
{
    CHECK(
        generatesIRSuccessfully("type Record = { user: string; pid: int }\nopen-csv \"data.csv\" as Record"));
}

TEST_CASE("IRGenerator.DataSource.open_json_with_pipeline")
{
    // Data source followed by pipeline
    CHECK(
        generatesIRSuccessfully("open-json \"test.json\" as { name: string; age: int } |> length |> print"));
}

TEST_CASE("IRGenerator.DataSource.open_csv_with_pipeline")
{
    CHECK(generatesIRSuccessfully(
        "open-csv \"data.csv\" as { name: string; score: int } |> map _.name |> print"));
}

TEST_CASE("IRGenerator.DataSource.from_json_with_pipeline")
{
    CHECK(generatesIRSuccessfully("from-json as { id: int; status: string } |> filter (_.id > 0) |> print"));
}

TEST_CASE("IRGenerator.DataSource.open_json_bool_field")
{
    CHECK(generatesIRSuccessfully("open-json \"test.json\" as { name: string; active: bool }"));
}

TEST_CASE("IRGenerator.DataSource.open_json_float_field")
{
    CHECK(generatesIRSuccessfully("open-json \"test.json\" as { name: string; score: float }"));
}

TEST_CASE("IRGenerator.DataSource.unknown_named_type_error")
{
    // Referencing a non-existent type should produce an error
    CHECK(generatesIRWithError("open-json \"test.json\" as UnknownType", "Unknown type"));
}

TEST_CASE("IRGenerator.DataSource.pipe_chain_from_json")
{
    // Shell pipe chain ending with from-json
    CHECK(generatesIRSuccessfully("echo hello | from-json as { name: string }"));
}

TEST_CASE("IRGenerator.DataSource.pipe_chain_from_csv")
{
    CHECK(generatesIRSuccessfully("echo hello | from-csv as { name: string; value: int }"));
}

// =============================================================================
// Bare Structured Command Routing (Phase 6.4.3)
// =============================================================================

TEST_CASE("IRGenerator.FSharp.structured.bare_ps_produces_output")
{
    // Bare `ps` at statement level should route through F# path with display_result
    auto result = endo::test::executeSourceWithStructuredState("ps");
    REQUIRE(result.has_value());
    CHECK(!result->output.empty()); // display_result produces record list output
}

TEST_CASE("IRGenerator.FSharp.structured.bare_jobs_produces_output")
{
    // Bare `jobs` at statement level should route through F# path
    auto result = endo::test::executeSourceWithStructuredState("jobs");
    REQUIRE(result.has_value());
    CHECK(!result->output.empty()); // display_result produces list output
}

TEST_CASE("IRGenerator.FSharp.structured.bare_ls_produces_output")
{
    // Bare `ls` at statement level should route through F# path
    auto result = endo::test::executeSourceWithStructuredState("ls");
    REQUIRE(result.has_value());
    CHECK(!result->output.empty());
}

TEST_CASE("IRGenerator.FSharp.structured.bare_ls_with_string_path")
{
    // ls with quoted string path should route through F# path
    auto result = endo::test::executeSourceWithStructuredState("ls \".\"");
    REQUIRE(result.has_value());
    CHECK(!result->output.empty());
}

TEST_CASE("IRGenerator.FSharp.structured.bare_ps_with_pipeline")
{
    // ps with pipeline should route through F# path (mock PIDs: 1, 42, 100)
    CHECK(executeSourceAndGetOutput("let r = ps |> map (fun p -> p.pid)\nprint r") == "[1; 42; 100]");
}

TEST_CASE("IRGenerator.FSharp.structured.bare_jobs_with_pipeline")
{
    // jobs with pipeline at statement level (mock IDs: 1, 2, 3)
    CHECK(executeSourceAndGetOutput("let r = jobs |> map (fun j -> j.id)\nprint r") == "[1; 2; 3]");
}

// =============================================================================
// toText Tests (Phase 6.4.3)
// =============================================================================

TEST_CASE("IRGenerator.FSharp.structured.toText_in_pipeline")
{
    // ps |> toText should produce string representation instead of table
    CHECK(generatesIRSuccessfully("let r = ps |> toText\nprint r"));
}

TEST_CASE("IRGenerator.FSharp.structured.toText_direct_call")
{
    // toText as a direct call
    CHECK(generatesIRSuccessfully("let r = toText ps\nprint r"));
}

TEST_CASE("IRGenerator.FSharp.structured.toText_with_jobs")
{
    // toText with jobs in pipeline
    CHECK(generatesIRSuccessfully("let r = jobs |> toText\nprint r"));
}

// ============================================================================
// Variadic Parameters and Shell Aliases
// ============================================================================

TEST_CASE("IRGenerator.FSharp.variadic.basic_list_collection")
{
    // Variadic parameter collects extra args into a list
    CHECK(generatesIRSuccessfully("let f ...xs = xs; f 1 2 3"));
}

TEST_CASE("IRGenerator.FSharp.variadic.zero_variadic_args")
{
    // Zero variadic args should produce an empty list
    CHECK(generatesIRSuccessfully("let f ...xs = xs; f"));
}

TEST_CASE("IRGenerator.FSharp.variadic.mixed_fixed_and_variadic")
{
    // Fixed param + variadic collects remaining into list
    CHECK(generatesIRSuccessfully("let f x ...rest = x; f 1 2 3"));
}

TEST_CASE("IRGenerator.FSharp.variadic.alias_with_string_args")
{
    // Alias with a string argument passed through splat
    CHECK(executeSourceAndGetOutput(R"(let greet ...args = & echo hello ...args; greet "world")")
          == "hello world\n");
}

TEST_CASE("IRGenerator.FSharp.variadic.alias_multiple_string_args")
{
    // Alias with multiple string arguments passed through splat
    CHECK(executeSourceAndGetOutput(R"(let greet ...args = & echo hello ...args; greet "foo" "bar")")
          == "hello foo bar\n");
}

TEST_CASE("IRGenerator.FSharp.variadic.capture_mode_with_splat")
{
    // let binding = expression context → capture mode, with splat to trigger dynamic cmd path
    CHECK(executeSourceAndGetOutput(R"(let run ...args = & echo test ...args; print (run "a"))") == "test a");
}

TEST_CASE("IRGenerator.FSharp.variadic.stmt_level_with_splat")
{
    // Statement-level shell command via alias function — normal I/O (no capture)
    CHECK(executeSourceAndGetOutput(R"(let say ...args = & echo hi ...args; say "there")") == "hi there\n");
}

TEST_CASE("IRGenerator.FSharp.variadic.ir_generation")
{
    // IR generation for various variadic + splat patterns
    CHECK(generatesIRSuccessfully(R"(let ll ...args = & echo -l ...args; ll "hello")"));
    CHECK(generatesIRSuccessfully(R"(let ll ...args = & echo -l ...args; ll "a" "b")"));
    // Shell command with splat, single fixed arg
    CHECK(generatesIRSuccessfully(R"(let run x ...args = & echo x ...args; run "test" "a")"));
}

TEST_CASE("IRGenerator.FSharp.variadic.stmt_level_ampersand")
{
    // & at statement level generates IR successfully
    CHECK(generatesIRSuccessfully("& echo test"));
}

TEST_CASE("IRGenerator.FSharp.variadic.zero_arg_invocation")
{
    // Zero-arg variadic function at statement level runs the body
    CHECK(executeSourceAndGetOutput("let greet ...xs = print \"hello\"; greet") == "hello");
}

TEST_CASE("IRGenerator.FSharp.variadic.unquoted_shell_args")
{
    // Unquoted identifiers as shell arguments at statement level (splat to shell command)
    CHECK(executeSourceAndGetOutput(R"(let say ...xs = & echo ...xs; say hello world)") == "hello world\n");
}

TEST_CASE("IRGenerator.FSharp.variadic.flag_args")
{
    // Shell flags as arguments at statement level
    CHECK(executeSourceAndGetOutput(R"(let say ...xs = & echo ...xs; say -la)") == "-la\n");
}

TEST_CASE("IRGenerator.FSharp.variadic.each_print_strings")
{
    // Variadic function iterating string args with each+print
    CHECK(executeSourceAndGetOutput(R"(let say ...xs = each (fun x -> print x) xs; say hello world)")
          == "helloworld");
}

TEST_CASE("IRGenerator.FSharp.variadic.each_print_flag_args")
{
    // Variadic function iterating flag args with each+print
    CHECK(executeSourceAndGetOutput(R"(let say ...xs = each (fun x -> print x) xs; say -la)") == "-la");
}

// =============================================================================
// F# list element string printing tests
// =============================================================================

TEST_CASE("IRGenerator.FSharp.list.each_print_strings")
{
    CHECK(executeSourceAndGetOutput(R"(each print ["hello"; "world"])") == "helloworld");
}

TEST_CASE("IRGenerator.FSharp.list.each_println_strings")
{
    CHECK(executeSourceAndGetOutput(R"(each println ["hello"; "world"])") == "hello\nworld\n");
}

TEST_CASE("IRGenerator.FSharp.list.each_print_numbers")
{
    // Numbers should still work correctly with element type tracking
    CHECK(executeSourceAndGetOutput(R"(each print [1; 2; 3])") == "123");
}

// =============================================================================
// F# fetch builtin IR Generation Tests
// =============================================================================

TEST_CASE("IRGenerator.FSharp.fetch.single_arg")
{
    // fetch with URL string generates IR with Result annotation
    REQUIRE(generatesIRSuccessfully(R"(let r = fetch "https://example.com")"));
}

TEST_CASE("IRGenerator.FSharp.fetch.two_args")
{
    // fetch with URL and headers list generates IR
    REQUIRE(generatesIRSuccessfully(R"(let r = fetch "https://example.com" ["Authorization: Bearer tok"])"));
}

TEST_CASE("IRGenerator.FSharp.fetch.match_on_result")
{
    // fetch result can be pattern matched as Ok/Error
    REQUIRE(generatesIRSuccessfully(
        R"(let r = match fetch "https://example.com" with | Ok body -> body | Error msg -> msg)"));
}

TEST_CASE("IRGenerator.FSharp.fetch.question_mark_operator")
{
    // fetch result works with ? operator
    REQUIRE(generatesIRSuccessfully(R"(let body = (fetch "https://example.com")?)"));
}

TEST_CASE("IRGenerator.FSharp.fetch.non_string_url_error")
{
    // fetch with a non-string URL argument should be a type error
    CHECK(generatesIRWithError("let r = fetch 42", "fetch url argument must be a string"));
}

TEST_CASE("IRGenerator.FSharp.fetch.three_args_error")
{
    // fetch with too many arguments should be a type error
    CHECK(generatesIRWithError(R"(let r = fetch "a" "b" "c")", "fetch requires 1 or 2 arguments"));
}

// =============================================================================
// List/Tuple with block-creating elements (env, if-then-else)
// =============================================================================

TEST_CASE("IRGenerator.FSharp.list_env_calls_in_list")
{
    // env calls create control-flow blocks (some/none/merge diamond);
    // storing each element to an alloca immediately prevents block-boundary
    // cleanup from discarding intermediate values.
    CHECK(executesSuccessfully(R"(let r = [(env "HOME"); (env "PATH")]; print r)"));
}

TEST_CASE("IRGenerator.FSharp.list_if_then_else_elements")
{
    // if-then-else creates blocks; same pattern as env
    CHECK(executeSourceAndGetOutput("let r = [if true then 1 else 2; if false then 3 else 4]; print r")
          == "[1; 4]");
}

TEST_CASE("IRGenerator.FSharp.list_compiled_function_calls")
{
    // Function-compiled (UCALL) function calls as list elements
    CHECK(executeSourceAndGetOutput("let f (x: int) : int = x * 2\nlet r = [f 1; f 2; f 3]\nprint r")
          == "[2; 4; 6]");
}

TEST_CASE("IRGenerator.FSharp.list_inlined_function_calls")
{
    // AST-inlined function calls (no control-flow blocks)
    CHECK(executeSourceAndGetOutput("let f x = x + 1\nlet r = [f 10; f 20]\nprint r") == "[11; 21]");
}

TEST_CASE("IRGenerator.FSharp.list_builtin_function_calls")
{
    // Native callback calls as list elements
    CHECK(executeSourceAndGetOutput(R"(let r = [string_length "abc"; string_length "hello"]; print r)")
          == "[3; 5]");
}

TEST_CASE("IRGenerator.FSharp.list_mixed_custom_and_builtin")
{
    // Mix of compiled function (string) + block-creating builtin (option<string>) is heterogeneous
    CHECK(generatesIRWithError(R"(let f (x: string) : string = x; let r = [f "test"; env "HOME"]; print r)",
                               "List elements must have the same type"));
}

TEST_CASE("IRGenerator.FSharp.list_heterogeneous_int_and_string")
{
    // Mixing int and string in list literal should be a type error
    CHECK(generatesIRWithError(R"(let r = [1; "hello"])", "List elements must have the same type"));
}

TEST_CASE("IRGenerator.FSharp.list_heterogeneous_string_and_option")
{
    // Mixing string and option<string> in list literal should be a type error
    CHECK(generatesIRWithError(R"(let r = ["foo"; (env "HOME")])", "List elements must have the same type"));
}

TEST_CASE("IRGenerator.FSharp.list_homogeneous_options")
{
    // Homogeneous list of options should still work
    CHECK(executesSuccessfully(R"(let r = [(env "HOME"); (env "PATH")]; print r)"));
}

TEST_CASE("IRGenerator.FSharp.tuple_env_calls")
{
    // Tuple with env calls — each creates control-flow blocks
    CHECK(executesSuccessfully(R"(let t = ((env "HOME"), (env "PATH")); print "ok")"));
}

TEST_CASE("IRGenerator.FSharp.tuple_if_then_else")
{
    // Tuple with if-then-else elements
    CHECK(executeSourceAndGetOutput("let t = (if true then 1 else 2, if false then 3 else 4)\nprint \"ok\"")
          == "ok");
}

// =============================================================================
// Option/Result in Binary Operation — Type Error Detection
// =============================================================================

TEST_CASE("IRGenerator.FSharp.option_in_binary_add")
{
    // Using Option directly in + should produce a type error
    CHECK(generatesIRWithError(R"(let r = (env "HOME") + "/.local/bin")", "must be unwrapped first"));
}

TEST_CASE("IRGenerator.FSharp.option_in_binary_comparison")
{
    // Using Option directly in == should produce a type error
    CHECK(generatesIRWithError(R"(let r = (env "HOME") == "foo")", "must be unwrapped first"));
}

TEST_CASE("IRGenerator.FSharp.result_in_binary_add")
{
    // Using Result directly in + should produce a type error
    CHECK(generatesIRWithError(R"(let r = (Ok 42) + 1)", "must be unwrapped first"));
}

TEST_CASE("IRGenerator.FSharp.option_unwrapped_in_binary_ok")
{
    // Using unwrapped Option with ? should compile without type errors (IR generation succeeds)
    CHECK(generatesIRSuccessfully(R"(let r = (env "HOME")? + "/.local/bin"; print r)"));
}

TEST_CASE("IRGenerator.FSharp.result_unwrapped_in_binary_ok")
{
    // Using unwrapped Result with ? should compile without type errors
    CHECK(generatesIRSuccessfully(R"(let r = (Ok 42)? + 1; print r)"));
}

// --- rand builtin tests ---

TEST_CASE("IRGenerator.FSharp.rand_no_args")
{
    // rand with no arguments returns a random positive integer > 0
    auto const output = executeSourceAndGetOutput("let x = rand; print (if x > 0 then 1 else 0)");
    CHECK(output == "1");
}

TEST_CASE("IRGenerator.FSharp.rand_range")
{
    // rand A B returns a value in [A, B]; test with a tight range
    auto const output = executeSourceAndGetOutput("let x = rand 5 5; print x");
    CHECK(output == "5");
}

TEST_CASE("IRGenerator.FSharp.rand_range_bounds")
{
    // rand 1 10 should always produce a value in [1, 10]; test via match guard
    auto const output = executeSourceAndGetOutput("let x = rand 1 10\n"
                                                  "print (if x >= 1 && x <= 10 then 1 else 0)");
    CHECK(output == "1");
}

TEST_CASE("IRGenerator.FSharp.rand_wrong_arity")
{
    // rand with 1 argument should produce a type error
    CHECK(generatesIRWithError("let x = rand 5", "rand requires 0 or 2 arguments"));
}

TEST_CASE("IRGenerator.FSharp.rand_in_let_binding")
{
    // rand can be used in let bindings and arithmetic
    CHECK(executesSuccessfully("let x = rand 1 100; let y = x + 1"));
}

TEST_CASE("IRGenerator.FSharp.rand_in_pipeline")
{
    // rand result can be piped to a function
    auto const output =
        executeSourceAndGetOutput("rand 1 10 |> fun n -> print (if n >= 1 && n <= 10 then 1 else 0)");
    CHECK(output == "1");
}

// =============================================================================
// exec keyword — dynamic command execution with pipe support
// =============================================================================

TEST_CASE("IRGenerator.FSharp.exec_single_command")
{
    // Single exec with literal program path and argument
    CHECK(executeSourceAndGetOutput(R"(exec "/bin/echo" "hello")") == "hello\n");
}

TEST_CASE("IRGenerator.FSharp.exec_multiple_args")
{
    // exec with multiple arguments
    CHECK(executeSourceAndGetOutput(R"(exec "/bin/echo" "hello" "world")") == "hello world\n");
}

TEST_CASE("IRGenerator.FSharp.exec_variable_program_path")
{
    // Variable program path
    CHECK(executeSourceAndGetOutput(R"(let p = "/bin/echo"; exec p "hi")") == "hi\n");
}

TEST_CASE("IRGenerator.FSharp.exec_variable_argument")
{
    // Variable arguments
    CHECK(executeSourceAndGetOutput(R"(let arg = "hello"; exec "/bin/echo" arg)") == "hello\n");
}

TEST_CASE("IRGenerator.FSharp.exec_ir_generation")
{
    // IR generation succeeds for exec
    CHECK(generatesIRSuccessfully(R"(exec "/bin/echo" "hello")"));
}

TEST_CASE("IRGenerator.FSharp.exec_pipeline_ir_generation")
{
    // IR generation succeeds for piped exec
    CHECK(generatesIRSuccessfully(R"(exec "/bin/echo" "hello" | exec "/bin/cat")"));
}

TEST_CASE("IRGenerator.FSharp.exec_three_stage_pipeline_ir")
{
    // IR generation succeeds for three-stage pipeline
    CHECK(generatesIRSuccessfully(
        R"(exec "/bin/echo" "hello" | exec "/bin/tr" "a-z" "A-Z" | exec "/bin/cat")"));
}

TEST_CASE("IRGenerator.FSharp.exec_in_match_arm")
{
    // exec inside match arm (parenthesized to disambiguate from |)
    CHECK(generatesIRSuccessfully(R"(
let x = Some "/bin/echo"
match x with
| Some p -> (exec p "test")
| None -> ()
)"));
}

TEST_CASE("IRGenerator.FSharp.exec_with_which")
{
    // The motivating use case: which + match + exec
    CHECK(generatesIRSuccessfully(R"(
match which "echo" with
| Some p -> (exec p "found it")
| None -> println "not found"
)"));
}

TEST_CASE("IRGenerator.FSharp.exec_with_tuple_pattern_match")
{
    // exec with pattern-matched variables from tuple scrutinee.
    // The bindings get Object/Void-typed allocas from tuple extraction,
    // but are strings at runtime. ensureString must cast them correctly.
    auto& rt = TestRuntime::instance();
    rt.clearMockWhichPaths();
    rt.setMockWhichPath("echo", "/bin/echo");
    rt.setMockWhichPath("cat", "/bin/cat");
    CHECK(executeSourceAndGetOutput(R"(
match (which "echo", which "cat") with
| (Some f, Some l) -> exec f "hello"
| (Some f, None) -> exec f "hello"
| (None, Some l) -> exec l
| (None, None) -> println "none"
)") == "hello\n");
    rt.clearMockWhichPaths();
}

TEST_CASE("IRGenerator.FSharp.exec_with_tuple_pattern_match_ir")
{
    // IR generation for exec with tuple pattern-matched variables
    CHECK(generatesIRSuccessfully(R"(
match (which "echo", which "cat") with
| (Some f, Some l) -> exec f "tuple test" | exec l
| (Some f, None) -> exec f "only first"
| (None, Some l) -> exec "/bin/echo" "fallback" | exec l
| (None, None) -> println "none found"
)"));
}

// ============================================================================
// Nested Recursive Functions with Multi-Statement Bodies
// ============================================================================

TEST_CASE("IRGenerator.FSharp.nested_rec_simple")
{
    // Simple nested recursive function
    CHECK(executeSourceAndGetOutput(
              "let f (n: int) =\n  let rec g (i: int) = if i <= 0 then 0 else g (i - 1)\n  print (g n)\nf 3")
          == "0");
}

TEST_CASE("IRGenerator.FSharp.nested_rec_with_outer_capture")
{
    // Nested recursive function captures outer parameter
    CHECK(executeSourceAndGetOutput("let f (n: int) =\n  let rec count (i: int) = if i >= n then i else "
                                    "count (i + 1)\n  print (count "
                                    "0)\nf 5")
          == "5");
}

TEST_CASE("IRGenerator.FSharp.nested_rec_multi_statement_body")
{
    // Multi-statement function body with println followed by computation
    CHECK(executeSourceAndGetOutput("let f (n: int) =\n  println \"start\"\n  print n\nf 42") == "start\n42");
}

TEST_CASE("IRGenerator.FSharp.nested_non_recursive_function")
{
    // Nested non-recursive function
    CHECK(executeSourceAndGetOutput("let f (x: int) =\n  let g (y: int) = y + 1\n  print (g x)\nf 10")
          == "11");
}

TEST_CASE("IRGenerator.FSharp.nested_rec_with_println_and_if")
{
    // Exact user script pattern: nested let rec with println + if-then-else
    CHECK(executeSourceAndGetOutput(R"(
let process (n: int) =
    let rec step (i: int) =
        println $"step {i}"
        if i >= n then
            println "done"
        else
            step (i + 1)
    step 1
process 3
)") == "step 1\nstep 2\nstep 3\ndone\n");
}

TEST_CASE("IRGenerator.FSharp.nested_rec_explicit_in_syntax")
{
    // Explicit 'in' syntax still works for recursive functions
    CHECK(executeSourceAndGetOutput(
              "let r = let rec f (x: int) = if x <= 0 then 0 else f (x - 1) in f 5; print r")
          == "0");
}

TEST_CASE("IRGenerator.FSharp.deeply_nested_functions")
{
    // Function inside function inside function
    CHECK(executeSourceAndGetOutput("let a (x: int) =\n  let b (y: int) =\n    let c (z: int) = z + y + x\n  "
                                    "  c 1\n  print (b 10)\na "
                                    "100")
          == "111");
}

TEST_CASE("IRGenerator.FSharp.nested_rec_ir_generation")
{
    // IR generation for nested recursive function
    CHECK(generatesIRSuccessfully(R"(
let process (n: int) =
    let rec step (i: int) =
        println $"step {i}"
        if i >= n then
            println "done"
        else
            step (i + 1)
    step 1
process 3
)"));
}
