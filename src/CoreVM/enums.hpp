// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <string>

namespace CoreVM
{

enum Opcode : uint16_t
{
    // misc
    NOP = 0,  // NOP                 ; no operation
    ALLOCA,   // ALLOCA imm         ; pushes A default-initialized items onto the stack
    DISCARD,  // DISCARD imm        ; pops A items from the stack
    STACKROT, // STACKROT imm       ; rotate stack at stack[imm], moving stack[imm] to top

    GALLOCA, // GALLOCA imm        ; appends A default-initialized items to the global scope
    GLOAD,   // GLOAD imm          ; stack[sp++] = globals[imm]
    GSTORE,  // GSTORE imm         ; globals[imm] = stack[--sp]

    // control
    EXIT,    // EXIT imm           ; exit program with constant exit code
    EXITPOP, // EXITPOP            ; exit program with exit code popped from stack
    JMP,     // JMP imm            ; unconditional jump to A
    JN,      // JN imm             ; conditional jump to A if (pop() != 0)
    JZ,      // JZ imm             ; conditional jump to A if (pop() == 0)

    // const arrays
    ITLOAD, // stack[sp++] = intArray[imm]
    STLOAD, // stack[sp++] = stringArray[imm]
    PTLOAD, // stack[sp++] = ipaddrArray[imm]
    CTLOAD, // stack[sp++] = cidrArray[imm]

    LOAD,  // LOAD imm, imm      ; stack[++op1] = stack[op2]
    STORE, // STORE imm, imm     ; stack[op1] = stack[op2]

    // numerical
    ILOAD,  // ILOAD imm
    NLOAD,  // NLOAD numberConstants[imm]
    NNEG,   //                    ; stack[SP] = -stack[SP]
    NNOT,   //                    ; stack[SP] = ~stack[SP]
    NADD,   //                    ; npush(npop() + npop())
    NSUB,   //                    ; npush(npop() - npop())
    NMUL,   //                    ; npush(npop() * npop())
    NDIV,   //                    ; npush(npop() / npop())
    NREM,   //                    ; npush(npop() % npop())
    NSHL,   // t = stack[SP-2] << stack[SP-1]; pop(2); npush(t);
    NSHR,   // A = B >> C
    NPOW,   // A = B ** C
    NAND,   // A = B & C
    NOR,    // A = B | C
    NXOR,   // A = B ^ C
    NCMPZ,  // A = B == 0
    NCMPEQ, // A = B == C
    NCMPNE, // A = B != C
    NCMPLE, // A = B <= C
    NCMPGE, // A = B >= C
    NCMPLT, // A = B < C
    NCMPGT, // A = B > C

    // boolean
    BNOT, // A = !A
    BAND, // A = B and C
    BOR,  // A = B or C
    BXOR, // A = B xor C

    // string
    SLOAD,     // SLOAD stringConstants[imm]
    SADD,      // b = pop(); a = pop(); push(a + b);
    SSUBSTR,   // A = substr(B, C /*offset*/, C+1 /*count*/)
    SCMPEQ,    // A = B == C
    SCMPNE,    // A = B != C
    SCMPLE,    // A = B <= C
    SCMPGE,    // A = B >= C
    SCMPLT,    // A = B < C
    SCMPGT,    // A = B > C
    SCMPBEG,   // A = B =^ C           /* B begins with C */
    SCMPEND,   // A = B =$ C           /* B ends with C */
    SCONTAINS, // A = B in C           /* B is contained in C */
    SLEN,      // A = strlen(B)
    SISEMPTY,  // A = strlen(B) == 0
    SMATCHEQ,  // $pc = MatchSame[A].evaluate(B);
    SMATCHBEG, // $pc = MatchBegin[A].evaluate(B);
    SMATCHEND, // $pc = MatchEnd[A].evaluate(B);
    SMATCHR,   // $pc = MatchRegEx[A].evaluate(B);

    // IP address
    PLOAD,   // PLOAD ipaddrConstants[imm]
    PCMPEQ,  // A = ip(B) == ip(C)
    PCMPNE,  // A = ip(B) != ip(C)
    PINCIDR, // A = cidr(C).contains(ip(B))

    // CIDR
    CLOAD, // CLOAD  cidrConstants[imm]

    // regex
    SREGMATCH, // A = B =~ C           /* regex match against regexPool[C] */
    SREGGROUP, // A = regex.match(B)   /* regex match result */

    // conversion
    N2S, // push(itoa(pop()))
    P2S, // push(ip(pop()).toString())
    C2S, // push(cidr(pop()).toString()
    R2S, // push(regex(pop()).toString()
    S2N, // push(atoi(pop()))

    // invocation
    // CALL A = id, B = argc
    CALL, // calls A with B arguments, always pushes result to stack

    // object operations (for composite types: Option, Result, tuples, closures, etc.)
    OALLOC,   // OALLOC typeId       ; allocate object of type typeId, push pointer
    ORETAIN,  // ORETAIN             ; increment refcount of object at top of stack
    ORELEASE, // ORELEASE            ; decrement refcount, free if zero
    OGETTAG,  // OGETTAG             ; push tag value from object (for sum types)
    OSETTAG,  // OSETTAG             ; pop tag, pop object, set tag on object
    OGETSLOT, // OGETSLOT imm        ; push slot[imm] from object at top of stack
    OSETSLOT, // OSETSLOT imm        ; pop value, pop object, set slot[imm] = value
    OTYPEID,  // OTYPEID             ; push type ID of object at top of stack
    OISTYPE,  // OISTYPE typeId      ; push (object.typeId == typeId)

    // dynamic value comparison (for pattern matching with unknown types)
    VCMPEQ, // VCMPEQ               ; A = pop() == pop() (compares as numbers)
    VCMPNE, // VCMPNE               ; A = pop() != pop() (compares as numbers)
    VCMPLT, // VCMPLT               ; A = pop() < pop() (compares as numbers)
    VCMPLE, // VCMPLE               ; A = pop() <= pop() (compares as numbers)
    VCMPGT, // VCMPGT               ; A = pop() > pop() (compares as numbers)
    VCMPGE, // VCMPGE               ; A = pop() >= pop() (compares as numbers)

    // float (double) — stored as bit_cast<uint64_t>(double) in stack slots
    FLOAD,  // FLOAD floatConstants[imm]
    FNEG,   //                    ; stack[SP] = -getFloat(SP)
    FADD,   //                    ; fpush(fpop() + fpop())
    FSUB,   //                    ; fpush(fpop() - fpop())
    FMUL,   //                    ; fpush(fpop() * fpop())
    FDIV,   //                    ; fpush(fpop() / fpop())
    FREM,   //                    ; fpush(fmod(fpop(), fpop()))
    FPOW,   //                    ; fpush(pow(fpop(), fpop()))
    FCMPEQ, //                    ; push(fpop() == fpop())
    FCMPNE, //                    ; push(fpop() != fpop())
    FCMPLE, //                    ; push(fpop() <= fpop())
    FCMPGE, //                    ; push(fpop() >= fpop())
    FCMPLT, //                    ; push(fpop() < fpop())
    FCMPGT, //                    ; push(fpop() > fpop())

    // float conversion
    N2F, // push(static_cast<double>(pop()))
    F2N, // push(static_cast<int64_t>(fpop()))
    F2S, // push(format("{:g}", fpop()))
    S2F, // push(stod(pop()))

    // user-defined function calls (frame-pointer based)
    UCALL,  // UCALL function_id, argc  ; call user function with argc args on stack
    URET,   // URET                    ; return from user function (top of stack = return value)
    UTCALL, // UTCALL function_id, argc ; tail-call user function (reuse current frame)

    // indirect user call (callable object on stack)
    IUCALL, // IUCALL argc             ; indirect call via Callable object on stack

    // lazy evaluation
    LFORCE, // LFORCE                  ; force lazy value at top of stack
};

enum class MatchClass
{
    Same,
    Head,
    Tail,
    RegExp,
};

std::string tos(MatchClass c);

enum class UnaryOperator
{
    // numerical
    INeg,
    INot,
    // float
    FNeg,
    // boolean
    BNot,
    // string
    SLen,
    SIsEmpty,
};

enum class BinaryOperator
{
    // numerical
    IAdd,
    ISub,
    IMul,
    IDiv,
    IRem,
    IPow,
    IAnd,
    IOr,
    IXor,
    IShl,
    IShr,
    ICmpEQ,
    ICmpNE,
    ICmpLE,
    ICmpGE,
    ICmpLT,
    ICmpGT,
    // boolean
    BAnd,
    BOr,
    BXor,
    // string
    SAdd,
    SSubStr,
    SCmpEQ,
    SCmpNE,
    SCmpLE,
    SCmpGE,
    SCmpLT,
    SCmpGT,
    SCmpRE,
    SCmpBeg,
    SCmpEnd,
    SIn,
    // float
    FAdd,
    FSub,
    FMul,
    FDiv,
    FRem,
    FPow,
    FCmpEQ,
    FCmpNE,
    FCmpLE,
    FCmpGE,
    FCmpLT,
    FCmpGT,
    // ip
    PCmpEQ,
    PCmpNE,
    PInCidr,
};

const char* cstr(BinaryOperator op);
const char* cstr(UnaryOperator op);

enum class LiteralType
{
    Void = 0,
    Boolean = 1,      // bool (int64)
    Number = 2,       // int64
    String = 3,       // std::string*
    IPAddress = 5,    // IPAddress*
    Cidr = 6,         // Cidr*
    RegExp = 7,       // RegExp*
    Function = 8,     // compiled function reference
    IntArray = 9,     // array<int>
    StringArray = 10, // array<string>
    IPAddrArray = 11, // array<IPAddress>
    CidrArray = 12,   // array<Cidr>
    IntPair = 13,     // array<int, 2>
    Option = 14,      // Option<T>: (tag: 0=None|1=Some, value: T) [deprecated, use Object]
    Result = 15,      // Result<T,E>: (tag: 0=Error|1=Ok, payload: T|E) [deprecated, use Object]
    Object = 16,      // TypedObject*: pointer to heap-allocated typed object
    Float = 17,       // double (stored as bit_cast<uint64_t>)
};

template <const UnaryOperator Operator, const LiteralType ResultType>
class UnaryInstr;

template <const BinaryOperator Operator, const LiteralType ResultType>
class BinaryInstr;

// numeric
using INegInstr = UnaryInstr<UnaryOperator::INeg, LiteralType::Number>;
using INotInstr = UnaryInstr<UnaryOperator::INot, LiteralType::Number>;
using IAddInstr = BinaryInstr<BinaryOperator::IAdd, LiteralType::Number>;
using ISubInstr = BinaryInstr<BinaryOperator::ISub, LiteralType::Number>;
using IMulInstr = BinaryInstr<BinaryOperator::IMul, LiteralType::Number>;
using IDivInstr = BinaryInstr<BinaryOperator::IDiv, LiteralType::Number>;
using IRemInstr = BinaryInstr<BinaryOperator::IRem, LiteralType::Number>;
using IPowInstr = BinaryInstr<BinaryOperator::IPow, LiteralType::Number>;
using IAndInstr = BinaryInstr<BinaryOperator::IAnd, LiteralType::Number>;
using IOrInstr = BinaryInstr<BinaryOperator::IOr, LiteralType::Number>;
using IXorInstr = BinaryInstr<BinaryOperator::IXor, LiteralType::Number>;
using IShlInstr = BinaryInstr<BinaryOperator::IShl, LiteralType::Number>;
using IShrInstr = BinaryInstr<BinaryOperator::IShr, LiteralType::Number>;

using ICmpEQInstr = BinaryInstr<BinaryOperator::ICmpEQ, LiteralType::Boolean>;
using ICmpNEInstr = BinaryInstr<BinaryOperator::ICmpNE, LiteralType::Boolean>;
using ICmpLEInstr = BinaryInstr<BinaryOperator::ICmpLE, LiteralType::Boolean>;
using ICmpGEInstr = BinaryInstr<BinaryOperator::ICmpGE, LiteralType::Boolean>;
using ICmpLTInstr = BinaryInstr<BinaryOperator::ICmpLT, LiteralType::Boolean>;
using ICmpGTInstr = BinaryInstr<BinaryOperator::ICmpGT, LiteralType::Boolean>;

// binary
using BNotInstr = UnaryInstr<UnaryOperator::BNot, LiteralType::Boolean>;
using BAndInstr = BinaryInstr<BinaryOperator::BAnd, LiteralType::Boolean>;
using BOrInstr = BinaryInstr<BinaryOperator::BOr, LiteralType::Boolean>;
using BXorInstr = BinaryInstr<BinaryOperator::BXor, LiteralType::Boolean>;

// string
using SLenInstr = UnaryInstr<UnaryOperator::SLen, LiteralType::Number>;
using SIsEmptyInstr = UnaryInstr<UnaryOperator::SIsEmpty, LiteralType::Boolean>;
using SAddInstr = BinaryInstr<BinaryOperator::SAdd, LiteralType::String>;
using SSubStrInstr = BinaryInstr<BinaryOperator::SSubStr, LiteralType::String>;
using SCmpEQInstr = BinaryInstr<BinaryOperator::SCmpEQ, LiteralType::Boolean>;
using SCmpNEInstr = BinaryInstr<BinaryOperator::SCmpNE, LiteralType::Boolean>;
using SCmpLEInstr = BinaryInstr<BinaryOperator::SCmpLE, LiteralType::Boolean>;
using SCmpGEInstr = BinaryInstr<BinaryOperator::SCmpGE, LiteralType::Boolean>;
using SCmpLTInstr = BinaryInstr<BinaryOperator::SCmpLT, LiteralType::Boolean>;
using SCmpGTInstr = BinaryInstr<BinaryOperator::SCmpGT, LiteralType::Boolean>;
using SCmpREInstr = BinaryInstr<BinaryOperator::SCmpRE, LiteralType::Boolean>;
using SCmpBegInstr = BinaryInstr<BinaryOperator::SCmpBeg, LiteralType::Boolean>;
using SCmpEndInstr = BinaryInstr<BinaryOperator::SCmpEnd, LiteralType::Boolean>;
using SInInstr = BinaryInstr<BinaryOperator::SIn, LiteralType::Boolean>;

// ip
using PCmpEQInstr = BinaryInstr<BinaryOperator::PCmpEQ, LiteralType::Boolean>;
using PCmpNEInstr = BinaryInstr<BinaryOperator::PCmpNE, LiteralType::Boolean>;
using PInCidrInstr = BinaryInstr<BinaryOperator::PInCidr, LiteralType::Boolean>;

// float
using FNegInstr = UnaryInstr<UnaryOperator::FNeg, LiteralType::Float>;
using FAddInstr = BinaryInstr<BinaryOperator::FAdd, LiteralType::Float>;
using FSubInstr = BinaryInstr<BinaryOperator::FSub, LiteralType::Float>;
using FMulInstr = BinaryInstr<BinaryOperator::FMul, LiteralType::Float>;
using FDivInstr = BinaryInstr<BinaryOperator::FDiv, LiteralType::Float>;
using FRemInstr = BinaryInstr<BinaryOperator::FRem, LiteralType::Float>;
using FPowInstr = BinaryInstr<BinaryOperator::FPow, LiteralType::Float>;
using FCmpEQInstr = BinaryInstr<BinaryOperator::FCmpEQ, LiteralType::Boolean>;
using FCmpNEInstr = BinaryInstr<BinaryOperator::FCmpNE, LiteralType::Boolean>;
using FCmpLEInstr = BinaryInstr<BinaryOperator::FCmpLE, LiteralType::Boolean>;
using FCmpGEInstr = BinaryInstr<BinaryOperator::FCmpGE, LiteralType::Boolean>;
using FCmpLTInstr = BinaryInstr<BinaryOperator::FCmpLT, LiteralType::Boolean>;
using FCmpGTInstr = BinaryInstr<BinaryOperator::FCmpGT, LiteralType::Boolean>;

enum class OperandSig
{
    V,   // no operands
    I,   // imm16
    II,  // imm16, imm16
    III, // imm16, imm16, imm16
};

// --------------------------------------------------------------------------
// opcode pricing

constexpr inline unsigned getPrice(Opcode opcode)
{
    switch (opcode)
    {
        case Opcode::EXIT:
        case Opcode::EXITPOP: return 0;
        case Opcode::JMP: return 1;
        case Opcode::JN:
        case Opcode::JZ: return 2;
        case Opcode::GSTORE:
        case Opcode::STORE: return 4;
        case Opcode::CALL:
        case Opcode::UCALL:
        case Opcode::UTCALL:
        case Opcode::IUCALL: return 8;
        case Opcode::URET: return 1;
        default: return 1;
    }
}

// --------------------------------------------------------------------------
// encoder

using Instruction = uint64_t;
using Operand = uint16_t;

/** Creates an instruction with no operands. */
constexpr Instruction makeInstruction(Opcode opc)
{
    return (Instruction) opc;
}

/** Creates an instruction with one operand. */
constexpr Instruction makeInstruction(Opcode opc, Operand op1)
{
    return (opc | (op1 << 16));
}

/** Creates an instruction with two operands. */
constexpr Instruction makeInstruction(Opcode opc, Operand op1, Operand op2)
{
    return (opc | (op1 << 16) | (Instruction(op2) << 32));
}

/** Creates an instruction with three operands. */
constexpr Instruction makeInstruction(Opcode opc, Operand op1, Operand op2, Operand op3)
{
    return (opc | (op1 << 16) | (Instruction(op2) << 32) | (Instruction(op3) << 48));
}

// --------------------------------------------------------------------------
// decoder

/** decodes the opcode from the instruction. */
constexpr Opcode opcode(Instruction instr)
{
    return static_cast<Opcode>(instr & 0xFF);
}

/** Decodes the first operand from the instruction. */
constexpr Operand operandA(Instruction instr)
{
    return static_cast<Operand>((instr >> 16) & 0xFFFF);
}

/** Decodes the second operand from the instruction. */
constexpr Operand operandB(Instruction instr)
{
    return static_cast<Operand>((instr >> 32) & 0xFFFF);
}

/** Decodes the third operand from the instruction. */
constexpr Operand operandC(Instruction instr)
{
    return static_cast<Operand>((instr >> 48) & 0xFFFF);
}

/** Determines the operand signature of the given instruction. */
OperandSig operandSignature(Opcode opc);

/** Returns the mnemonic string representing the opcode. */
const char* mnemonic(Opcode opc);

/**
 * Determines the data type of the result being pushed onto the stack, if any.
 */
LiteralType resultType(Opcode opc);

/**
 * Computes the stack height after the execution of the given instruction.
 */
int getStackChange(Instruction instr);

/**
 * Computes the highest stack size needed to run the given program.
 */
size_t computeStackSize(const Instruction* program, size_t programSize);

} // namespace CoreVM
