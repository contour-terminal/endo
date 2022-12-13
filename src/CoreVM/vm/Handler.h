// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <CoreVM/sysconfig.h>
#include <CoreVM/vm/Instruction.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace CoreVM
{

class Program;
class Runner;

class Handler
{
  public:
    Handler(Program* program, std::string name, std::vector<Instruction> instructions);
    Handler() = default;
    Handler(const Handler& handler) = default;
    Handler(Handler&& handler) noexcept = default;
    Handler& operator=(const Handler& handler) = default;
    Handler& operator=(Handler&& handler) noexcept = default;
    ~Handler() = default;

    [[nodiscard]] Program* program() const noexcept { return _program; }

    [[nodiscard]] const std::string& name() const noexcept { return _name; }
    void setName(const std::string& name) { _name = name; }

    [[nodiscard]] size_t stackSize() const noexcept { return _stackSize; }

    [[nodiscard]] const std::vector<Instruction>& code() const noexcept { return _code; }
    void setCode(std::vector<Instruction> code);

#if defined(COREVM_DIRECT_THREADED_VM)
    [[nodiscard]] const std::vector<uint64_t>& directThreadedCode() const noexcept { return _directThreadedCode; }
    [[nodiscard]] std::vector<uint64_t>& directThreadedCode() noexcept { return _directThreadedCode; }
#endif

    void disassemble() const noexcept;

  private:
    Program* _program {};
    std::string _name;
    size_t _stackSize {};
    std::vector<Instruction> _code;
#if defined(COREVM_DIRECT_THREADED_VM)
    std::vector<uint64_t> _directThreadedCode;
#endif
};

} // namespace CoreVM
