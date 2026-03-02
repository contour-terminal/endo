// SPDX-License-Identifier: Apache-2.0
#include <CoreVM/CoreVM.hpp>
#include <CoreVM/sysconfig.h>

#include <algorithm>
#include <print>
#include <string>
#include <vector>

namespace CoreVM
{

Function::Function(Program* program, std::string name, std::vector<Instruction> code):
    _program(program), _name(std::move(name))
{
    setCode(std::move(code));
}

void Function::setCode(std::vector<Instruction> code)
{
    _code = std::move(code);

    if (opcode(_code.back()) != Opcode::EXIT)
        _code.push_back(makeInstruction(Opcode::EXIT, false));

    _stackSize = computeStackSize(_code.data(), _code.size());

#if defined(COREVM_DIRECT_THREADED_VM)
    _directThreadedCode.clear();
#endif
}

void Function::disassemble() const noexcept
{
    std::println("\n.function {:27} ; ({} stack size, {} instructions)", name(), stackSize(), code().size());
    std::print("{}", CoreVM::disassemble(_code.data(), _code.size(), "  ", &_program->constants()));
}

void Function::setLocationTable(std::vector<std::pair<size_t, SourceLocation>> table)
{
    _locationTable = std::make_unique<std::vector<std::pair<size_t, SourceLocation>>>(std::move(table));
}

SourceLocation const& Function::locationOf(size_t offset) const
{
    static SourceLocation empty;
    if (!_locationTable || _locationTable->empty())
        return empty;

    // Binary search for largest offset <= target
    auto it = std::upper_bound(_locationTable->begin(),
                               _locationTable->end(),
                               offset,
                               [](size_t off, auto const& entry) { return off < entry.first; });

    if (it == _locationTable->begin())
        return empty;

    return std::prev(it)->second;
}

} // namespace CoreVM
