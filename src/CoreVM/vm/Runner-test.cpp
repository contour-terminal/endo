// SPDX-License-Identifier: Apache-2.0
#include <CoreVM/CoreVM.hpp>

#include <array>

#include <xzero/testing.h>
using namespace xzero;
using namespace CoreVM;
using Code = ConstantPool::Code;

std::unique_ptr<Runner> run(Code&& code)
{
    ConstantPool cp;
    cp.makeInteger(3);
    cp.makeInteger(4);
    cp.setFunction("main", std::move(code));
    Program program(std::move(cp));
    std::unique_ptr<Runner> vm = std::make_unique<Runner>(program.findFunction("main"), nullptr);
    vm->run();
    return vm;
}

// {{{ numeric
TEST(flow_vm_Runner, iload)
{
    auto vm = run({ makeInstruction(Opcode::ILOAD, 3) });

    ASSERT_EQ(1, vm->getStackPointer());
    EXPECT_EQ(3, vm->stack(-1));
}

TEST(flow_vm_Runner, nload)
{
    auto vm = run({ makeInstruction(Opcode::NLOAD, 0) });

    ASSERT_EQ(1, vm->getStackPointer());
    EXPECT_EQ(3, vm->stack(-1));
}

TEST(flow_vm_Runner, nneg)
{
    auto vm = run({ makeInstruction(Opcode::ILOAD, 3), makeInstruction(Opcode::NNEG) });

    ASSERT_EQ(1, vm->getStackPointer());
    EXPECT_EQ(-3, static_cast<CoreNumber>(vm->stack(-1)));
}

TEST(flow_vm_Runner, nnot)
{
    auto vm = run({ makeInstruction(Opcode::ILOAD, 3), makeInstruction(Opcode::NNOT) });

    ASSERT_EQ(1, vm->getStackPointer());
    EXPECT_EQ(~3, static_cast<CoreNumber>(vm->stack(-1)));
}

TEST(flow_vm_Runner, nadd)
{
    auto vm = run({ makeInstruction(Opcode::ILOAD, 3),
                    makeInstruction(Opcode::ILOAD, 4),
                    makeInstruction(Opcode::NADD) });

    ASSERT_EQ(1, vm->getStackPointer());
    EXPECT_EQ(7, vm->stack(-1));
}

TEST(flow_vm_Runner, nsub)
{
    auto vm = run({ makeInstruction(Opcode::ILOAD, 7),
                    makeInstruction(Opcode::ILOAD, 4),
                    makeInstruction(Opcode::NSUB) });

    ASSERT_EQ(1, vm->getStackPointer());
    EXPECT_EQ(3, vm->stack(-1));
}

TEST(flow_vm_Runner, nmul)
{
    auto vm = run({ makeInstruction(Opcode::ILOAD, 3),
                    makeInstruction(Opcode::ILOAD, 4),
                    makeInstruction(Opcode::NMUL) });

    ASSERT_EQ(1, vm->getStackPointer());
    EXPECT_EQ(12, vm->stack(-1));
}

TEST(flow_vm_Runner, ndiv)
{
    auto vm = run({ makeInstruction(Opcode::ILOAD, 12),
                    makeInstruction(Opcode::ILOAD, 4),
                    makeInstruction(Opcode::NDIV) });

    ASSERT_EQ(1, vm->getStackPointer());
    EXPECT_EQ(3, vm->stack(-1));
}

// }}}

// {{{ invoke (native -> script function call with args + result)
TEST(flow_vm_Runner, invoke_passes_arg_and_returns_result)
{
    // fn(x) = x + 100, returned via URET. invoke({7}) must yield 107: the argument
    // is seeded at fp+0 (read by LOAD 0) and the URET result is read back.
    ConstantPool cp;
    auto const fnId = cp.setFunction("handler",
                                     { makeInstruction(Opcode::LOAD, 0),
                                       makeInstruction(Opcode::ILOAD, 100),
                                       makeInstruction(Opcode::NADD),
                                       makeInstruction(Opcode::URET) });
    cp.setFunctionParameterCount(fnId, 1);

    Program program(std::move(cp));
    Runner vm(program.findFunction("handler"), nullptr);

    std::array<Runner::Value, 1> const args { 7 };
    auto const result = vm.invoke(args);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(107, static_cast<CoreNumber>(*result));
}

// }}}
