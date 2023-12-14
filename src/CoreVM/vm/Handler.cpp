// SPDX-License-Identifier: Apache-2.0
module;
#include <CoreVM/sysconfig.h>
#include <string>
#include <vector>
module CoreVM;
namespace CoreVM
{

Handler::Handler(Program* program, std::string name, std::vector<Instruction> code):
    _program(program),
    _name(std::move(name)),
    _stackSize(),
    _code()
#if defined(COREVM_DIRECT_THREADED_VM)
    ,
    _directThreadedCode()
#endif
{
    setCode(std::move(code));
}

void Handler::setCode(std::vector<Instruction> code)
{
    _code = std::move(code);

    if (opcode(_code.back()) != Opcode::EXIT)
        _code.push_back(makeInstruction(Opcode::EXIT, false));

    _stackSize = computeStackSize(_code.data(), _code.size());

#if defined(COREVM_DIRECT_THREADED_VM)
    _directThreadedCode.clear();
#endif
}

void Handler::disassemble() const noexcept
{
    printf("\n.handler %-27s ; (%zu stack size, %zu instructions)\n",
           name().c_str(),
           stackSize(),
           code().size());
    printf("%s", CoreVM::disassemble(_code.data(), _code.size(), "  ", &_program->constants()).c_str());
}

} // namespace CoreVM
