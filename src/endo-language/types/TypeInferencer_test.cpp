// SPDX-License-Identifier: Apache-2.0
#include <endo-language/TestHelper.hpp>
#include <endo-language/types/Type.hpp>
#include <endo-language/types/TypeEnv.hpp>
#include <endo-language/types/TypeInferencer.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace endo;
using namespace endo::test;

// ============================================================================
// Unit Tests: TypeInferencer in isolation
// ============================================================================

namespace
{

/// Helper: runs inference on source and returns the InferenceResult.
InferenceResult inferTypes(std::string const& source)
{
    auto ast = parse(source);
    REQUIRE(ast != nullptr);
    auto env = createStandardTypeEnv();
    TypeInferencer inferencer(env);
    return inferencer.inferProgram(*ast);
}

/// Helper: checks that a function was inferred and returns its type.
InferredFunctionType const& getInferredFunction(InferenceResult const& result, std::string const& name)
{
    auto it = result.functions.find(name);
    REQUIRE(it != result.functions.end());
    return it->second;
}

} // namespace

TEST_CASE("TypeInferencer.literal_int", "[TypeInferencer]")
{
    auto result = inferTypes("let f x = x + 1; print (f 5)");
    REQUIRE_FALSE(result.hasErrors());
    auto const& f = getInferredFunction(result, "f");
    REQUIRE(f.paramTypes.size() == 1);
    CHECK(f.paramTypes[0]->isPrimitive());
    CHECK(f.paramTypes[0]->asPrimitive()->kind == PrimitiveType::Int);
    REQUIRE(f.returnType.has_value());
    CHECK((*f.returnType)->asPrimitive()->kind == PrimitiveType::Int);
}

TEST_CASE("TypeInferencer.literal_bool", "[TypeInferencer]")
{
    auto result = inferTypes("let negate x = !x");
    REQUIRE_FALSE(result.hasErrors());
    auto const& f = getInferredFunction(result, "negate");
    REQUIRE(f.paramTypes.size() == 1);
    CHECK(f.paramTypes[0]->asPrimitive()->kind == PrimitiveType::Bool);
}

TEST_CASE("TypeInferencer.binary_arithmetic", "[TypeInferencer]")
{
    auto result = inferTypes("let add x y = x + y");
    REQUIRE_FALSE(result.hasErrors());
    auto const& f = getInferredFunction(result, "add");
    REQUIRE(f.paramTypes.size() == 2);
    CHECK(f.paramTypes[0]->asPrimitive()->kind == PrimitiveType::Int);
    CHECK(f.paramTypes[1]->asPrimitive()->kind == PrimitiveType::Int);
}

TEST_CASE("TypeInferencer.comparison", "[TypeInferencer]")
{
    auto result = inferTypes("let isPositive x = x > 0");
    REQUIRE_FALSE(result.hasErrors());
    auto const& f = getInferredFunction(result, "isPositive");
    REQUIRE(f.paramTypes.size() == 1);
    CHECK(f.paramTypes[0]->asPrimitive()->kind == PrimitiveType::Int);
    REQUIRE(f.returnType.has_value());
    CHECK((*f.returnType)->asPrimitive()->kind == PrimitiveType::Bool);
}

TEST_CASE("TypeInferencer.recursive_function", "[TypeInferencer]")
{
    auto result = inferTypes("let rec fact n = if n <= 1 then 1 else n * fact (n - 1)");
    REQUIRE_FALSE(result.hasErrors());
    auto const& f = getInferredFunction(result, "fact");
    REQUIRE(f.paramTypes.size() == 1);
    CHECK(f.paramTypes[0]->asPrimitive()->kind == PrimitiveType::Int);
    REQUIRE(f.returnType.has_value());
    CHECK((*f.returnType)->asPrimitive()->kind == PrimitiveType::Int);
}

TEST_CASE("TypeInferencer.partial_annotations", "[TypeInferencer]")
{
    // x is annotated, y is not — should infer y from usage
    auto result = inferTypes("let add (x: int) y = x + y");
    REQUIRE_FALSE(result.hasErrors());
    auto const& f = getInferredFunction(result, "add");
    REQUIRE(f.paramTypes.size() == 2);
    CHECK(f.paramTypes[0]->asPrimitive()->kind == PrimitiveType::Int);
    CHECK(f.paramTypes[1]->asPrimitive()->kind == PrimitiveType::Int);
}

TEST_CASE("TypeInferencer.lambda_as_variable", "[TypeInferencer]")
{
    auto result = inferTypes("let double = fun x -> x * 2");
    REQUIRE_FALSE(result.hasErrors());
    auto const& f = getInferredFunction(result, "double");
    REQUIRE(f.paramTypes.size() == 1);
    CHECK(f.paramTypes[0]->asPrimitive()->kind == PrimitiveType::Int);
}

TEST_CASE("TypeInferencer.option_type", "[TypeInferencer]")
{
    auto result = inferTypes("let wrap x = Some x");
    REQUIRE_FALSE(result.hasErrors());
    auto const& f = getInferredFunction(result, "wrap");
    REQUIRE(f.returnType.has_value());
    CHECK((*f.returnType)->isOption());
}

TEST_CASE("TypeInferencer.if_branches_unified", "[TypeInferencer]")
{
    auto result = inferTypes("let abs x = if x < 0 then 0 - x else x");
    REQUIRE_FALSE(result.hasErrors());
    auto const& f = getInferredFunction(result, "abs");
    REQUIRE(f.paramTypes.size() == 1);
    CHECK(f.paramTypes[0]->asPrimitive()->kind == PrimitiveType::Int);
    REQUIRE(f.returnType.has_value());
    CHECK((*f.returnType)->asPrimitive()->kind == PrimitiveType::Int);
}

// ============================================================================
// End-to-End Tests: Parse → Infer → Compile → Execute
// ============================================================================

TEST_CASE("TypeInferencer.e2e.untyped_add", "[TypeInferencer][e2e]")
{
    CHECK(executeSourceAndGetOutput("let add x y = x + y; print (add 3 4)") == "7");
}

TEST_CASE("TypeInferencer.e2e.untyped_factorial", "[TypeInferencer][e2e]")
{
    CHECK(executeSourceAndGetOutput("let rec fact n = if n <= 1 then 1 else n * fact (n - 1); print (fact 5)")
          == "120");
}

TEST_CASE("TypeInferencer.e2e.untyped_identity", "[TypeInferencer][e2e]")
{
    CHECK(executeSourceAndGetOutput("let id x = x; print (id 42)") == "42");
}

// NOTE: Higher-order function e2e test (let apply f x = f x) requires function-type parameters
// to be compiled as handlers, which is not yet supported. The inferencer correctly infers
// the types, but the compilation pipeline only applies primitive types currently.

TEST_CASE("TypeInferencer.e2e.untyped_subtraction", "[TypeInferencer][e2e]")
{
    CHECK(executeSourceAndGetOutput("let sub x y = x - y; print (sub 10 3)") == "7");
}

TEST_CASE("TypeInferencer.e2e.untyped_multiply", "[TypeInferencer][e2e]")
{
    CHECK(executeSourceAndGetOutput("let mul x y = x * y; print (mul 6 7)") == "42");
}

TEST_CASE("TypeInferencer.e2e.untyped_comparison", "[TypeInferencer][e2e]")
{
    CHECK(executeSourceAndGetOutput("let isZero x = x == 0; print (isZero 0)") == "true");
}

TEST_CASE("TypeInferencer.e2e.untyped_with_let_in", "[TypeInferencer][e2e]")
{
    CHECK(executeSourceAndGetOutput("let f x = let y = x + 1 in y * 2; print (f 3)") == "8");
}

TEST_CASE("TypeInferencer.e2e.untyped_bool_function", "[TypeInferencer][e2e]")
{
    CHECK(executeSourceAndGetOutput("let both a b = a && b; print (both true false)") == "false");
}

TEST_CASE("TypeInferencer.e2e.lambda_inferred", "[TypeInferencer][e2e]")
{
    CHECK(executeSourceAndGetOutput("let double = fun x -> x * 2; print (double 5)") == "10");
}

TEST_CASE("TypeInferencer.e2e.typed_still_works", "[TypeInferencer][e2e]")
{
    // Ensure explicitly typed functions still compile correctly
    CHECK(executeSourceAndGetOutput("let add (x: int) (y: int) = x + y; print (add 3 4)") == "7");
}

TEST_CASE("TypeInferencer.e2e.mixed_annotations", "[TypeInferencer][e2e]")
{
    // One param annotated, one inferred
    CHECK(executeSourceAndGetOutput("let add (x: int) y = x + y; print (add 3 4)") == "7");
}

// ============================================================================
// Record and Union Type Tests
// ============================================================================

TEST_CASE("TypeInferencer.record_type_def", "[TypeInferencer]")
{
    auto result = inferTypes("type Person = { name: string; age: int }");
    REQUIRE_FALSE(result.hasErrors());
    // The type should be registered as a binding
    // Type defs don't produce bindings in the result — they modify the env.
    // But we can verify by using it in a let binding.
}

TEST_CASE("TypeInferencer.record_literal_type", "[TypeInferencer]")
{
    auto result =
        inferTypes("type Person = { name: string; age: int }\nlet p = { name = \"Alice\"; age = 30 }");
    REQUIRE_FALSE(result.hasErrors());
    auto it = result.bindings.find("p");
    REQUIRE(it != result.bindings.end());
    CHECK(it->second->isRecord());
    auto const* rec = it->second->asRecord();
    REQUIRE(rec != nullptr);
    CHECK(rec->name == "Person");
    REQUIRE(rec->fields.size() == 2);
    CHECK(rec->fields[0].name == "name");
    CHECK(rec->fields[0].type->asPrimitive()->kind == PrimitiveType::Str);
    CHECK(rec->fields[1].name == "age");
    CHECK(rec->fields[1].type->asPrimitive()->kind == PrimitiveType::Int);
}

TEST_CASE("TypeInferencer.record_field_access", "[TypeInferencer]")
{
    // Field access on a record expression should resolve the field type
    auto result = inferTypes("type Person = { name: string; age: int }\n"
                             "let p = { name = \"Alice\"; age = 30 }\n"
                             "let a = p.age");
    REQUIRE_FALSE(result.hasErrors());
    auto it = result.bindings.find("a");
    REQUIRE(it != result.bindings.end());
    CHECK(it->second->asPrimitive()->kind == PrimitiveType::Int);
}

TEST_CASE("TypeInferencer.record_update_type", "[TypeInferencer]")
{
    auto result = inferTypes("type Person = { name: string; age: int }\n"
                             "let p = { name = \"Alice\"; age = 30 }\n"
                             "let q = { p with age = 31 }");
    REQUIRE_FALSE(result.hasErrors());
    auto it = result.bindings.find("q");
    REQUIRE(it != result.bindings.end());
    CHECK(it->second->isRecord());
    auto const* rec = it->second->asRecord();
    REQUIRE(rec != nullptr);
    CHECK(rec->name == "Person");
}

TEST_CASE("TypeInferencer.union_type_def_and_constructor", "[TypeInferencer]")
{
    auto result = inferTypes("type Shape = | Circle of float | Point");
    REQUIRE_FALSE(result.hasErrors());
}
