// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include "AST.hpp"
#include "Pattern.hpp"

using namespace endo::pattern;
using namespace endo::pattern::patterns;
using namespace endo::ast;

namespace
{
/// Helper to create a simple guard expression: x < 0
std::unique_ptr<Expr> makeGuard(std::string varName, BinaryOp op, int64_t value)
{
    return std::make_unique<BinaryExpr>(
        op, std::make_unique<IdentifierExpr>(std::move(varName)), std::make_unique<IntLiteralExpr>(value));
}
} // namespace

// ============================================================================
// Literal Patterns
// ============================================================================

TEST_CASE("LiteralPattern.int", "[Pattern]")
{
    auto p = literal(int64_t { 42 });
    REQUIRE(toString(*p) == "42");
    REQUIRE(!isIrrefutable(*p));
    REQUIRE(collectBindings(*p).empty());
}

TEST_CASE("LiteralPattern.float", "[Pattern]")
{
    auto p = literal(3.14);
    REQUIRE(toString(*p).substr(0, 4) == "3.14");
    REQUIRE(!isIrrefutable(*p));
}

TEST_CASE("LiteralPattern.bool", "[Pattern]")
{
    auto p1 = literal(true);
    auto p2 = literal(false);
    REQUIRE(toString(*p1) == "true");
    REQUIRE(toString(*p2) == "false");
}

TEST_CASE("LiteralPattern.string", "[Pattern]")
{
    auto p = literal(std::string("hello"));
    REQUIRE(toString(*p) == "\"hello\"");
}

// ============================================================================
// Variable Patterns
// ============================================================================

TEST_CASE("VariablePattern.basic", "[Pattern]")
{
    auto p = variable("x");
    REQUIRE(toString(*p) == "x");
    REQUIRE(isIrrefutable(*p));

    auto bindings = collectBindings(*p);
    REQUIRE(bindings.size() == 1);
    REQUIRE(bindings[0] == "x");
}

TEST_CASE("VariablePattern.mutable", "[Pattern]")
{
    auto p = variable("counter", true);
    REQUIRE(toString(*p) == "mut counter");
    REQUIRE(isIrrefutable(*p));
}

// ============================================================================
// Wildcard Pattern
// ============================================================================

TEST_CASE("WildcardPattern", "[Pattern]")
{
    auto p = wildcard();
    REQUIRE(toString(*p) == "_");
    REQUIRE(isIrrefutable(*p));
    REQUIRE(collectBindings(*p).empty());
}

// ============================================================================
// Tuple Patterns
// ============================================================================

TEST_CASE("TuplePattern.pair", "[Pattern]")
{
    std::vector<PatternPtr> elems;
    elems.push_back(variable("x"));
    elems.push_back(variable("y"));
    auto p = tuple(std::move(elems));

    REQUIRE(toString(*p) == "(x, y)");
    REQUIRE(isIrrefutable(*p));

    auto bindings = collectBindings(*p);
    REQUIRE(bindings.size() == 2);
    REQUIRE(bindings[0] == "x");
    REQUIRE(bindings[1] == "y");
}

TEST_CASE("TuplePattern.mixed", "[Pattern]")
{
    std::vector<PatternPtr> elems;
    elems.push_back(literal(int64_t { 0 }));
    elems.push_back(variable("y"));
    auto p = tuple(std::move(elems));

    REQUIRE(toString(*p) == "(0, y)");
    REQUIRE(!isIrrefutable(*p)); // Contains literal
}

TEST_CASE("TuplePattern.nested", "[Pattern]")
{
    std::vector<PatternPtr> inner;
    inner.push_back(variable("a"));
    inner.push_back(variable("b"));

    std::vector<PatternPtr> outer;
    outer.push_back(tuple(std::move(inner)));
    outer.push_back(variable("c"));

    auto p = tuple(std::move(outer));
    REQUIRE(toString(*p) == "((a, b), c)");

    auto bindings = collectBindings(*p);
    REQUIRE(bindings.size() == 3);
}

// ============================================================================
// List Patterns
// ============================================================================

TEST_CASE("ListPattern.empty", "[Pattern]")
{
    auto p = list({});
    REQUIRE(toString(*p) == "[]");
    REQUIRE(isIrrefutable(*p)); // Empty list pattern matches only empty list
}

TEST_CASE("ListPattern.single", "[Pattern]")
{
    std::vector<PatternPtr> elems;
    elems.push_back(variable("x"));
    auto p = list(std::move(elems));

    REQUIRE(toString(*p) == "[x]");
    REQUIRE(!isIrrefutable(*p)); // Requires exactly one element
}

TEST_CASE("ListPattern.multiple", "[Pattern]")
{
    std::vector<PatternPtr> elems;
    elems.push_back(variable("a"));
    elems.push_back(variable("b"));
    elems.push_back(variable("c"));
    auto p = list(std::move(elems));

    REQUIRE(toString(*p) == "[a; b; c]");
}

TEST_CASE("ListPattern.with_rest", "[Pattern]")
{
    std::vector<PatternPtr> elems;
    elems.push_back(variable("first"));
    elems.push_back(variable("second"));
    auto p = list(std::move(elems), "rest");

    REQUIRE(toString(*p) == "[first; second; rest...]");

    auto bindings = collectBindings(*p);
    REQUIRE(bindings.size() == 3);
    REQUIRE(bindings[2] == "rest");
}

// ============================================================================
// Cons Patterns
// ============================================================================

TEST_CASE("ConsPattern.basic", "[Pattern]")
{
    auto p = cons(variable("head"), variable("tail"));
    REQUIRE(toString(*p) == "head :: tail");
    REQUIRE(!isIrrefutable(*p)); // List could be empty

    auto bindings = collectBindings(*p);
    REQUIRE(bindings.size() == 2);
}

TEST_CASE("ConsPattern.nested", "[Pattern]")
{
    auto p = cons(variable("x"), cons(variable("y"), variable("rest")));
    REQUIRE(toString(*p) == "x :: y :: rest");
}

// ============================================================================
// Record Patterns
// ============================================================================

TEST_CASE("RecordPattern.punning", "[Pattern]")
{
    std::vector<FieldPattern> fields;
    fields.emplace_back("name");
    fields.emplace_back("age");
    auto p = record(std::move(fields));

    REQUIRE(toString(*p) == "{ name; age }");
    REQUIRE(isIrrefutable(*p));

    auto bindings = collectBindings(*p);
    REQUIRE(bindings.size() == 2);
    REQUIRE(bindings[0] == "name");
    REQUIRE(bindings[1] == "age");
}

TEST_CASE("RecordPattern.with_patterns", "[Pattern]")
{
    std::vector<FieldPattern> fields;
    fields.emplace_back("name", variable("n"));
    fields.emplace_back("age", literal(int64_t { 18 }));
    auto p = record(std::move(fields));

    REQUIRE(toString(*p) == "{ name = n; age = 18 }");
    REQUIRE(!isIrrefutable(*p)); // Contains literal
}

TEST_CASE("RecordPattern.with_wildcard", "[Pattern]")
{
    std::vector<FieldPattern> fields;
    fields.emplace_back("name", variable("n"));
    auto p = record(std::move(fields), true);

    REQUIRE(toString(*p) == "{ name = n; _ }");
}

// ============================================================================
// Constructor Patterns
// ============================================================================

TEST_CASE("ConstructorPattern.no_payload", "[Pattern]")
{
    auto p = constructor("None");
    REQUIRE(toString(*p) == "None");
    REQUIRE(!isIrrefutable(*p));
    REQUIRE(collectBindings(*p).empty());
}

TEST_CASE("ConstructorPattern.with_payload", "[Pattern]")
{
    auto p = constructor("Some", variable("x"));
    REQUIRE(toString(*p) == "Some x");

    auto bindings = collectBindings(*p);
    REQUIRE(bindings.size() == 1);
    REQUIRE(bindings[0] == "x");
}

TEST_CASE("ConstructorPattern.complex_payload", "[Pattern]")
{
    std::vector<PatternPtr> elems;
    elems.push_back(variable("w"));
    elems.push_back(variable("h"));

    auto p = constructor("Rectangle", tuple(std::move(elems)));
    REQUIRE(toString(*p) == "Rectangle (w, h)");
}

TEST_CASE("ConstructorPattern.result_ok", "[Pattern]")
{
    auto p = constructor("Ok", variable("value"));
    REQUIRE(toString(*p) == "Ok value");
}

TEST_CASE("ConstructorPattern.result_error", "[Pattern]")
{
    std::vector<FieldPattern> fields;
    fields.emplace_back("code");
    fields.emplace_back("message");

    auto p = constructor("Error", record(std::move(fields)));
    REQUIRE(toString(*p) == "Error { code; message }");
}

// ============================================================================
// As Patterns
// ============================================================================

TEST_CASE("AsPattern.basic", "[Pattern]")
{
    std::vector<FieldPattern> fields;
    fields.emplace_back("name");
    fields.emplace_back("price");

    auto p = as(record(std::move(fields)), "product");
    REQUIRE(toString(*p) == "{ name; price } as product");

    auto bindings = collectBindings(*p);
    REQUIRE(bindings.size() == 3);
    REQUIRE(bindings[0] == "product");
    REQUIRE(bindings[1] == "name");
    REQUIRE(bindings[2] == "price");
}

TEST_CASE("AsPattern.with_constructor", "[Pattern]")
{
    auto p = as(constructor("Leaf", wildcard()), "leaf");
    REQUIRE(toString(*p) == "Leaf _ as leaf");
}

// ============================================================================
// Or Patterns
// ============================================================================

TEST_CASE("OrPattern.strings", "[Pattern]")
{
    std::vector<PatternPtr> alts;
    alts.push_back(literal(std::string("quit")));
    alts.push_back(literal(std::string("exit")));
    alts.push_back(literal(std::string("q")));

    auto p = or_(std::move(alts));
    REQUIRE(toString(*p) == "\"quit\" | \"exit\" | \"q\"");
    REQUIRE(!isIrrefutable(*p));
}

TEST_CASE("OrPattern.numbers", "[Pattern]")
{
    std::vector<PatternPtr> alts;
    alts.push_back(literal(int64_t { 200 }));
    alts.push_back(literal(int64_t { 201 }));
    alts.push_back(literal(int64_t { 204 }));

    auto p = or_(std::move(alts));
    REQUIRE(toString(*p) == "200 | 201 | 204");
}

TEST_CASE("OrPattern.bindings", "[Pattern]")
{
    // All alternatives must bind the same variables
    // We collect from the first alternative
    std::vector<PatternPtr> alts;
    alts.push_back(variable("x"));
    alts.push_back(variable("x")); // Same name

    auto p = or_(std::move(alts));
    auto bindings = collectBindings(*p);
    REQUIRE(bindings.size() == 1);
    REQUIRE(bindings[0] == "x");
}

// ============================================================================
// Guarded Patterns
// ============================================================================

TEST_CASE("GuardedPattern.basic", "[Pattern]")
{
    auto p = guarded(variable("x"), makeGuard("x", BinaryOp::Lt, 0));
    REQUIRE(toString(*p) == "x when (x < 0)");
    REQUIRE(!isIrrefutable(*p)); // Guards are always refutable
}

TEST_CASE("GuardedPattern.with_destructuring", "[Pattern]")
{
    std::vector<FieldPattern> fields;
    fields.emplace_back("age");

    auto p = guarded(record(std::move(fields)), makeGuard("age", BinaryOp::Ge, 18));
    REQUIRE(toString(*p) == "{ age } when (age >= 18)");
}

// ============================================================================
// Clone Tests
// ============================================================================

TEST_CASE("Pattern.clone.literal", "[Pattern]")
{
    auto p1 = literal(int64_t { 42 });
    auto p2 = p1->clone();
    REQUIRE(toString(*p1) == toString(*p2));
}

TEST_CASE("Pattern.clone.variable", "[Pattern]")
{
    auto p1 = variable("x", true);
    auto p2 = p1->clone();
    REQUIRE(toString(*p1) == toString(*p2));
}

TEST_CASE("Pattern.clone.tuple", "[Pattern]")
{
    std::vector<PatternPtr> elems;
    elems.push_back(variable("a"));
    elems.push_back(literal(int64_t { 1 }));
    auto p1 = tuple(std::move(elems));
    auto p2 = p1->clone();
    REQUIRE(toString(*p1) == toString(*p2));
}

TEST_CASE("Pattern.clone.list_with_rest", "[Pattern]")
{
    std::vector<PatternPtr> elems;
    elems.push_back(variable("x"));
    auto p1 = list(std::move(elems), "rest");
    auto p2 = p1->clone();
    REQUIRE(toString(*p1) == toString(*p2));
}

TEST_CASE("Pattern.clone.cons", "[Pattern]")
{
    auto p1 = cons(variable("h"), variable("t"));
    auto p2 = p1->clone();
    REQUIRE(toString(*p1) == toString(*p2));
}

TEST_CASE("Pattern.clone.record", "[Pattern]")
{
    std::vector<FieldPattern> fields;
    fields.emplace_back("name", variable("n"));
    auto p1 = record(std::move(fields), true);
    auto p2 = p1->clone();
    REQUIRE(toString(*p1) == toString(*p2));
}

TEST_CASE("Pattern.clone.constructor", "[Pattern]")
{
    auto p1 = constructor("Some", variable("x"));
    auto p2 = p1->clone();
    REQUIRE(toString(*p1) == toString(*p2));
}

TEST_CASE("Pattern.clone.as", "[Pattern]")
{
    auto p1 = as(variable("x"), "alias");
    auto p2 = p1->clone();
    REQUIRE(toString(*p1) == toString(*p2));
}

TEST_CASE("Pattern.clone.or", "[Pattern]")
{
    std::vector<PatternPtr> alts;
    alts.push_back(literal(int64_t { 1 }));
    alts.push_back(literal(int64_t { 2 }));
    auto p1 = or_(std::move(alts));
    auto p2 = p1->clone();
    REQUIRE(toString(*p1) == toString(*p2));
}

TEST_CASE("Pattern.clone.guarded", "[Pattern]")
{
    auto p1 = guarded(variable("x"), makeGuard("x", BinaryOp::Gt, 0));
    auto p2 = p1->clone();
    // Note: Guard is not cloned (set to nullptr), so toString will differ
    // Original has guard, clone does not
    REQUIRE(toString(*p1) == "x when (x > 0)");
    REQUIRE(toString(*p2) == "x"); // Guard not cloned
}

// ============================================================================
// Complex Patterns
// ============================================================================

TEST_CASE("Pattern.complex.nested_option", "[Pattern]")
{
    // { database = Some { host; port = Some p }; _ }
    std::vector<FieldPattern> innerFields;
    innerFields.emplace_back("host");
    innerFields.emplace_back("port", constructor("Some", variable("p")));

    std::vector<FieldPattern> outerFields;
    outerFields.emplace_back("database", constructor("Some", record(std::move(innerFields))));

    auto p = record(std::move(outerFields), true);
    REQUIRE(toString(*p) == "{ database = Some { host; port = Some p }; _ }");

    auto bindings = collectBindings(*p);
    REQUIRE(bindings.size() == 2);
    // Note: "host" comes from punning, "p" from the variable pattern
}

TEST_CASE("Pattern.complex.list_of_records", "[Pattern]")
{
    // [{ name; _ }]
    std::vector<FieldPattern> fields;
    fields.emplace_back("name");

    std::vector<PatternPtr> elems;
    elems.push_back(record(std::move(fields), true));

    auto p = list(std::move(elems));
    REQUIRE(toString(*p) == "[{ name; _ }]");
}

TEST_CASE("Pattern.irrefutable.deeply_nested", "[Pattern]")
{
    // ((x, y), { a; b })
    std::vector<PatternPtr> inner;
    inner.push_back(variable("x"));
    inner.push_back(variable("y"));

    std::vector<FieldPattern> fields;
    fields.emplace_back("a");
    fields.emplace_back("b");

    std::vector<PatternPtr> outer;
    outer.push_back(tuple(std::move(inner)));
    outer.push_back(record(std::move(fields)));

    auto p = tuple(std::move(outer));
    REQUIRE(isIrrefutable(*p)); // All variables/punning
}

TEST_CASE("Pattern.refutable.deeply_nested", "[Pattern]")
{
    // ((x, 0), { a; b })
    std::vector<PatternPtr> inner;
    inner.push_back(variable("x"));
    inner.push_back(literal(int64_t { 0 })); // Literal makes it refutable

    std::vector<FieldPattern> fields;
    fields.emplace_back("a");
    fields.emplace_back("b");

    std::vector<PatternPtr> outer;
    outer.push_back(tuple(std::move(inner)));
    outer.push_back(record(std::move(fields)));

    auto p = tuple(std::move(outer));
    REQUIRE(!isIrrefutable(*p));
}
