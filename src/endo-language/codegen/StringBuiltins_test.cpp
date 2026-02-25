// SPDX-License-Identifier: Apache-2.0

#include <endo-language/TestHelper.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace endo::test;

// =================================================================================================
// split — direct call
// =================================================================================================

TEST_CASE("split.direct_call", "[fsharp][builtin][string]")
{
    CHECK(executeSourceAndGetOutput(R"(
let parts = split "," "a,b,c"
print (length parts)
)") == "3");
}

TEST_CASE("split.direct_call_no_match", "[fsharp][builtin][string]")
{
    CHECK(executeSourceAndGetOutput(R"(
let parts = split ":" "hello"
print (length parts)
)") == "1");
}

// =================================================================================================
// split — pipeline
// =================================================================================================

TEST_CASE("split.pipeline", "[fsharp][pipeline][builtin][string]")
{
    CHECK(executeSourceAndGetOutput(R"(
let parts = "a:b:c" |> split ":"
print (length parts)
)") == "3");
}

TEST_CASE("split.pipeline_chain_with_join", "[fsharp][pipeline][builtin][string]")
{
    CHECK(executeSourceAndGetOutput(R"(
print ("a:b:c" |> split ":" |> join "-")
)") == "a-b-c");
}

TEST_CASE("split.pipeline_then_filter", "[fsharp][pipeline][builtin][string]")
{
    CHECK(executeSourceAndGetOutput(R"(
let r = "a,,b,,c" |> split "," |> filter (fun s -> s != "")
print (length r)
)") == "3");
}

// =================================================================================================
// replace — pipeline
// =================================================================================================

TEST_CASE("replace.pipeline", "[fsharp][pipeline][builtin][string]")
{
    CHECK(executeSourceAndGetOutput(R"(
print ("a,b,c" |> replace "," "-")
)") == "a-b-c");
}

TEST_CASE("replace.pipeline_chain", "[fsharp][pipeline][builtin][string]")
{
    CHECK(executeSourceAndGetOutput(R"(
print ("hello world" |> replace "o" "0" |> replace "l" "1")
)") == "he110 w0r1d");
}

TEST_CASE("replace.pipeline_no_match", "[fsharp][pipeline][builtin][string]")
{
    CHECK(executeSourceAndGetOutput(R"(
print ("hello" |> replace "xyz" "!")
)") == "hello");
}

// =================================================================================================
// trim — pipeline
// =================================================================================================

TEST_CASE("trim.pipeline", "[fsharp][pipeline][builtin][string]")
{
    CHECK(executeSourceAndGetOutput(R"(
print ("  hello  " |> trim)
)") == "hello");
}

TEST_CASE("trim.pipeline_only_whitespace", "[fsharp][pipeline][builtin][string]")
{
    CHECK(executeSourceAndGetOutput(R"(
print ("   " |> trim |> length)
)") == "0");
}

// =================================================================================================
// toLower / toUpper — pipeline
// =================================================================================================

TEST_CASE("toLower.pipeline", "[fsharp][pipeline][builtin][string]")
{
    CHECK(executeSourceAndGetOutput(R"(
print ("HELLO" |> toLower)
)") == "hello");
}

TEST_CASE("toUpper.pipeline", "[fsharp][pipeline][builtin][string]")
{
    CHECK(executeSourceAndGetOutput(R"(
print ("hello" |> toUpper)
)") == "HELLO");
}

TEST_CASE("toLower.pipeline_chain", "[fsharp][pipeline][builtin][string]")
{
    CHECK(executeSourceAndGetOutput(R"(
print ("  HELLO  " |> trim |> toLower)
)") == "hello");
}

// =================================================================================================
// contains / startsWith / endsWith — pipeline
// =================================================================================================

TEST_CASE("contains.pipeline", "[fsharp][pipeline][builtin][string]")
{
    CHECK(executeSourceAndGetOutput(R"(
print ("hello world" |> contains "world")
)") == "true");
}

TEST_CASE("startsWith.pipeline", "[fsharp][pipeline][builtin][string]")
{
    CHECK(executeSourceAndGetOutput(R"(
print ("hello" |> startsWith "hel")
)") == "true");
}

TEST_CASE("endsWith.pipeline", "[fsharp][pipeline][builtin][string]")
{
    CHECK(executeSourceAndGetOutput(R"(
print ("hello" |> endsWith "llo")
)") == "true");
}

// =================================================================================================
// Cons pattern string extraction — verifies that head elements from split/list
// are printed as actual strings, not raw pointer values.
// =================================================================================================

TEST_CASE("split.match_cons_extract", "[fsharp][builtin][string][match]")
{
    CHECK(executeSourceAndGetOutput(R"(
let parts = split "," "hello,world"
match parts with
| head :: _ -> print head
| _ -> print "empty"
)") == "hello");
}

TEST_CASE("split.match_cons_extract_tail", "[fsharp][builtin][string][match]")
{
    CHECK(executeSourceAndGetOutput(R"(
let parts = split "," "a,b,c"
match parts with
| _ :: second :: _ -> print second
| _ -> print "empty"
)") == "b");
}

TEST_CASE("split.map_with_cons_pattern", "[fsharp][builtin][string][match]")
{
    CHECK(executeSourceAndGetOutput(R"(
let parts = split ":" "x:y:z"
let result = map (fun s -> toUpper s) parts
print (join "," result)
)") == "X,Y,Z");
}

// =================================================================================================
// Multi-step pipeline chains combining string builtins
// =================================================================================================

TEST_CASE("string.pipeline_complex_chain", "[fsharp][pipeline][builtin][string]")
{
    CHECK(executeSourceAndGetOutput(R"(
print ("  Hello, World!  " |> trim |> toLower |> replace "," "" |> split " " |> join "_")
)") == "hello_world!");
}
