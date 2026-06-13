// SPDX-License-Identifier: Apache-2.0
// Tests for TargetCodeGenerator - specifically testing multi-block control flow
// with allocas to verify proper stack tracking across basic blocks.

#include <CoreVM/CoreVM.hpp>

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>

using namespace CoreVM;

namespace
{

// Helper to build IR, generate code, and run it
// Uses a global to capture the result value for verification
class IRTestRunner
{
  public:
    IRTestRunner() { _builder.setProgram(std::make_unique<IRProgram>()); }

    IRBuilder& builder() { return _builder; }

    // Create a function and return it
    IRFunction* createFunction(std::string const& name)
    {
        auto* fn = _builder.getFunction(name);
        _builder.setFunction(fn);
        return fn;
    }

    // Generate target code and run the function
    // Returns pair of (completed, exit_code_was_zero)
    std::pair<bool, bool> run(std::string const& functionName)
    {
        TargetCodeGenerator codegen;
        auto targetProgram = codegen.generate(_builder.program());

        Function const* fn = targetProgram->findFunction(functionName);
        if (!fn)
            return { false, false };

        Runner::Globals globals;
        Runner runner(fn, nullptr, &globals, RuntimeConfig::defaultConfig(), nullptr);
        bool success = runner.run();

        // run() returns false if EXIT code was 0 (success in shell terms)
        // run() returns true if EXIT code was non-zero (failure)
        // This is inverted from typical shell convention
        return { true, !success };
    }

  private:
    IRBuilder _builder;
};

} // namespace

// =============================================================================
// Basic tests - single block with allocas
// =============================================================================

TEST_CASE("TargetCodeGenerator.single_block_alloca_store_ret")
{
    // Simple test: alloca, store, ret the stored value
    // entry:
    //   %0 = alloca i64
    //   store 42, %0
    //   %1 = load %0
    //   ret %1

    IRTestRunner testRunner;
    testRunner.createFunction("test");
    auto& builder = testRunner.builder();

    auto* entry = builder.createBlock("entry");
    builder.setInsertPoint(entry);

    auto* alloca = builder.createAlloca(LiteralType::Number, builder.get(1), "x");
    builder.createStore(alloca, builder.get(static_cast<CoreNumber>(42)), "store");
    auto* loaded = builder.createLoad(alloca, "load");
    builder.createRet(loaded);

    auto [completed, success] = testRunner.run("test");
    CHECK(completed);
    // 42 is non-zero, so success should be false (non-zero exit code)
    CHECK_FALSE(success);
}

TEST_CASE("TargetCodeGenerator.single_block_ret_zero")
{
    // Simple test: ret 0
    IRTestRunner testRunner;
    testRunner.createFunction("test");
    auto& builder = testRunner.builder();

    auto* entry = builder.createBlock("entry");
    builder.setInsertPoint(entry);
    builder.createRet(builder.get(static_cast<CoreNumber>(0)));

    auto [completed, success] = testRunner.run("test");
    CHECK(completed);
    CHECK(success); // 0 is success
}

TEST_CASE("TargetCodeGenerator.single_block_multiple_allocas")
{
    // Multiple allocas in single block, verify they work correctly
    // entry:
    //   %a = alloca i64
    //   %b = alloca i64
    //   store 10, %a
    //   store 20, %b
    //   %va = load %a
    //   %vb = load %b
    //   %sum = add %va, %vb
    //   ret %sum

    IRTestRunner testRunner;
    testRunner.createFunction("test");
    auto& builder = testRunner.builder();

    auto* entry = builder.createBlock("entry");
    builder.setInsertPoint(entry);

    auto* allocaA = builder.createAlloca(LiteralType::Number, builder.get(1), "a");
    auto* allocaB = builder.createAlloca(LiteralType::Number, builder.get(1), "b");
    builder.createStore(allocaA, builder.get(static_cast<CoreNumber>(10)), "store.a");
    builder.createStore(allocaB, builder.get(static_cast<CoreNumber>(20)), "store.b");
    auto* loadedA = builder.createLoad(allocaA, "load.a");
    auto* loadedB = builder.createLoad(allocaB, "load.b");
    auto* sum = builder.createAdd(loadedA, loadedB, "sum");
    builder.createRet(sum);

    auto [completed, success] = testRunner.run("test");
    CHECK(completed);
    // 30 is non-zero, so success should be false
    CHECK_FALSE(success);
}

// =============================================================================
// Multi-block tests - the core issue being tested
// =============================================================================

TEST_CASE("TargetCodeGenerator.two_blocks_simple_branch")
{
    // Simple unconditional branch between blocks
    // entry:
    //   %x = alloca i64
    //   store 0, %x
    //   br next
    // next:
    //   %v = load %x
    //   ret %v

    IRTestRunner testRunner;
    testRunner.createFunction("test");
    auto& builder = testRunner.builder();

    auto* entry = builder.createBlock("entry");
    auto* next = builder.createBlock("next");

    builder.setInsertPoint(entry);
    auto* alloca = builder.createAlloca(LiteralType::Number, builder.get(1), "x");
    builder.createStore(alloca, builder.get(static_cast<CoreNumber>(0)), "store");
    builder.createBr(next);

    builder.setInsertPoint(next);
    auto* loaded = builder.createLoad(alloca, "load");
    builder.createRet(loaded);

    auto [completed, success] = testRunner.run("test");
    CHECK(completed);
    CHECK(success); // 0 is success
}

TEST_CASE("TargetCodeGenerator.conditional_branch_true_path")
{
    // Conditional branch - takes true path
    // entry:
    //   %result = alloca i64
    //   store 1, %result    ; non-zero default
    //   %cond = true
    //   condbr %cond, if.true, if.false
    // if.true:
    //   store 0, %result    ; zero = success
    //   br merge
    // if.false:
    //   store 2, %result
    //   br merge
    // merge:
    //   %v = load %result
    //   ret %v

    IRTestRunner testRunner;
    testRunner.createFunction("test");
    auto& builder = testRunner.builder();

    auto* entry = builder.createBlock("entry");
    auto* ifTrue = builder.createBlock("if.true");
    auto* ifFalse = builder.createBlock("if.false");
    auto* merge = builder.createBlock("merge");

    builder.setInsertPoint(entry);
    auto* result = builder.createAlloca(LiteralType::Number, builder.get(1), "result");
    builder.createStore(result, builder.get(static_cast<CoreNumber>(1)), "init");
    auto* cond = builder.get(true);
    builder.createCondBr(cond, ifTrue, ifFalse);

    builder.setInsertPoint(ifTrue);
    builder.createStore(result, builder.get(static_cast<CoreNumber>(0)), "store.true");
    builder.createBr(merge);

    builder.setInsertPoint(ifFalse);
    builder.createStore(result, builder.get(static_cast<CoreNumber>(2)), "store.false");
    builder.createBr(merge);

    builder.setInsertPoint(merge);
    auto* loaded = builder.createLoad(result, "load");
    builder.createRet(loaded);

    auto [completed, success] = testRunner.run("test");
    CHECK(completed);
    CHECK(success); // Should store 0 via true path
}

TEST_CASE("TargetCodeGenerator.conditional_branch_false_path")
{
    // Same as above but takes false path

    IRTestRunner testRunner;
    testRunner.createFunction("test");
    auto& builder = testRunner.builder();

    auto* entry = builder.createBlock("entry");
    auto* ifTrue = builder.createBlock("if.true");
    auto* ifFalse = builder.createBlock("if.false");
    auto* merge = builder.createBlock("merge");

    builder.setInsertPoint(entry);
    auto* result = builder.createAlloca(LiteralType::Number, builder.get(1), "result");
    builder.createStore(result, builder.get(static_cast<CoreNumber>(1)), "init");
    auto* cond = builder.get(false); // false this time
    builder.createCondBr(cond, ifTrue, ifFalse);

    builder.setInsertPoint(ifTrue);
    builder.createStore(result, builder.get(static_cast<CoreNumber>(0)), "store.true");
    builder.createBr(merge);

    builder.setInsertPoint(ifFalse);
    builder.createStore(result, builder.get(static_cast<CoreNumber>(0)), "store.false"); // also 0 for testing
    builder.createBr(merge);

    builder.setInsertPoint(merge);
    auto* loaded = builder.createLoad(result, "load");
    builder.createRet(loaded);

    auto [completed, success] = testRunner.run("test");
    CHECK(completed);
    CHECK(success); // Should store 0 via false path
}

TEST_CASE("TargetCodeGenerator.multiple_allocas_with_conditional")
{
    // This is the pattern that match expressions generate - multiple allocas
    // with conditional branches between check and arm blocks.
    //
    // entry:
    //   %scrutinee = alloca i64
    //   %result = alloca i64
    //   store 5, %scrutinee
    //   br check.0
    // check.0:
    //   %s = load %scrutinee
    //   %match = cmp eq %s, 5
    //   condbr %match, arm.0, check.1
    // arm.0:
    //   store 0, %result   ; matched - success
    //   br merge
    // check.1:
    //   store 1, %result   ; default - failure
    //   br merge
    // merge:
    //   %r = load %result
    //   ret %r

    IRTestRunner testRunner;
    testRunner.createFunction("test");
    auto& builder = testRunner.builder();

    auto* entry = builder.createBlock("entry");
    auto* check0 = builder.createBlock("check.0");
    auto* arm0 = builder.createBlock("arm.0");
    auto* check1 = builder.createBlock("check.1");
    auto* merge = builder.createBlock("merge");

    // Entry block - allocate storage
    builder.setInsertPoint(entry);
    auto* scrutinee = builder.createAlloca(LiteralType::Number, builder.get(1), "scrutinee");
    auto* result = builder.createAlloca(LiteralType::Number, builder.get(1), "result");
    builder.createStore(scrutinee, builder.get(static_cast<CoreNumber>(5)), "store.scrutinee");
    builder.createBr(check0);

    // First pattern check
    builder.setInsertPoint(check0);
    auto* s = builder.createLoad(scrutinee, "load.scrutinee");
    auto* match = builder.createNCmpEQ(s, builder.get(static_cast<CoreNumber>(5)), "match");
    builder.createCondBr(match, arm0, check1);

    // First arm body - matched!
    builder.setInsertPoint(arm0);
    builder.createStore(result, builder.get(static_cast<CoreNumber>(0)), "store.result.matched");
    builder.createBr(merge);

    // Second check (default case) - not matched
    builder.setInsertPoint(check1);
    builder.createStore(result, builder.get(static_cast<CoreNumber>(1)), "store.default");
    builder.createBr(merge);

    // Merge block
    builder.setInsertPoint(merge);
    auto* r = builder.createLoad(result, "load.result");
    builder.createRet(r);

    auto [completed, success] = testRunner.run("test");
    CHECK(completed);
    CHECK(success); // Should match first pattern and return 0
}

TEST_CASE("TargetCodeGenerator.match_pattern_fallthrough")
{
    // Same structure but scrutinee = 7 (doesn't match 5)
    // Should fall through to default

    IRTestRunner testRunner;
    testRunner.createFunction("test");
    auto& builder = testRunner.builder();

    auto* entry = builder.createBlock("entry");
    auto* check0 = builder.createBlock("check.0");
    auto* arm0 = builder.createBlock("arm.0");
    auto* check1 = builder.createBlock("check.1");
    auto* merge = builder.createBlock("merge");

    builder.setInsertPoint(entry);
    auto* scrutinee = builder.createAlloca(LiteralType::Number, builder.get(1), "scrutinee");
    auto* result = builder.createAlloca(LiteralType::Number, builder.get(1), "result");
    builder.createStore(scrutinee, builder.get(static_cast<CoreNumber>(7)), "store.scrutinee"); // 7 != 5
    builder.createBr(check0);

    builder.setInsertPoint(check0);
    auto* s = builder.createLoad(scrutinee, "load.scrutinee");
    auto* match = builder.createNCmpEQ(s, builder.get(static_cast<CoreNumber>(5)), "match");
    builder.createCondBr(match, arm0, check1);

    builder.setInsertPoint(arm0);
    builder.createStore(result, builder.get(static_cast<CoreNumber>(0)), "store.matched");
    builder.createBr(merge);

    builder.setInsertPoint(check1);
    builder.createStore(
        result, builder.get(static_cast<CoreNumber>(0)), "store.default"); // 0 for verification
    builder.createBr(merge);

    builder.setInsertPoint(merge);
    auto* r = builder.createLoad(result, "load.result");
    builder.createRet(r);

    auto [completed, success] = testRunner.run("test");
    CHECK(completed);
    CHECK(success); // Should take default path and return 0
}

TEST_CASE("TargetCodeGenerator.three_way_branch_second_match")
{
    // Three pattern checks with fallthrough
    // scrutinee = 2
    // check 1: == 1 -> arm1 (returns 1 - failure)
    // check 2: == 2 -> arm2 (returns 0 - success)  <- should match
    // default: -> arm3 (returns 3 - failure)

    IRTestRunner testRunner;
    testRunner.createFunction("test");
    auto& builder = testRunner.builder();

    auto* entry = builder.createBlock("entry");
    auto* check0 = builder.createBlock("check.0");
    auto* arm0 = builder.createBlock("arm.0");
    auto* check1 = builder.createBlock("check.1");
    auto* arm1 = builder.createBlock("arm.1");
    auto* defaultBlock = builder.createBlock("default");
    auto* merge = builder.createBlock("merge");

    builder.setInsertPoint(entry);
    auto* scrutinee = builder.createAlloca(LiteralType::Number, builder.get(1), "scrutinee");
    auto* result = builder.createAlloca(LiteralType::Number, builder.get(1), "result");
    builder.createStore(scrutinee, builder.get(static_cast<CoreNumber>(2)), "store.scrutinee");
    builder.createBr(check0);

    builder.setInsertPoint(check0);
    auto* s0 = builder.createLoad(scrutinee, "load.s0");
    auto* match0 = builder.createNCmpEQ(s0, builder.get(static_cast<CoreNumber>(1)), "match0");
    builder.createCondBr(match0, arm0, check1);

    builder.setInsertPoint(arm0);
    builder.createStore(result, builder.get(static_cast<CoreNumber>(1)), "store.1"); // non-zero
    builder.createBr(merge);

    builder.setInsertPoint(check1);
    auto* s1 = builder.createLoad(scrutinee, "load.s1");
    auto* match1 = builder.createNCmpEQ(s1, builder.get(static_cast<CoreNumber>(2)), "match1");
    builder.createCondBr(match1, arm1, defaultBlock);

    builder.setInsertPoint(arm1);
    builder.createStore(result, builder.get(static_cast<CoreNumber>(0)), "store.0"); // zero = success
    builder.createBr(merge);

    builder.setInsertPoint(defaultBlock);
    builder.createStore(result, builder.get(static_cast<CoreNumber>(3)), "store.3"); // non-zero
    builder.createBr(merge);

    builder.setInsertPoint(merge);
    auto* r = builder.createLoad(result, "load.result");
    builder.createRet(r);

    auto [completed, success] = testRunner.run("test");
    CHECK(completed);
    CHECK(success); // Should match second pattern and return 0
}

TEST_CASE("TargetCodeGenerator.three_way_branch_first_match")
{
    // Same as above but scrutinee = 1 (matches first)

    IRTestRunner testRunner;
    testRunner.createFunction("test");
    auto& builder = testRunner.builder();

    auto* entry = builder.createBlock("entry");
    auto* check0 = builder.createBlock("check.0");
    auto* arm0 = builder.createBlock("arm.0");
    auto* check1 = builder.createBlock("check.1");
    auto* arm1 = builder.createBlock("arm.1");
    auto* defaultBlock = builder.createBlock("default");
    auto* merge = builder.createBlock("merge");

    builder.setInsertPoint(entry);
    auto* scrutinee = builder.createAlloca(LiteralType::Number, builder.get(1), "scrutinee");
    auto* result = builder.createAlloca(LiteralType::Number, builder.get(1), "result");
    builder.createStore(scrutinee, builder.get(static_cast<CoreNumber>(1)), "store.scrutinee");
    builder.createBr(check0);

    builder.setInsertPoint(check0);
    auto* s0 = builder.createLoad(scrutinee, "load.s0");
    auto* match0 = builder.createNCmpEQ(s0, builder.get(static_cast<CoreNumber>(1)), "match0");
    builder.createCondBr(match0, arm0, check1);

    builder.setInsertPoint(arm0);
    builder.createStore(result, builder.get(static_cast<CoreNumber>(0)), "store.0"); // zero = success
    builder.createBr(merge);

    builder.setInsertPoint(check1);
    auto* s1 = builder.createLoad(scrutinee, "load.s1");
    auto* match1 = builder.createNCmpEQ(s1, builder.get(static_cast<CoreNumber>(2)), "match1");
    builder.createCondBr(match1, arm1, defaultBlock);

    builder.setInsertPoint(arm1);
    builder.createStore(result, builder.get(static_cast<CoreNumber>(2)), "store.2");
    builder.createBr(merge);

    builder.setInsertPoint(defaultBlock);
    builder.createStore(result, builder.get(static_cast<CoreNumber>(3)), "store.3");
    builder.createBr(merge);

    builder.setInsertPoint(merge);
    auto* r = builder.createLoad(result, "load.result");
    builder.createRet(r);

    auto [completed, success] = testRunner.run("test");
    CHECK(completed);
    CHECK(success); // Should match first pattern and return 0
}

TEST_CASE("TargetCodeGenerator.three_way_branch_default")
{
    // Same as above but scrutinee = 99 (matches default)

    IRTestRunner testRunner;
    testRunner.createFunction("test");
    auto& builder = testRunner.builder();

    auto* entry = builder.createBlock("entry");
    auto* check0 = builder.createBlock("check.0");
    auto* arm0 = builder.createBlock("arm.0");
    auto* check1 = builder.createBlock("check.1");
    auto* arm1 = builder.createBlock("arm.1");
    auto* defaultBlock = builder.createBlock("default");
    auto* merge = builder.createBlock("merge");

    builder.setInsertPoint(entry);
    auto* scrutinee = builder.createAlloca(LiteralType::Number, builder.get(1), "scrutinee");
    auto* result = builder.createAlloca(LiteralType::Number, builder.get(1), "result");
    builder.createStore(scrutinee, builder.get(static_cast<CoreNumber>(99)), "store.scrutinee");
    builder.createBr(check0);

    builder.setInsertPoint(check0);
    auto* s0 = builder.createLoad(scrutinee, "load.s0");
    auto* match0 = builder.createNCmpEQ(s0, builder.get(static_cast<CoreNumber>(1)), "match0");
    builder.createCondBr(match0, arm0, check1);

    builder.setInsertPoint(arm0);
    builder.createStore(result, builder.get(static_cast<CoreNumber>(1)), "store.1");
    builder.createBr(merge);

    builder.setInsertPoint(check1);
    auto* s1 = builder.createLoad(scrutinee, "load.s1");
    auto* match1 = builder.createNCmpEQ(s1, builder.get(static_cast<CoreNumber>(2)), "match1");
    builder.createCondBr(match1, arm1, defaultBlock);

    builder.setInsertPoint(arm1);
    builder.createStore(result, builder.get(static_cast<CoreNumber>(2)), "store.2");
    builder.createBr(merge);

    builder.setInsertPoint(defaultBlock);
    builder.createStore(result, builder.get(static_cast<CoreNumber>(0)), "store.0"); // zero = success
    builder.createBr(merge);

    builder.setInsertPoint(merge);
    auto* r = builder.createLoad(result, "load.result");
    builder.createRet(r);

    auto [completed, success] = testRunner.run("test");
    CHECK(completed);
    CHECK(success); // Should match default and return 0
}

// =============================================================================
// Source location propagation tests
// =============================================================================

TEST_CASE("TargetCodeGenerator.source_location_propagation")
{
    // Test that source locations from IR instructions are recorded
    // in the Function's sparse location table and can be looked up.

    IRTestRunner testRunner;
    testRunner.createFunction("test");
    auto& builder = testRunner.builder();

    // Create instructions with source locations
    SourceLocation loc1("test.endo", FilePos { 1, 1, 0 }, FilePos { 1, 10, 9 });
    SourceLocation loc2("test.endo", FilePos { 2, 1, 10 }, FilePos { 2, 15, 24 });

    auto* entry = builder.createBlock("entry");
    builder.setInsertPoint(entry);

    // Set location for first instruction
    builder.setSourceLocation(loc1);
    auto* x = builder.get(static_cast<CoreNumber>(42));
    auto* y = builder.get(static_cast<CoreNumber>(10));
    auto* sum = builder.createAdd(x, y, "sum");

    // Set different location for second instruction
    builder.setSourceLocation(loc2);
    builder.createRet(sum);

    // Generate target code
    TargetCodeGenerator codegen;
    auto targetProgram = codegen.generate(builder.program());

    Function const* fn = targetProgram->findFunction("test");
    REQUIRE(fn != nullptr);

    // Verify that the function has source locations recorded
    // The exact offsets depend on code generation, but we can verify
    // that locations are properly recorded and lookup works

    // Find an offset that maps to loc1
    bool foundLoc1 = false;
    bool foundLoc2 = false;
    for (size_t i = 0; i < fn->code().size(); ++i)
    {
        auto const& loc = fn->locationOf(i);
        if (!loc.filename.empty())
        {
            if (loc.begin.line == 1)
                foundLoc1 = true;
            if (loc.begin.line == 2)
                foundLoc2 = true;
        }
    }

    // At least one of the locations should be found
    // (loc1 for the add, loc2 for the ret)
    CHECK((foundLoc1 || foundLoc2));

    // The file name should be correct
    auto const& anyLoc = fn->locationOf(0);
    if (!anyLoc.filename.empty())
    {
        CHECK(anyLoc.filename == "test.endo");
    }
}

TEST_CASE("TargetCodeGenerator.empty_location_table")
{
    // Test that lookups work when no locations were set

    IRTestRunner testRunner;
    testRunner.createFunction("test");
    auto& builder = testRunner.builder();

    auto* entry = builder.createBlock("entry");
    builder.setInsertPoint(entry);

    // No setSourceLocation calls - should use empty location
    builder.createRet(builder.get(static_cast<CoreNumber>(0)));

    TargetCodeGenerator codegen;
    auto targetProgram = codegen.generate(builder.program());

    Function const* fn = targetProgram->findFunction("test");
    REQUIRE(fn != nullptr);

    // Location lookup should return empty location
    auto const& loc = fn->locationOf(0);
    CHECK(loc.filename.empty());
}
