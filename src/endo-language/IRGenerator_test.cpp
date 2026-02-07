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
