// SPDX-License-Identifier: Apache-2.0
#include <CoreVM/CoreVM.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>

using namespace CoreVM;

TEST_CASE("Runner::invoke passes an argument and returns the result", "[Runner][invoke]")
{
    // fn(x) = x + 100, returned via URET. invoke({7}) must yield 107: the argument
    // is seeded at fp+0 (read by LOAD 0) and the URET result is read back. This is
    // the native->script call convention the httpServe builtin uses to dispatch into
    // a script handler.
    ConstantPool cp;
    auto const fnId = cp.setFunction("handler",
                                     { makeInstruction(Opcode::LOAD, 0),
                                       makeInstruction(Opcode::ILOAD, 100),
                                       makeInstruction(Opcode::NADD),
                                       makeInstruction(Opcode::URET) });
    cp.setFunctionParameterCount(fnId, 1);

    Program program(std::move(cp));
    auto globals = Runner::Globals {};
    Runner vm(program.findFunction("handler"), nullptr, &globals, RuntimeConfig::defaultConfig(), nullptr);

    std::array<Runner::Value, 1> const args { 7 };
    auto const result = vm.invoke(args);

    REQUIRE(result.has_value());
    REQUIRE(static_cast<CoreNumber>(*result) == 107);
}
