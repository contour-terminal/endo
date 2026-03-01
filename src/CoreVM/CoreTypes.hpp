// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <CoreVM/enums.hpp>
#include <CoreVM/util.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace CoreVM
{

class ConstantPool;

using Register = uint64_t;
using CoreNumber = int64_t;
using CoreString = std::string;
using Quota = int64_t;
constexpr Quota NoQuota = -1;

/**
 * Disassembles the @p program with @p n instructions.
 *
 * @param program pointer to the first instruction to disassemble
 * @param n       number of instructions to disassemble
 * @param indent  prefix to inject in front of every new instruction line
 * @param cp      pointer to ConstantPool for pretty-printing or @c nullptr
 *
 * @returns       disassembled program in text form.
 */
std::string disassemble(const Instruction* program,
                        size_t n,
                        const std::string& indent,
                        const ConstantPool* cp);

/**
 * Disassembles a single instruction.
 *
 * @param pc      Instruction to disassemble.
 * @param ip      The instruction pointer at which position the instruction is
 *                located within the program.
 * @param sp      current stack size (depth) before executing given instruction.
 *                This value will be modified as if the instruction would have
 *                been executed.
 * @param cp      pointer to ConstantPool for pretty-printing or @c nullptr
 */
std::string disassemble(Instruction pc, size_t ip, size_t sp, const ConstantPool* cp);

std::string tos(LiteralType type);

bool isArrayType(LiteralType type);
LiteralType elementTypeOf(LiteralType type);

// {{{ array types
class CoreArray
{
  public:
    [[nodiscard]] size_t size() const { return static_cast<size_t>(_base[0]); }

  protected:
    explicit CoreArray(const Register* base): _base(base) {}

    [[nodiscard]] Register getRawAt(size_t i) const { return _base[1 + i]; }

    [[nodiscard]] const Register* data() const { return _base + 1; }

  protected:
    const Register* _base;
};

using CoreIntArray = std::vector<CoreNumber>;
using CoreStringArray = std::vector<CoreString>;
using CoreIPAddrArray = std::vector<util::IPAddress>;
using CoreCidrArray = std::vector<util::Cidr>;
// }}}

} // namespace CoreVM
