// SPDX-License-Identifier: Apache-2.0
#include <endo-language/types/Type.hpp>
#include <endo-language/types/TypeEnv.hpp>
#include <endo-language/types/Unification.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace endo;

// ============================================================================
// Type Creation Tests
// ============================================================================

TEST_CASE("Type.primitives")
{
    auto intT = types::intType();
    auto floatT = types::floatType();
    auto strT = types::strType();
    auto boolT = types::boolType();
    auto unitT = types::unitType();

    CHECK(intT->isPrimitive());
    CHECK(floatT->isPrimitive());
    CHECK(strT->isPrimitive());
    CHECK(boolT->isPrimitive());
    CHECK(unitT->isPrimitive());

    CHECK(intT->asPrimitive()->kind == PrimitiveType::Int);
    CHECK(floatT->asPrimitive()->kind == PrimitiveType::Float);
    CHECK(strT->asPrimitive()->kind == PrimitiveType::Str);
    CHECK(boolT->asPrimitive()->kind == PrimitiveType::Bool);
    CHECK(unitT->asPrimitive()->kind == PrimitiveType::Unit);
}

TEST_CASE("Type.primitives_singleton")
{
    // Primitive types should be singletons
    CHECK(types::intType() == types::intType());
    CHECK(types::floatType() == types::floatType());
    CHECK(types::strType() == types::strType());
    CHECK(types::boolType() == types::boolType());
    CHECK(types::unitType() == types::unitType());
}

TEST_CASE("Type.type_variable")
{
    auto a = types::typeVar(0);
    auto b = types::typeVar(1);

    CHECK(a->isTypeVar());
    CHECK(b->isTypeVar());
    CHECK(a->asTypeVar()->id == 0);
    CHECK(b->asTypeVar()->id == 1);
}

TEST_CASE("Type.function")
{
    // int -> str
    auto fn = types::function(types::intType(), types::strType());

    CHECK(fn->isFunction());
    CHECK(*fn->asFunction()->paramType == *types::intType());
    CHECK(*fn->asFunction()->returnType == *types::strType());
}

TEST_CASE("Type.curried_function")
{
    // int -> int -> int
    auto add = types::function({ types::intType(), types::intType() }, types::intType());

    CHECK(add->isFunction());
    auto* fn1 = add->asFunction();
    CHECK(*fn1->paramType == *types::intType());
    CHECK(fn1->returnType->isFunction());
    auto* fn2 = fn1->returnType->asFunction();
    CHECK(*fn2->paramType == *types::intType());
    CHECK(*fn2->returnType == *types::intType());
}

TEST_CASE("Type.list")
{
    auto intList = types::list(types::intType());

    CHECK(intList->isList());
    CHECK(*intList->asList()->elementType == *types::intType());
}

TEST_CASE("Type.tuple")
{
    auto pair = types::tuple({ types::intType(), types::strType() });

    CHECK(pair->isTuple());
    CHECK(pair->asTuple()->elementTypes.size() == 2);
    CHECK(*pair->asTuple()->elementTypes[0] == *types::intType());
    CHECK(*pair->asTuple()->elementTypes[1] == *types::strType());
}

TEST_CASE("Type.option")
{
    auto optInt = types::option(types::intType());

    CHECK(optInt->isOption());
    CHECK(*optInt->asOption()->innerType == *types::intType());
}

TEST_CASE("Type.result")
{
    auto res = types::result(types::intType(), types::strType());

    CHECK(res->isResult());
    CHECK(*res->asResult()->okType == *types::intType());
    CHECK(*res->asResult()->errorType == *types::strType());
}

TEST_CASE("Type.record")
{
    auto person = types::record("Person", { { "name", types::strType() }, { "age", types::intType() } });

    CHECK(person->isRecord());
    CHECK(person->asRecord()->name == "Person");
    CHECK(person->asRecord()->fields.size() == 2);
    CHECK(person->asRecord()->fields[0].name == "name");
    CHECK(*person->asRecord()->fields[0].type == *types::strType());
    CHECK(person->asRecord()->fields[1].name == "age");
    CHECK(*person->asRecord()->fields[1].type == *types::intType());
}

TEST_CASE("Type.record_field_lookup")
{
    auto person = types::record("Person", { { "name", types::strType() }, { "age", types::intType() } });

    auto nameType = person->asRecord()->fieldType("name");
    CHECK(nameType.has_value());
    CHECK(**nameType == *types::strType());

    auto ageType = person->asRecord()->fieldType("age");
    CHECK(ageType.has_value());
    CHECK(**ageType == *types::intType());

    auto unknown = person->asRecord()->fieldType("unknown");
    CHECK(!unknown.has_value());
}

TEST_CASE("Type.union")
{
    auto shape = types::unionType("Shape",
                                  { { "Circle", types::floatType() },
                                    { "Rectangle", types::tuple({ types::floatType(), types::floatType() }) },
                                    { "Point", std::nullopt } });

    CHECK(shape->isUnion());
    CHECK(shape->asUnion()->name == "Shape");
    CHECK(shape->asUnion()->cases.size() == 3);

    CHECK(shape->asUnion()->cases[0].name == "Circle");
    CHECK(shape->asUnion()->cases[0].payloadType.has_value());

    CHECK(shape->asUnion()->cases[1].name == "Rectangle");
    CHECK(shape->asUnion()->cases[1].payloadType.has_value());

    CHECK(shape->asUnion()->cases[2].name == "Point");
    CHECK(!shape->asUnion()->cases[2].payloadType.has_value());
}

// ============================================================================
// Type ToString Tests
// ============================================================================

TEST_CASE("Type.toString.primitives")
{
    CHECK(toString(types::intType()) == "int");
    CHECK(toString(types::floatType()) == "float");
    CHECK(toString(types::strType()) == "string");
    CHECK(toString(types::boolType()) == "bool");
    CHECK(toString(types::unitType()) == "unit");
}

TEST_CASE("Type.toString.type_variable")
{
    CHECK(toString(types::typeVar(0)) == "a");
    CHECK(toString(types::typeVar(1)) == "b");
    CHECK(toString(types::typeVar(25)) == "z");
    CHECK(toString(types::typeVar(26)) == "a1");
    CHECK(toString(types::typeVar(27)) == "b1");
}

TEST_CASE("Type.toString.function")
{
    auto fn = types::function(types::intType(), types::strType());
    CHECK(toString(fn) == "int -> string");

    auto curried = types::function({ types::intType(), types::strType() }, types::boolType());
    CHECK(toString(curried) == "int -> string -> bool");
}

TEST_CASE("Type.toString.list")
{
    CHECK(toString(types::list(types::intType())) == "list<int>");
}

TEST_CASE("Type.toString.tuple")
{
    auto pair = types::tuple({ types::intType(), types::strType() });
    CHECK(toString(pair) == "(int, string)");
}

TEST_CASE("Type.toString.option")
{
    CHECK(toString(types::option(types::intType())) == "option<int>");
}

TEST_CASE("Type.toString.result")
{
    CHECK(toString(types::result(types::intType(), types::strType())) == "result<int, string>");
}

// ============================================================================
// TypeScheme Tests
// ============================================================================

TEST_CASE("TypeScheme.monomorphic")
{
    auto scheme = types::monomorphic(types::intType());
    CHECK(scheme.quantifiedVars.empty());
    CHECK(*scheme.type == *types::intType());
}

TEST_CASE("TypeScheme.polymorphic")
{
    // forall a. a -> a
    auto a = TypeVarId { 0 };
    auto idType = types::function(types::typeVar(a), types::typeVar(a));
    auto scheme = types::scheme({ a }, idType);

    CHECK(scheme.quantifiedVars.size() == 1);
    CHECK(scheme.quantifiedVars[0] == 0);
}

TEST_CASE("TypeScheme.instantiate")
{
    // forall a. a -> a
    auto a = TypeVarId { 0 };
    auto idType = types::function(types::typeVar(a), types::typeVar(a));
    auto scheme = types::scheme({ a }, idType);

    TypeVarId nextId = 100;
    auto instantiated = scheme.instantiate([&nextId]() { return nextId++; });

    // Should have fresh type variable
    CHECK(instantiated->isFunction());
    auto* fn = instantiated->asFunction();
    CHECK(fn->paramType->isTypeVar());
    CHECK(fn->returnType->isTypeVar());
    CHECK(fn->paramType->asTypeVar()->id == 100);
    CHECK(fn->returnType->asTypeVar()->id == 100);
}

TEST_CASE("TypeScheme.toString")
{
    auto a = TypeVarId { 0 };
    auto b = TypeVarId { 1 };
    auto fnType = types::function(types::typeVar(a), types::typeVar(b));
    auto scheme = types::scheme({ a, b }, fnType);

    CHECK(toString(scheme) == "forall a b. a -> b");
}

// ============================================================================
// Unification Tests
// ============================================================================

TEST_CASE("Unification.identical_primitives")
{
    auto result = unify(types::intType(), types::intType());
    REQUIRE(result.has_value());
    CHECK(result->empty());
}

TEST_CASE("Unification.different_primitives")
{
    auto result = unify(types::intType(), types::strType());
    CHECK(!result.has_value());
    CHECK(result.error().kind == TypeError::Kind::Mismatch);
}

TEST_CASE("Unification.type_variable_left")
{
    auto a = types::typeVar(0);
    auto result = unify(a, types::intType());
    REQUIRE(result.has_value());
    CHECK(result->contains(0));
    CHECK(*result->lookup(0).value() == *types::intType());
}

TEST_CASE("Unification.type_variable_right")
{
    auto a = types::typeVar(0);
    auto result = unify(types::intType(), a);
    REQUIRE(result.has_value());
    CHECK(result->contains(0));
    CHECK(*result->lookup(0).value() == *types::intType());
}

TEST_CASE("Unification.type_variable_both")
{
    auto a = types::typeVar(0);
    auto b = types::typeVar(1);
    auto result = unify(a, b);
    REQUIRE(result.has_value());
    // One should map to the other
    CHECK((result->contains(0) || result->contains(1)));
}

TEST_CASE("Unification.occurs_check")
{
    // a cannot unify with list<a>
    auto a = types::typeVar(0);
    auto listA = types::list(a);
    auto result = unify(a, listA);
    CHECK(!result.has_value());
    CHECK(result.error().kind == TypeError::Kind::OccursCheck);
}

TEST_CASE("Unification.function")
{
    // (a -> b) unifies with (int -> str)
    auto a = types::typeVar(0);
    auto b = types::typeVar(1);
    auto fnVar = types::function(a, b);
    auto fnConcrete = types::function(types::intType(), types::strType());

    auto result = unify(fnVar, fnConcrete);
    REQUIRE(result.has_value());
    CHECK(*result->apply(a) == *types::intType());
    CHECK(*result->apply(b) == *types::strType());
}

TEST_CASE("Unification.list")
{
    // list<a> unifies with list<int>
    auto a = types::typeVar(0);
    auto listA = types::list(a);
    auto listInt = types::list(types::intType());

    auto result = unify(listA, listInt);
    REQUIRE(result.has_value());
    CHECK(*result->apply(a) == *types::intType());
}

TEST_CASE("Unification.tuple")
{
    // (a, b) unifies with (int, str)
    auto a = types::typeVar(0);
    auto b = types::typeVar(1);
    auto tupleVar = types::tuple({ a, b });
    auto tupleConcrete = types::tuple({ types::intType(), types::strType() });

    auto result = unify(tupleVar, tupleConcrete);
    REQUIRE(result.has_value());
    CHECK(*result->apply(a) == *types::intType());
    CHECK(*result->apply(b) == *types::strType());
}

TEST_CASE("Unification.tuple_arity_mismatch")
{
    auto pair = types::tuple({ types::intType(), types::strType() });
    auto triple = types::tuple({ types::intType(), types::strType(), types::boolType() });

    auto result = unify(pair, triple);
    CHECK(!result.has_value());
    CHECK(result.error().kind == TypeError::Kind::ArityMismatch);
}

TEST_CASE("Unification.option")
{
    auto a = types::typeVar(0);
    auto optA = types::option(a);
    auto optInt = types::option(types::intType());

    auto result = unify(optA, optInt);
    REQUIRE(result.has_value());
    CHECK(*result->apply(a) == *types::intType());
}

TEST_CASE("Unification.result")
{
    auto a = types::typeVar(0);
    auto e = types::typeVar(1);
    auto resVar = types::result(a, e);
    auto resConcrete = types::result(types::intType(), types::strType());

    auto result = unify(resVar, resConcrete);
    REQUIRE(result.has_value());
    CHECK(*result->apply(a) == *types::intType());
    CHECK(*result->apply(e) == *types::strType());
}

// ============================================================================
// Substitution Tests
// ============================================================================

TEST_CASE("Substitution.apply_simple")
{
    auto subst = Substitution::single(0, types::intType());
    auto a = types::typeVar(0);

    auto result = subst.apply(a);
    CHECK(*result == *types::intType());
}

TEST_CASE("Substitution.apply_function")
{
    auto subst = Substitution::single(0, types::intType());
    subst.add(1, types::strType());

    auto fnType = types::function(types::typeVar(0), types::typeVar(1));
    auto result = subst.apply(fnType);

    CHECK(result->isFunction());
    CHECK(*result->asFunction()->paramType == *types::intType());
    CHECK(*result->asFunction()->returnType == *types::strType());
}

TEST_CASE("Substitution.apply_nested")
{
    // {a -> list<b>, b -> int}
    auto subst = Substitution::single(0, types::list(types::typeVar(1)));
    subst.add(1, types::intType());

    auto a = types::typeVar(0);
    auto result = subst.apply(a);

    // Should get list<int>
    CHECK(result->isList());
    CHECK(*result->asList()->elementType == *types::intType());
}

TEST_CASE("Substitution.compose")
{
    // s1 = {b -> int}
    auto s1 = Substitution::single(1, types::intType());

    // s2 = {a -> b}
    auto s2 = Substitution::single(0, types::typeVar(1));

    // s1 compose s2 should give {a -> int, b -> int}
    auto composed = s1.compose(s2);

    auto a = types::typeVar(0);
    auto b = types::typeVar(1);

    CHECK(*composed.apply(a) == *types::intType());
    CHECK(*composed.apply(b) == *types::intType());
}

// ============================================================================
// TypeEnv Tests
// ============================================================================

TEST_CASE("TypeEnv.bind_and_lookup")
{
    auto env = std::make_shared<TypeEnv>();

    env->bindMono("x", types::intType());

    auto result = env->lookup("x");
    REQUIRE(result.has_value());
    CHECK(*result->type == *types::intType());
}

TEST_CASE("TypeEnv.lookup_not_found")
{
    auto env = std::make_shared<TypeEnv>();
    auto result = env->lookup("undefined");
    CHECK(!result.has_value());
}

TEST_CASE("TypeEnv.child_scope")
{
    auto parent = std::make_shared<TypeEnv>();
    parent->bindMono("x", types::intType());

    auto child = std::make_shared<TypeEnv>(parent);
    child->bindMono("y", types::strType());

    // Child can see parent's bindings
    auto xResult = child->lookup("x");
    REQUIRE(xResult.has_value());
    CHECK(*xResult->type == *types::intType());

    // Child can see its own bindings
    auto yResult = child->lookup("y");
    REQUIRE(yResult.has_value());
    CHECK(*yResult->type == *types::strType());

    // Parent cannot see child's bindings
    CHECK(!parent->lookup("y").has_value());
}

TEST_CASE("TypeEnv.shadowing")
{
    auto parent = std::make_shared<TypeEnv>();
    parent->bindMono("x", types::intType());

    auto child = std::make_shared<TypeEnv>(parent);
    child->bindMono("x", types::strType()); // Shadow parent's x

    // Child sees its own binding
    auto result = child->lookup("x");
    REQUIRE(result.has_value());
    CHECK(*result->type == *types::strType());

    // Parent still has the original
    auto parentResult = parent->lookup("x");
    REQUIRE(parentResult.has_value());
    CHECK(*parentResult->type == *types::intType());
}

TEST_CASE("TypeEnv.fresh_type_var")
{
    auto env = std::make_shared<TypeEnv>();

    auto v1 = env->freshTypeVar();
    auto v2 = env->freshTypeVar();
    auto v3 = env->freshTypeVar();

    CHECK(v1 == 0);
    CHECK(v2 == 1);
    CHECK(v3 == 2);
}

TEST_CASE("TypeEnv.generalize_simple")
{
    auto env = std::make_shared<TypeEnv>();

    // No bindings in env, so all type vars in type should be quantified
    auto a = types::typeVar(0);
    auto scheme = env->generalize(a);

    CHECK(scheme.quantifiedVars.size() == 1);
    CHECK(scheme.quantifiedVars[0] == 0);
}

TEST_CASE("TypeEnv.generalize_with_env_binding")
{
    auto env = std::make_shared<TypeEnv>();

    // Bind 'x' to type variable 'a'
    auto a = types::typeVar(0);
    env->bindMono("x", a);

    // Now generalize 'a -> b'
    auto b = types::typeVar(1);
    auto fnType = types::function(a, b);
    auto scheme = env->generalize(fnType);

    // Only 'b' should be quantified (a is free in env)
    CHECK(scheme.quantifiedVars.size() == 1);
    CHECK(scheme.quantifiedVars[0] == 1);
}

TEST_CASE("TypeEnv.instantiate")
{
    auto env = std::make_shared<TypeEnv>();

    // forall a. a -> a
    auto scheme = types::scheme({ 0 }, types::function(types::typeVar(0), types::typeVar(0)));

    auto instantiated = env->instantiate(scheme);

    CHECK(instantiated->isFunction());
    auto* fn = instantiated->asFunction();
    CHECK(fn->paramType->isTypeVar());
    CHECK(fn->returnType->isTypeVar());
    // Both should have the same fresh type variable
    CHECK(fn->paramType->asTypeVar()->id == fn->returnType->asTypeVar()->id);
}

// ============================================================================
// Standard Environment Tests
// ============================================================================

TEST_CASE("TypeEnv.standard_env_has_operators")
{
    auto env = createStandardTypeEnv();

    CHECK(env->isDefined("+"));
    CHECK(env->isDefined("-"));
    CHECK(env->isDefined("*"));
    CHECK(env->isDefined("/"));
    CHECK(env->isDefined("=="));
    CHECK(env->isDefined("&&"));
    CHECK(env->isDefined("||"));
}

TEST_CASE("TypeEnv.standard_env_has_list_functions")
{
    auto env = createStandardTypeEnv();

    CHECK(env->isDefined("head"));
    CHECK(env->isDefined("tail"));
    CHECK(env->isDefined("length"));
    CHECK(env->isDefined("map"));
    CHECK(env->isDefined("filter"));
    CHECK(env->isDefined("fold"));
}

TEST_CASE("TypeEnv.standard_env_has_option_result")
{
    auto env = createStandardTypeEnv();

    CHECK(env->isDefined("Some"));
    CHECK(env->isDefined("None"));
    CHECK(env->isDefined("Ok"));
    CHECK(env->isDefined("Error"));
    CHECK(env->isDefined("isOk"));
    CHECK(env->isDefined("isNone"));
}

// ============================================================================
// TypeRegistry Tests
// ============================================================================

TEST_CASE("TypeRegistry.register_and_lookup")
{
    TypeRegistry registry;

    registry.registerRecord(
        "Point", { .name = "Point", .fields = { { "x", types::floatType() }, { "y", types::floatType() } } });

    auto result = registry.lookupRecord("Point");
    REQUIRE(result.has_value());
    CHECK(result->name == "Point");
    CHECK(result->fields.size() == 2);
}

TEST_CASE("TypeRegistry.builtins")
{
    auto registry = createStandardTypeRegistry();

    auto errorType = registry->lookupRecord("Error");
    REQUIRE(errorType.has_value());
    CHECK(errorType->name == "Error");
    CHECK(errorType->fields.size() == 2);

    auto codeField = errorType->fieldType("code");
    REQUIRE(codeField.has_value());
    CHECK(**codeField == *types::intType());

    auto msgField = errorType->fieldType("message");
    REQUIRE(msgField.has_value());
    CHECK(**msgField == *types::strType());
}
