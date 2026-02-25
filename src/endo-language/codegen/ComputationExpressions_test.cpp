// SPDX-License-Identifier: Apache-2.0
#include <endo-language/TestHelper.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace endo::test;

// =============================================================================
// time builtin — measures execution time, returns TimeSpan
// =============================================================================

TEST_CASE("ComputationExpressions.time.block_returns_timespan", "[computation][time]")
{
    // time { body } should return a TimeSpan with non-negative milliseconds
    CHECK(executesSuccessfully("let t = time { let x = 1 + 2; x }; print t.milliseconds"));
}

TEST_CASE("ComputationExpressions.time.block_side_effects", "[computation][time]")
{
    // Side effects inside the block should execute
    CHECK(executeSourceAndGetOutput("time { print \"hello\" }") == "hello");
}

TEST_CASE("ComputationExpressions.time.trivial_block", "[computation][time]")
{
    // Trivial block — should succeed and produce a TimeSpan
    CHECK(executesSuccessfully("let t = time { () }; print t.milliseconds"));
}

TEST_CASE("ComputationExpressions.time.timespan_formatting", "[computation][time]")
{
    // TimeSpan from time can be formatted
    CHECK(executesSuccessfully("let t = time { 42 }; print (formatTimeSpan t)"));
}

TEST_CASE("ComputationExpressions.time.let_binding", "[computation][time]")
{
    // time result can be bound to a variable
    CHECK(executesSuccessfully("let elapsed = time { let x = 10; x * 2 }"));
}

TEST_CASE("ComputationExpressions.time.block_with_multiple_statements", "[computation][time]")
{
    // Block with multiple statements
    CHECK(executeSourceAndGetOutput("time { print \"a\"; print \"b\" }") == "ab");
}

// =============================================================================
// Auto-thunk: block arguments wrapped as zero-arg lambdas
// =============================================================================

TEST_CASE("ComputationExpressions.auto_thunk.user_function_receives_thunk", "[computation][thunk]")
{
    // A user function accepting unit -> int can receive a block argument
    CHECK(executeSourceAndGetOutput("let run (f: unit -> int) = f ()\n"
                                    "print (run { 42 })")
          == "42");
}

TEST_CASE("ComputationExpressions.auto_thunk.block_with_computation", "[computation][thunk]")
{
    // Block auto-thunk with computation inside
    CHECK(executeSourceAndGetOutput("let run (f: unit -> int) = f ()\n"
                                    "print (run { let x = 10; x + 5 })")
          == "15");
}

TEST_CASE("ComputationExpressions.auto_thunk.block_in_let_not_thunked", "[computation][thunk]")
{
    // Block in let binding is NOT auto-thunked (only in function application position)
    CHECK(executeSourceAndGetOutput("let x = { let y = 1; y + 2 }; print x") == "3");
}

TEST_CASE("ComputationExpressions.auto_thunk.record_in_let_not_thunked", "[computation][thunk]")
{
    // Record literal in let binding is NOT auto-thunked
    CHECK(executeSourceAndGetOutput("type Person = { name: str; age: int }\n"
                                    "let p = { name = \"Alice\"; age = 30 }\n"
                                    "print p.name")
          == "Alice");
}

// =============================================================================
// time with pipeline
// =============================================================================

TEST_CASE("ComputationExpressions.time.pipeline_formatTimeSpan", "[computation][time][pipeline]")
{
    // time { body } |> formatTimeSpan should format the result
    CHECK(executesSuccessfully("time { 42 } |> formatTimeSpan |> print"));
}

// =============================================================================
// time as statement (auto-display)
// =============================================================================

TEST_CASE("ComputationExpressions.time.statement_level", "[computation][time]")
{
    // time at statement level should parse and execute
    CHECK(executesSuccessfully("time { let x = 1; x + 2 }"));
}
