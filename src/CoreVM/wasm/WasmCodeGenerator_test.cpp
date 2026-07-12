// SPDX-License-Identifier: Apache-2.0
// Tests for WasmCodeGenerator: IR is built programmatically with IRBuilder,
// compiled to a WASM module, and verified structurally (the module always
// passes binaryen validation before generate() returns) plus via WAT
// substring checks. Runtime helpers are satisfied by ImportOnlyRuntimeProvider.

#include <CoreVM/CoreVM.hpp>
#include <CoreVM/wasm/WasmCodeGenerator.hpp>
#include <CoreVM/wasm/WasmRuntimeABI.hpp>

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <optional>
#include <string>

using namespace CoreVM;

namespace
{

/// Builds IR via IRBuilder and compiles it with the WASM backend.
class WasmTestCompiler
{
  public:
    WasmTestCompiler() { _builder.setProgram(std::make_unique<IRProgram>()); }

    IRBuilder& builder() { return _builder; }

    IRFunction* createFunction(std::string const& name)
    {
        auto* function = _builder.getFunction(name);
        _builder.setFunction(function);
        return function;
    }

    /// Compiles the program; on success returns the output with WAT enabled.
    std::optional<CoreVM::wasm::WasmOutput> compile()
    {
        auto provider = CoreVM::wasm::ImportOnlyRuntimeProvider {};
        auto generator =
            CoreVM::wasm::WasmCodeGenerator(provider, CoreVM::wasm::WasmOptions { .emitWat = true });
        return generator.generate(_builder.program(), _report);
    }

    diagnostics::BufferedReport const& report() const { return _report; }

  private:
    IRBuilder _builder;
    diagnostics::BufferedReport _report;
};

/// True if the WASM binary starts with the "\0asm" magic and version 1.
bool hasWasmMagic(std::vector<uint8_t> const& binary)
{
    return binary.size() >= 8 && binary[0] == 0x00 && binary[1] == 'a' && binary[2] == 's' && binary[3] == 'm'
           && binary[4] == 0x01;
}

} // namespace

TEST_CASE("WasmCodeGenerator.main_ret_zero")
{
    // @main: ret 0  =>  valid module with an exported _start calling proc_exit.
    WasmTestCompiler compiler;
    compiler.createFunction("@main");
    auto& builder = compiler.builder();

    auto* entry = builder.createBlock("entry");
    builder.setInsertPoint(entry);
    builder.createRet(builder.get(static_cast<CoreNumber>(0)));

    auto const output = compiler.compile();
    REQUIRE(output.has_value());
    CHECK(hasWasmMagic(output->binary));
    CHECK(output->wat.contains("_start"));
    CHECK(output->wat.contains("proc_exit"));
    CHECK(output->wat.contains("fn$@main"));
    CHECK(output->wat.contains("(memory"));
}

TEST_CASE("WasmCodeGenerator.alloca_store_load_ret")
{
    // %x = alloca; store 42, %x; %v = load %x; ret %v
    WasmTestCompiler compiler;
    compiler.createFunction("@main");
    auto& builder = compiler.builder();

    auto* entry = builder.createBlock("entry");
    builder.setInsertPoint(entry);
    auto* variable = builder.createAlloca(LiteralType::Number, builder.get(1), "x");
    builder.createStore(variable, builder.get(static_cast<CoreNumber>(42)), "store");
    auto* loaded = builder.createLoad(variable, "load");
    builder.createRet(loaded);

    auto const output = compiler.compile();
    REQUIRE(output.has_value());
    CHECK(output->wat.contains("local.set"));
    CHECK(output->wat.contains("local.get"));
}

TEST_CASE("WasmCodeGenerator.cond_br_diamond")
{
    // Diamond control flow:
    //   entry: %x = alloca; store 1, %x; %c = load %x; condbr %c, then, else
    //   then:  store 10, %x; br merge
    //   else:  store 20, %x; br merge
    //   merge: %r = load %x; ret %r
    WasmTestCompiler compiler;
    compiler.createFunction("@main");
    auto& builder = compiler.builder();

    auto* entry = builder.createBlock("entry");
    auto* thenBlock = builder.createBlock("then");
    auto* elseBlock = builder.createBlock("else");
    auto* mergeBlock = builder.createBlock("merge");

    builder.setInsertPoint(entry);
    auto* variable = builder.createAlloca(LiteralType::Number, builder.get(1), "x");
    builder.createStore(variable, builder.get(static_cast<CoreNumber>(1)), "init");
    auto* condition = builder.createLoad(variable, "cond");
    builder.createCondBr(condition, thenBlock, elseBlock);

    builder.setInsertPoint(thenBlock);
    builder.createStore(variable, builder.get(static_cast<CoreNumber>(10)), "then.store");
    builder.createBr(mergeBlock);

    builder.setInsertPoint(elseBlock);
    builder.createStore(variable, builder.get(static_cast<CoreNumber>(20)), "else.store");
    builder.createBr(mergeBlock);

    builder.setInsertPoint(mergeBlock);
    auto* result = builder.createLoad(variable, "result");
    builder.createRet(result);

    auto const output = compiler.compile();
    REQUIRE(output.has_value());
    CHECK(output->wat.contains("if"));
}

TEST_CASE("WasmCodeGenerator.loop_back_edge")
{
    // Loop with a back edge (no arithmetic yet; the condition is re-loaded):
    //   entry: %x = alloca; store 1, %x; br head
    //   head:  %c = load %x; condbr %c, body, exit
    //   body:  store 0, %x; br head
    //   exit:  ret 0
    WasmTestCompiler compiler;
    compiler.createFunction("@main");
    auto& builder = compiler.builder();

    auto* entry = builder.createBlock("entry");
    auto* head = builder.createBlock("head");
    auto* body = builder.createBlock("body");
    auto* exitBlock = builder.createBlock("exit");

    builder.setInsertPoint(entry);
    auto* variable = builder.createAlloca(LiteralType::Number, builder.get(1), "x");
    builder.createStore(variable, builder.get(static_cast<CoreNumber>(1)), "init");
    builder.createBr(head);

    builder.setInsertPoint(head);
    auto* condition = builder.createLoad(variable, "cond");
    builder.createCondBr(condition, body, exitBlock);

    builder.setInsertPoint(body);
    builder.createStore(variable, builder.get(static_cast<CoreNumber>(0)), "clear");
    builder.createBr(head);

    builder.setInsertPoint(exitBlock);
    builder.createRet(builder.get(static_cast<CoreNumber>(0)));

    auto const output = compiler.compile();
    REQUIRE(output.has_value());
    CHECK(output->wat.contains("loop"));
}

TEST_CASE("WasmCodeGenerator.unsupported_construct_reports_error")
{
    // Regular expressions are permanently unsupported in the WASM backend:
    // generation must fail with a diagnostic instead of producing a module.
    WasmTestCompiler compiler;
    compiler.createFunction("@main");
    auto& builder = compiler.builder();

    auto* entry = builder.createBlock("entry");
    builder.setInsertPoint(entry);
    auto* group = builder.createRegExpGroup(builder.get(static_cast<CoreNumber>(0)), "re.group");
    builder.createRet(group);

    auto const output = compiler.compile();
    CHECK(!output.has_value());
    CHECK(compiler.report().containsFailures());

    auto errorText = std::string {};
    for (auto const& message: compiler.report())
        errorText += message.string();
    CHECK(errorText.contains("cannot compile to WebAssembly"));
    CHECK(errorText.contains("not supported"));
}
