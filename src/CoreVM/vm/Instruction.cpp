// SPDX-License-Identifier: Apache-2.0
#include <CoreVM/CoreVM.hpp>
#include <CoreVM/util.hpp>

#include <climits>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <iterator>
#include <sstream>
#include <unordered_map>
#include <utility>
#include <vector>

namespace CoreVM
{

// {{{ InstructionInfo
struct InstructionInfo
{
    const char* mnemonic;
    Opcode opcode;
    OperandSig operandSig;
    LiteralType stackOutput;
    int stackChange;

    InstructionInfo() = default;
    InstructionInfo(const InstructionInfo&) = default;
    InstructionInfo(InstructionInfo&&) noexcept = default;
    InstructionInfo& operator=(const InstructionInfo&) = default;
    InstructionInfo& operator=(InstructionInfo&&) noexcept = default;
    ~InstructionInfo() = default;

    InstructionInfo(
        Opcode opc, const char* const m, OperandSig opsig, int stackChange, LiteralType stackOutput):
        mnemonic(m), opcode(opc), operandSig(opsig), stackOutput(stackOutput), stackChange(stackChange)
    {
    }
};

#define IIDEF(opcode, operandSig, stackChange, stackOutput) \
    { Opcode::opcode, #opcode, OperandSig::operandSig, stackChange, LiteralType::stackOutput }
// [(size_t)(Opcode:: opcode)] = { Opcode:: opcode, #opcode, OperandSig:: operandSig, stackChange,
// LiteralType:: stackOutput }

// OPCODE, operandSignature, stackChange
static InstructionInfo instructionInfos[] = {
    // misc
    IIDEF(NOP, V, 0, Void),
    IIDEF(ALLOCA, I, 0, Void),
    IIDEF(DISCARD, I, 0, Void),
    IIDEF(STACKROT, I, 0, Void),

    IIDEF(GALLOCA, I, 0, Void),
    IIDEF(GLOAD, I, 1, Void),
    IIDEF(GSTORE, I, -1, Void),

    // control
    IIDEF(EXIT, I, 0, Void),
    IIDEF(EXITPOP, V, -1, Void),
    IIDEF(JMP, I, 0, Void),
    IIDEF(JN, I, -1, Void),
    IIDEF(JZ, I, -1, Void),

    // arrays
    IIDEF(ITLOAD, I, 1, IntArray),
    IIDEF(STLOAD, I, 1, StringArray),
    IIDEF(PTLOAD, I, 1, IPAddrArray),
    IIDEF(CTLOAD, I, 1, CidrArray),

    IIDEF(LOAD, I, 1, Void),
    IIDEF(STORE, I, -1, Void),

    // numeric
    IIDEF(ILOAD, I, 1, Number),
    IIDEF(NLOAD, I, 1, Number),
    IIDEF(NNEG, V, 0, Number),
    IIDEF(NNOT, V, 0, Number),
    IIDEF(NADD, V, -1, Number),
    IIDEF(NSUB, V, -1, Number),
    IIDEF(NMUL, V, -1, Number),
    IIDEF(NDIV, V, -1, Number),
    IIDEF(NREM, V, -1, Number),
    IIDEF(NSHL, V, -1, Number),
    IIDEF(NSHR, V, -1, Number),
    IIDEF(NPOW, V, -1, Number),
    IIDEF(NAND, V, -1, Number),
    IIDEF(NOR, V, -1, Number),
    IIDEF(NXOR, V, -1, Number),
    IIDEF(NCMPZ, V, 0, Boolean),
    IIDEF(NCMPEQ, V, -1, Boolean),
    IIDEF(NCMPNE, V, -1, Boolean),
    IIDEF(NCMPLE, V, -1, Boolean),
    IIDEF(NCMPGE, V, -1, Boolean),
    IIDEF(NCMPLT, V, -1, Boolean),
    IIDEF(NCMPGT, V, -1, Boolean),

    // bool
    IIDEF(BNOT, V, 0, Boolean),
    IIDEF(BAND, V, -1, Boolean),
    IIDEF(BOR, V, -1, Boolean),
    IIDEF(BXOR, V, -1, Boolean),

    // string
    IIDEF(SLOAD, I, 1, String),
    IIDEF(SADD, V, -1, String),
    IIDEF(SSUBSTR, V, -2, String),
    IIDEF(SCMPEQ, V, -1, Boolean),
    IIDEF(SCMPNE, V, -1, Boolean),
    IIDEF(SCMPLE, V, -1, Boolean),
    IIDEF(SCMPGE, V, -1, Boolean),
    IIDEF(SCMPLT, V, -1, Boolean),
    IIDEF(SCMPGT, V, -1, Boolean),
    IIDEF(SCMPBEG, V, -1, Boolean),
    IIDEF(SCMPEND, V, -1, Boolean),
    IIDEF(SCONTAINS, V, -1, Boolean),
    IIDEF(SLEN, V, 0, Number),
    IIDEF(SISEMPTY, V, 0, Boolean),
    IIDEF(SMATCHEQ, I, -1, Void),
    IIDEF(SMATCHBEG, I, -1, Void),
    IIDEF(SMATCHEND, I, -1, Void),
    IIDEF(SMATCHR, I, -1, Void),

    // IP
    IIDEF(PLOAD, I, 1, IPAddress),
    IIDEF(PCMPEQ, V, -1, Boolean),
    IIDEF(PCMPNE, V, -1, Boolean),
    IIDEF(PINCIDR, V, -1, Boolean),

    // Cidr
    IIDEF(CLOAD, I, 1, Cidr),

    // regex
    IIDEF(SREGMATCH, I, 0, Boolean),
    IIDEF(SREGGROUP, I, 1, String),

    // cast
    IIDEF(N2S, V, 0, String),
    IIDEF(P2S, V, 0, String),
    IIDEF(C2S, V, 0, String),
    IIDEF(R2S, V, 0, String),
    IIDEF(S2N, V, 0, Number),

    // invocation
    IIDEF(CALL, III, 0, Void),

    // object operations
    IIDEF(OALLOC, I, 1, Object),   // typeId → push new object
    IIDEF(ORETAIN, V, 0, Void),    // increment refcount of top
    IIDEF(ORELEASE, V, -1, Void),  // decrement refcount, pop
    IIDEF(OGETTAG, V, 0, Number),  // pop obj, push tag
    IIDEF(OSETTAG, V, -1, Void),   // pop tag, pop obj, set tag, push obj
    IIDEF(OGETSLOT, I, 0, Void),   // pop obj, push slot[imm] (replaces obj with slot value)
    IIDEF(OSETSLOT, I, -1, Void),  // pop value, pop obj, set slot[imm] = value, push obj
    IIDEF(OTYPEID, V, 0, Number),  // pop obj, push type ID
    IIDEF(OISTYPE, I, 0, Boolean), // pop obj, push (obj.typeId == imm)

    // dynamic value comparison
    IIDEF(VCMPEQ, V, -1, Boolean), // pop B, pop A, push (A == B) as numbers
    IIDEF(VCMPNE, V, -1, Boolean), // pop B, pop A, push (A != B) as numbers
    IIDEF(VCMPLT, V, -1, Boolean), // pop B, pop A, push (A < B) as numbers
    IIDEF(VCMPLE, V, -1, Boolean), // pop B, pop A, push (A <= B) as numbers
    IIDEF(VCMPGT, V, -1, Boolean), // pop B, pop A, push (A > B) as numbers
    IIDEF(VCMPGE, V, -1, Boolean), // pop B, pop A, push (A >= B) as numbers

    // float
    IIDEF(FLOAD, I, 1, Float),
    IIDEF(FNEG, V, 0, Float),
    IIDEF(FADD, V, -1, Float),
    IIDEF(FSUB, V, -1, Float),
    IIDEF(FMUL, V, -1, Float),
    IIDEF(FDIV, V, -1, Float),
    IIDEF(FREM, V, -1, Float),
    IIDEF(FPOW, V, -1, Float),
    IIDEF(FCMPEQ, V, -1, Boolean),
    IIDEF(FCMPNE, V, -1, Boolean),
    IIDEF(FCMPLE, V, -1, Boolean),
    IIDEF(FCMPGE, V, -1, Boolean),
    IIDEF(FCMPLT, V, -1, Boolean),
    IIDEF(FCMPGT, V, -1, Boolean),

    // float cast
    IIDEF(N2F, V, 0, Float),
    IIDEF(F2N, V, 0, Number),
    IIDEF(F2S, V, 0, String),
    IIDEF(S2F, V, 0, Float),

    // user-defined function calls
    IIDEF(UCALL, II, 0, Void),  // stack change handled dynamically (pops argc, pushes 1 return value)
    IIDEF(URET, V, 0, Void),    // stack change handled dynamically (returns to caller frame)
    IIDEF(UTCALL, II, 0, Void), // stack change handled dynamically (tail call, reuses frame)

    // indirect user call (via Callable object)
    IIDEF(IUCALL, I, 0, Void),  // stack change handled dynamically (pops callable + argc args, pushes 1)
    IIDEF(IUTCALL, I, 0, Void), // stack change handled dynamically (indirect tail call)

    // lazy evaluation
    IIDEF(LFORCE, V, 0, Void), // consumes lazy obj, pushes result (net 0)
};

// }}}

int getStackChange(Instruction instr)
{
    Opcode opc = opcode(instr);
    switch (opc)
    {
        case Opcode::ALLOCA: return operandA(instr);
        case Opcode::DISCARD: return -static_cast<int>(operandA(instr));
        case Opcode::CALL:
            // operandC is 1 for non-void (pushes return value) or 0 for void
            return static_cast<int>(operandC(instr)) - static_cast<int>(operandB(instr));
        case Opcode::UCALL:
            // Pops argc args, pushes 1 return value: net = 1 - argc
            return 1 - static_cast<int>(operandB(instr));
        case Opcode::URET:
            // Handled dynamically by restoring caller frame; for static analysis treat as 0
            return 0;
        case Opcode::UTCALL:
            // Tail call: handled dynamically (reuses frame). For static analysis treat as 0.
            return 0;
        case Opcode::IUCALL:
            // Pops callable + argc explicit args, pushes 1 return value: net = 1 - argc - 1
            return -static_cast<int>(operandA(instr));
        case Opcode::IUTCALL:
            // Indirect tail call: handled dynamically (reuses frame). For static analysis treat as 0.
            return 0;
        default: return instructionInfos[opc].stackChange;
    }
}

size_t computeStackSize(const Instruction* program, size_t programSize)
{
    const Instruction* i = program;
    const Instruction* e = program + programSize;
    int stackSize = 0;
    int limit = 0;

    while (i != e)
    {
        int change = getStackChange(*i);
        stackSize += change;
        limit = std::max(limit, stackSize);
        i++;
    }

    return static_cast<size_t>(limit);
}

OperandSig operandSignature(Opcode opc)
{
    return instructionInfos[(size_t) opc].operandSig;
}

const char* mnemonic(Opcode opc)
{
    return instructionInfos[(size_t) opc].mnemonic;
}

LiteralType resultType(Opcode opc)
{
    return instructionInfos[(size_t) opc].stackOutput;
}

// ---------------------------------------------------------------------------

std::string disassemble(const Instruction* program,
                        size_t n,
                        const std::string& indent,
                        const ConstantPool* cp)
{
    std::stringstream result;
    size_t i = 0;
    size_t sp = 0;
    for (const Instruction* pc = program; pc < program + n; ++pc)
    {
        result << indent;
        result << disassemble(*pc, i++, sp, cp);
        result << '\n';
        sp += getStackChange(*pc);
    }
    return result.str();
}

std::string disassemble(Instruction pc, size_t ip, size_t sp, const ConstantPool* cp)
{
    const Opcode opc = opcode(pc);
    const Operand A = operandA(pc);
    const Operand B = operandB(pc);
    const Operand C = operandC(pc);
    const char* mnemo = mnemonic(opc);
    std::stringstream line;
    size_t n = 0;

    std::string word = std::format("{:<10}", mnemo);
    line << word;
    n = word.size();

    // operands
    if (cp != nullptr)
    {
        switch (opc)
        {
            case Opcode::ITLOAD: {
                line << "[";
                n++;
                const std::vector<CoreNumber>& v = cp->getIntArray(A);
                for (size_t i = 0, e = v.size(); i != e; ++i)
                {
                    if (i)
                    {
                        line << ", ";
                        n += 2;
                    }
                    word = std::to_string(v[i]);
                    line << word;
                    n += word.size();
                }
                line << "]";
                n++;
                break;
            }
            case Opcode::STLOAD: {
                line << '[';
                n++;
                const std::vector<std::string>& v = cp->getStringArray(A);
                for (size_t i = 0, e = v.size(); i != e; ++i)
                {
                    if (i)
                    {
                        line << ", ";
                        n += 2;
                    }
                    line << '"' << v[i] << '"';
                    n += v[i].size() + 2;
                }
                line << ']';
                n += 1;
                break;
            }
            case Opcode::PTLOAD: {
                line << "[";
                n++;
                const std::vector<util::IPAddress>& v = cp->getIPAddressArray(A);
                for (size_t i = 0, e = v.size(); i != e; ++i)
                {
                    if (i)
                    {
                        line << ", ";
                        n += 2;
                    }
                    word = v[i].str();
                    line << word;
                    n += word.size();
                }
                line << "]";
                n++;
                break;
            }
            case Opcode::CTLOAD: {
                line << "[";
                n++;
                const std::vector<util::Cidr>& v = cp->getCidrArray(A);
                for (size_t i = 0, e = v.size(); i != e; ++i)
                {
                    if (i)
                    {
                        line << ", ";
                        n += 2;
                    }
                    word = v[i].str();
                    line << word;
                    n += word.size();
                }
                line << "]";
                n++;
                break;
            }
            case Opcode::LOAD:
                word = std::format("STACK[{}]", A);
                line << word;
                n += word.size();
                break;
            case Opcode::STORE:
                word = std::format("@STACK[{}]", A);
                line << word;
                n += word.size();
                break;
            case Opcode::NLOAD:
                word = std::to_string(cp->getInteger(A));
                line << word;
                n += word.size();
                break;
            case Opcode::SLOAD:
                word = std::format("\"{}\"", cp->getString(A));
                line << word;
                n += word.size();
                break;
            case Opcode::PLOAD:
                word = cp->getIPAddress(A).str();
                line << word;
                n += word.size();
                break;
            case Opcode::CLOAD:
                word = cp->getCidr(A).str();
                line << word;
                n += word.size();
                break;
            case Opcode::FLOAD:
                word = std::format("{:g}", cp->getFloat(A));
                line << word;
                n += word.size();
                break;
            case Opcode::CALL:
                word = cp->getNativeFunctionSignatures()[A];
                line << word;
                n += word.size();
                break;
            default:
                switch (operandSignature(opc))
                {
                    case OperandSig::III:
                        word = std::format("{}, {}, {}", A, B, C);
                        line << word;
                        n += word.size();
                        break;
                    case OperandSig::II:
                        word = std::format("{}, {}", A, B);
                        line << word;
                        n += word.size();
                        break;
                    case OperandSig::I:
                        word = std::format("{}", A);
                        line << word;
                        n += word.size();
                        break;
                    case OperandSig::V: break;
                }
                break;
        }
    }
    else
    {
        switch (operandSignature(opc))
        {
            case OperandSig::III:
                word = std::format("{}, {}, {}", A, B, C);
                line << word;
                n += word.size();
                break;
            case OperandSig::II:
                word = std::format("{}, {}", A, B);
                line << word;
                n += word.size();
                break;
            case OperandSig::I:
                word = std::format("{}", A);
                line << word;
                n += word.size();
                break;
            case OperandSig::V: break;
        }
    }

    while (n < 35)
    {
        line << ' ';
        n++;
    }

    int stackChange = getStackChange(pc);

    char sign = ' ';
    if (stackChange > 0)
        sign = '+';
    else if (stackChange < 0)
        sign = '-';

    word = std::format("; ip={:>3} sp={:>2} ({}{})", ip, sp, sign, std::abs(stackChange));
    line << word;

    return line.str();
}

} // namespace CoreVM
