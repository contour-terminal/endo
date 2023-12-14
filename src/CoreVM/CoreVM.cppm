// SPDX-License-Identifier: Apache-2.0
module;

#include <CoreVM/util/IPAddress.h>
#include <CoreVM/util/unbox.h>
#include <CoreVM/util/PrefixTree.h>
#include <CoreVM/util/SuffixTree.h>

#include <functional>
#include <vector>
#include <optional>
#include <string>
#include <variant>
#include <vector>
#include <regex>
#include <list>
#include <cassert>

export module CoreVM;

export namespace CoreVM::util
{

/**
 * @brief CIDR network notation object.
 *
 * @see IPAddress
 */
class Cidr
{
  public:
    /**
     * @brief Initializes an empty cidr notation.
     *
     * e.g. 0.0.0.0/0
     */
    Cidr(): _ipaddr(), _prefix(0) {}

    /**
     * @brief Initializes this CIDR notation with given IP address and prefix.
     */
    Cidr(const char* ipaddress, size_t prefix): _ipaddr(ipaddress), _prefix(prefix) {}

    /**
     * @brief Initializes this CIDR notation with given IP address and prefix.
     */
    Cidr(const IPAddress& ipaddress, size_t prefix): _ipaddr(ipaddress), _prefix(prefix) {}

    /**
     * @brief Retrieves the address part of this CIDR notation.
     */
    const IPAddress& address() const { return _ipaddr; }

    /**
     * @brief Sets the address part of this CIDR notation.
     */
    bool setAddress(const std::string& text, IPAddress::Family family)
    {
        return _ipaddr.assign(text, family);
    }

    /**
     * @brief Retrieves the prefix part of this CIDR notation.
     */
    size_t prefix() const { return _prefix; }

    /**
     * @brief Sets the prefix part of this CIDR notation.
     */
    void setPrefix(size_t n) { _prefix = n; }

    /**
     * @brief Retrieves the string-form of this network in CIDR notation.
     */
    std::string str() const;

    /**
     * @brief test whether or not given IP address is inside the network.
     *
     * @retval true it is inside this network.
     * @retval false it is not inside this network.
     */
    bool contains(const IPAddress& ipaddr) const;

    /**
     * @brief compares 2 CIDR notations for equality.
     */
    friend bool operator==(const Cidr& a, const Cidr& b);

    /**
     * @brief compares 2 CIDR notations for inequality.
     */
    friend bool operator!=(const Cidr& a, const Cidr& b);

  private:
    IPAddress _ipaddr;
    size_t _prefix;
};

class BufferRef;

class RegExp
{
  private:
    std::string _pattern;
    std::regex _re;

  public:
    using Result = std::smatch;

  public:
    explicit RegExp(std::string const& pattern);
    RegExp() = default;
    RegExp(RegExp const& v) = default;
    ~RegExp() = default;

    RegExp(RegExp&& v) noexcept = default;
    RegExp& operator=(RegExp&& v) noexcept = default;

    bool match(const std::string& target, Result* result = nullptr) const;

    [[nodiscard]] const std::string& pattern() const { return _pattern; }
    [[nodiscard]] const char* c_str() const;

    operator const std::string&() const { return _pattern; }

    friend bool operator==(const RegExp& a, const RegExp& b) { return a.pattern() == b.pattern(); }
    friend bool operator!=(const RegExp& a, const RegExp& b) { return a.pattern() != b.pattern(); }
    friend bool operator<=(const RegExp& a, const RegExp& b) { return a.pattern() <= b.pattern(); }
    friend bool operator>=(const RegExp& a, const RegExp& b) { return a.pattern() >= b.pattern(); }
    friend bool operator<(const RegExp& a, const RegExp& b) { return a.pattern() < b.pattern(); }
    friend bool operator>(const RegExp& a, const RegExp& b) { return a.pattern() > b.pattern(); }
};

class RegExpContext
{
  public:
    const RegExp::Result* regexMatch() const
    {
        if (!_regexMatch)
            _regexMatch = std::make_unique<RegExp::Result>();

        return _regexMatch.get();
    }

    RegExp::Result* regexMatch()
    {
        if (!_regexMatch)
            _regexMatch = std::make_unique<RegExp::Result>();

        return _regexMatch.get();
    }

  private:
    mutable std::unique_ptr<RegExp::Result> _regexMatch;
};

} // namespace CoreVM::util


export namespace CoreVM::diagnostics
{
    class Report;
}


export namespace CoreVM
{

class Instr;
class IRBuilder;
class IRProgram;
class NativeCallback;
class Value;
class Constant;
class BasicBlock;
class IRHandler;
class IRProgram;
class IRBuilder;
class IRBuiltinHandler;
class IRBuiltinFunction;
class AllocaInstr;
class MatchInstr;
class InstructionVisitor;
class ConstantPool;
class Program;
class Handler;
class Runner;
class Runtime;


using Quota = int64_t;
constexpr Quota NoQuota = -1;


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
    EXIT, // EXIT imm           ; exit program
    JMP,  // JMP imm            ; unconditional jump to A
    JN,   // JN imm             ; conditional jump to A if (pop() != 0)
    JZ,   // JZ imm             ; conditional jump to A if (pop() == 0)

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

    // invokation
    // CALL A = id, B = argc
    CALL,    // calls A with B arguments, always pushes result to stack
    HANDLER, // calls A with B arguments (never leaves result on stack)
};

using Instruction = uint64_t;
using Operand = uint16_t;


enum class MatchClass
{
    Same,
    Head,
    Tail,
    RegExp,
};


std::string tos(MatchClass c);


enum class LiteralType
{
    Void = 0,
    Boolean = 1,      // bool (int64)
    Number = 2,       // int64
    String = 3,       // std::string*
    IPAddress = 5,    // IPAddress*
    Cidr = 6,         // Cidr*
    RegExp = 7,       // RegExp*
    Handler = 8,      // bool (*native_handler)(CoreContext*);
    IntArray = 9,     // array<int>
    StringArray = 10, // array<string>
    IPAddrArray = 11, // array<IPAddress>
    CidrArray = 12,   // array<Cidr>
    IntPair = 13,     // array<int, 2>
};



enum class UnaryOperator
{
    // numerical
    INeg,
    INot,
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
    // ip
    PCmpEQ,
    PCmpNE,
    PInCidr,
};

const char* cstr(BinaryOperator op);
const char* cstr(UnaryOperator op);

class NopInstr;
class AllocaInstr;
class StoreInstr;
class LoadInstr;
class CallInstr;
class HandlerCallInstr;
class PhiNode;
class CondBrInstr;
class BrInstr;
class RetInstr;
class MatchInstr;
class RegExpGroupInstr;
class CastInstr;

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
        case Opcode::EXIT: return 0;
        case Opcode::JMP: return 1;
        case Opcode::JN:
        case Opcode::JZ: return 2;
        case Opcode::GSTORE:
        case Opcode::STORE: return 4;
        case Opcode::CALL:
        case Opcode::HANDLER: return 8;
        default: return 1;
    }
}

// --------------------------------------------------------------------------
// encoder


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


using Register = uint64_t; // vm

using CoreNumber = int64_t;
using CoreString = std::string;

std::string tos(LiteralType type);

bool isArrayType(LiteralType type);
LiteralType elementTypeOf(LiteralType type);


class InstructionVisitor
{
  public:
    virtual ~InstructionVisitor() = default;

    virtual void visit(NopInstr& instr) = 0;

    // storage
    virtual void visit(AllocaInstr& instr) = 0;
    virtual void visit(StoreInstr& instr) = 0;
    virtual void visit(LoadInstr& instr) = 0;
    virtual void visit(PhiNode& instr) = 0;

    // calls
    virtual void visit(CallInstr& instr) = 0;
    virtual void visit(HandlerCallInstr& instr) = 0;

    // terminator
    virtual void visit(CondBrInstr& instr) = 0;
    virtual void visit(BrInstr& instr) = 0;
    virtual void visit(RetInstr& instr) = 0;
    virtual void visit(MatchInstr& instr) = 0;

    // regexp
    virtual void visit(RegExpGroupInstr& instr) = 0;

    // type cast
    virtual void visit(CastInstr& instr) = 0;

    // numeric
    virtual void visit(INegInstr& instr) = 0;
    virtual void visit(INotInstr& instr) = 0;
    virtual void visit(IAddInstr& instr) = 0;
    virtual void visit(ISubInstr& instr) = 0;
    virtual void visit(IMulInstr& instr) = 0;
    virtual void visit(IDivInstr& instr) = 0;
    virtual void visit(IRemInstr& instr) = 0;
    virtual void visit(IPowInstr& instr) = 0;
    virtual void visit(IAndInstr& instr) = 0;
    virtual void visit(IOrInstr& instr) = 0;
    virtual void visit(IXorInstr& instr) = 0;
    virtual void visit(IShlInstr& instr) = 0;
    virtual void visit(IShrInstr& instr) = 0;
    virtual void visit(ICmpEQInstr& instr) = 0;
    virtual void visit(ICmpNEInstr& instr) = 0;
    virtual void visit(ICmpLEInstr& instr) = 0;
    virtual void visit(ICmpGEInstr& instr) = 0;
    virtual void visit(ICmpLTInstr& instr) = 0;
    virtual void visit(ICmpGTInstr& instr) = 0;

    // boolean
    virtual void visit(BNotInstr& instr) = 0;
    virtual void visit(BAndInstr& instr) = 0;
    virtual void visit(BOrInstr& instr) = 0;
    virtual void visit(BXorInstr& instr) = 0;

    // string
    virtual void visit(SLenInstr& instr) = 0;
    virtual void visit(SIsEmptyInstr& instr) = 0;
    virtual void visit(SAddInstr& instr) = 0;
    virtual void visit(SSubStrInstr& instr) = 0;
    virtual void visit(SCmpEQInstr& instr) = 0;
    virtual void visit(SCmpNEInstr& instr) = 0;
    virtual void visit(SCmpLEInstr& instr) = 0;
    virtual void visit(SCmpGEInstr& instr) = 0;
    virtual void visit(SCmpLTInstr& instr) = 0;
    virtual void visit(SCmpGTInstr& instr) = 0;
    virtual void visit(SCmpREInstr& instr) = 0;
    virtual void visit(SCmpBegInstr& instr) = 0;
    virtual void visit(SCmpEndInstr& instr) = 0;
    virtual void visit(SInInstr& instr) = 0;

    // ip
    virtual void visit(PCmpEQInstr& instr) = 0;
    virtual void visit(PCmpNEInstr& instr) = 0;
    virtual void visit(PInCidrInstr& instr) = 0;
};



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



struct FilePos
{ // {{{
    FilePos(): FilePos { 1, 1, 0 } {}
    FilePos(unsigned r, unsigned c): FilePos { r, c, 0 } {}
    FilePos(unsigned r, unsigned c, unsigned o): line(r), column(c), offset(o) {}

    FilePos& set(unsigned r, unsigned c, unsigned o)
    {
        line = r;
        column = c;
        offset = o;

        return *this;
    }

    void advance(char ch)
    {
        offset++;
        if (ch != '\n')
        {
            column++;
        }
        else
        {
            line++;
            column = 1;
        }
    }

    bool operator==(const FilePos& other) const noexcept
    {
        return line == other.line && column == other.column && offset == other.offset;
    }

    bool operator!=(const FilePos& other) const noexcept { return !(*this == other); }

    unsigned line;
    unsigned column;
    unsigned offset;
};

inline size_t operator-(const FilePos& a, const FilePos& b)
{
    if (b.offset > a.offset)
        return 1 + b.offset - a.offset;
    else
        return 1 + a.offset - b.offset;
}
// }}}


// ExecutionEngine
// VM
class Runner
{
  public:
    enum State
    {
        Inactive,  //!< No handler running nor suspended.
        Running,   //!< Active handler is currently running.
        Suspended, //!< Active handler is currently suspended.
    };

    class QuotaExceeded: public std::runtime_error
    {
      public:
        QuotaExceeded(): std::runtime_error { "CoreVM runtime quota exceeded." } {}
    };

    using Value = uint64_t;
    using Globals = std::vector<Value>;

    using TraceLogger = std::function<void(Instruction instr, size_t ip, size_t sp)>;

    class Stack
    { // {{{
      public:
        explicit Stack(size_t stackSize) { _stack.reserve(stackSize); }

        void push(Value value) { _stack.push_back(value); }

        Value pop()
        {
            Value v = _stack.back();
            _stack.pop_back();
            return v;
        }

        void discard(size_t n)
        {
            n = std::min(n, _stack.size());
            _stack.resize(_stack.size() - n);
        }

        void rotate(size_t n);

        size_t size() const { return _stack.size(); }

        Value operator[](int relativeIndex) const
        {
            if (relativeIndex < 0)
            {
                return _stack[_stack.size() + relativeIndex];
            }
            else
            {
                return _stack[relativeIndex];
            }
        }

        Value& operator[](int relativeIndex)
        {
            if (relativeIndex < 0)
            {
                return _stack[_stack.size() + relativeIndex];
            }
            else
            {
                return _stack[relativeIndex];
            }
        }

        Value operator[](size_t absoluteIndex) const { return _stack[absoluteIndex]; }

        Value& operator[](size_t absoluteIndex) { return _stack[absoluteIndex]; }

      private:
        std::vector<Value> _stack;
    };
    // }}}

  public:
    Runner(const Handler* handler, void* userdata, Globals* globals, TraceLogger logger);
    Runner(const Handler* handler, void* userdata, Globals* globals, Quota quota, TraceLogger logger);
    ~Runner() = default;

    const Handler* handler() const noexcept { return _handler; }
    const Program* program() const noexcept { return _program; }
    void* userdata() const noexcept { return _userdata; }

    bool run();
    void suspend();
    bool resume();
    void rewind();

    /** Retrieves the last saved program execution offset. */
    size_t getInstructionPointer() const noexcept { return _ip; }

    /** Retrieves number of elements on stack. */
    size_t getStackPointer() const noexcept { return _stack.size(); }

    const util::RegExpContext* regexpContext() const noexcept { return &_regexpContext; }

    CoreString* newString(std::string value);

  private:
    //! consumes @p tokens from quota and raises QuotaExceeded if quota is being exceeded.
    void consume(Opcode op);

    //! retrieves a pointer to a an empty string constant.
    const CoreString* emptyString() const { return &*_stringGarbage.begin(); }

    CoreString* catString(const CoreString& a, const CoreString& b);

    const Stack& stack() const noexcept { return _stack; }
    Value stack(int si) const { return _stack[si]; }

    CoreNumber getNumber(int si) const { return static_cast<CoreNumber>(_stack[si]); }
    const CoreString& getString(int si) const { return *(CoreString*) _stack[si]; }
    const util::IPAddress& getIPAddress(int si) const { return *(util::IPAddress*) _stack[si]; }
    const util::Cidr& getCidr(int si) const { return *(util::Cidr*) _stack[si]; }
    const util::RegExp& getRegExp(int si) const { return *(util::RegExp*) _stack[si]; }

    const CoreString* getStringPtr(int si) const { return (CoreString*) _stack[si]; }
    const util::Cidr* getCidrPtr(int si) const { return (util::Cidr*) _stack[si]; }

    void push(Value value) { _stack.push(value); }
    Value pop() { return _stack.pop(); }
    void discard(size_t n) { _stack.discard(n); }
    void pushString(const CoreString* value) { push((Value) value); }

    bool loop();

    Runner(Runner&) = delete;
    Runner& operator=(Runner&) = delete;

  private:
    Quota _quota;
    const Handler* _handler;
    TraceLogger _traceLogger;

    /**
     * We especially keep this ref to prevent ensure handler has
     * access to the program until the end, which is not garranteed
     * as it is only having a weak reference to the program (to avoid cycling
     * references).
     */
    const Program* _program;

    //! pointer to the currently evaluated HttpRequest/HttpResponse our case
    void* _userdata;

    util::RegExpContext _regexpContext;

    State _state; //!< current VM state
    size_t _ip;   //!< last saved program execution offset

    Stack _stack; //!< runtime stack

    Globals& _globals; //!< runtime global scope

    std::list<std::string> _stringGarbage;
};



struct MatchCaseDef
{
    //!< offset into the string pool (or regexp pool) of the associated program.
    uint64_t label;
    //!< program offset into the associated handler
    uint64_t pc;

    MatchCaseDef() = default;
    explicit MatchCaseDef(uint64_t l): label(l), pc(0) {}
    MatchCaseDef(uint64_t l, uint64_t p): label(l), pc(p) {}
};

class MatchDef
{
  public:
    size_t handlerId;
    MatchClass op; // == =^ =$ =~
    uint64_t elsePC;
    std::vector<MatchCaseDef> cases;
};


class Match
{
  public:
    explicit Match(MatchDef def);
    virtual ~Match() = default;

    const MatchDef& def() const { return _def; }

    /**
     * Matches input condition.
     * \return a code pointer to continue processing
     */
    virtual uint64_t evaluate(const CoreString* condition, Runner* env) const = 0;

  protected:
    MatchDef _def;
};


/**
 * Provides a pool of constant that can be built dynamically during code
 * generation and accessed effeciently at runtime.
 *
 * @see Program
 */
class ConstantPool
{
  public:
    using Code = std::vector<Instruction>;

    ConstantPool(const ConstantPool& v) = delete;
    ConstantPool& operator=(const ConstantPool& v) = delete;

    ConstantPool() = default;
    ConstantPool(ConstantPool&& from) noexcept = default;
    ConstantPool& operator=(ConstantPool&& v) noexcept = default;

    // builder
    size_t makeInteger(CoreNumber value);
    size_t makeString(const std::string& value);
    size_t makeIPAddress(const util::IPAddress& value);
    size_t makeCidr(const util::Cidr& value);
    size_t makeRegExp(const util::RegExp& value);

    size_t makeIntegerArray(const std::vector<CoreNumber>& elements);
    size_t makeStringArray(const std::vector<std::string>& elements);
    size_t makeIPaddrArray(const std::vector<util::IPAddress>& elements);
    size_t makeCidrArray(const std::vector<util::Cidr>& elements);

    size_t makeMatchDef();
    MatchDef& getMatchDef(size_t id) { return _matchDefs[id]; }

    size_t makeNativeHandler(const std::string& sig);
    size_t makeNativeHandler(const IRBuiltinHandler* handler);
    size_t makeNativeFunction(const std::string& sig);
    size_t makeNativeFunction(const IRBuiltinFunction* function);

    size_t makeHandler(const std::string& handlerName);
    size_t makeHandler(const IRHandler* handler);

    void setModules(const std::vector<std::pair<std::string, std::string>>& modules) { _modules = modules; }

    // accessor
    CoreNumber getInteger(size_t id) const { return _numbers[id]; }
    const CoreString& getString(size_t id) const { return _strings[id]; }
    const util::IPAddress& getIPAddress(size_t id) const { return _ipaddrs[id]; }
    const util::Cidr& getCidr(size_t id) const { return _cidrs[id]; }
    const util::RegExp& getRegExp(size_t id) const { return _regularExpressions[id]; }

    const std::vector<CoreNumber>& getIntArray(size_t id) const { return _intArrays[id]; }
    const std::vector<std::string>& getStringArray(size_t id) const { return _stringArrays[id]; }
    const std::vector<util::IPAddress>& getIPAddressArray(size_t id) const { return _ipaddrArrays[id]; }
    const std::vector<util::Cidr>& getCidrArray(size_t id) const { return _cidrArrays[id]; }

    const MatchDef& getMatchDef(size_t id) const { return _matchDefs[id]; }

    const std::pair<std::string, Code>& getHandler(size_t id) const { return _handlers[id]; }
    std::pair<std::string, Code>& getHandler(size_t id) { return _handlers[id]; }

    size_t setHandler(const std::string& name, Code&& code)
    {
        auto id = makeHandler(name);
        _handlers[id].second = std::move(code);
        return id;
    }

    // bulk accessors
    const std::vector<std::pair<std::string, std::string>>& getModules() const { return _modules; }
    const std::vector<std::pair<std::string, Code>>& getHandlers() const { return _handlers; }
    const std::vector<MatchDef>& getMatchDefs() const { return _matchDefs; }
    const std::vector<std::string>& getNativeHandlerSignatures() const { return _nativeHandlerSignatures; }
    const std::vector<std::string>& getNativeFunctionSignatures() const { return _nativeFunctionSignatures; }

    void dump() const;

  private:
    // constant primitives
    std::vector<CoreNumber> _numbers;
    std::vector<std::string> _strings;
    std::vector<util::IPAddress> _ipaddrs;
    std::vector<util::Cidr> _cidrs;
    std::vector<util::RegExp> _regularExpressions;

    // constant arrays
    std::vector<std::vector<CoreNumber>> _intArrays;
    std::vector<std::vector<std::string>> _stringArrays;
    std::vector<std::vector<util::IPAddress>> _ipaddrArrays;
    std::vector<std::vector<util::Cidr>> _cidrArrays;

    // code data
    std::vector<std::pair<std::string, std::string>> _modules;
    std::vector<std::pair<std::string, Code>> _handlers;
    std::vector<MatchDef> _matchDefs;
    std::vector<std::string> _nativeHandlerSignatures;
    std::vector<std::string> _nativeFunctionSignatures;
};


class Program
{
  public:
    explicit Program(ConstantPool&& cp);
    Program(Program&) = delete;
    Program& operator=(Program&) = delete;
    ~Program() = default;

    const ConstantPool& constants() const noexcept { return _cp; }
    ConstantPool& constants() noexcept { return _cp; }

    // accessors to linked data
    const Match* match(size_t index) const { return _matches[index].get(); }
    Handler* handler(size_t index) const;
    NativeCallback* nativeHandler(size_t index) const { return _nativeHandlers[index]; }
    NativeCallback* nativeFunction(size_t index) const { return _nativeFunctions[index]; }

    // bulk accessors
    auto matches() { return util::unbox(_matches); }

    std::vector<std::string> handlerNames() const;
    int indexOf(const Handler* handler) const noexcept;
    Handler* findHandler(const std::string& name) const noexcept;

    bool link(Runtime* runtime, diagnostics::Report* report);

    void dump();

  private:
    void setup();

    // builders
    using Code = ConstantPool::Code;
    Handler* createHandler(const std::string& name);
    Handler* createHandler(const std::string& name, const Code& instructions);

  private:
    ConstantPool _cp;

    // linked data
    Runtime* _runtime;
    mutable std::vector<std::unique_ptr<Handler>> _handlers;
    std::vector<std::unique_ptr<Match>> _matches;
    std::vector<NativeCallback*> _nativeHandlers;
    std::vector<NativeCallback*> _nativeFunctions;
};


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
    [[nodiscard]] const std::vector<uint64_t>& directThreadedCode() const noexcept
    {
        return _directThreadedCode;
    }
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


class Params
{
  public:
    using Value = Runner::Value;

    Params(Runner* caller, int argc): _caller(caller), _argc(argc), _argv(argc + 1) {}

    void setArg(int argi, Value value) { _argv[argi] = value; }

    [[nodiscard]] Runner* caller() const { return _caller; }

    void setResult(bool value) { _argv[0] = value; }
    void setResult(CoreNumber value) { _argv[0] = (Value) value; }
    void setResult(const Handler* handler) { _argv[0] = _caller->program()->indexOf(handler); }
    void setResult(const char* str) { _argv[0] = (Value) _caller->newString(str); }
    void setResult(std::string str) { _argv[0] = (Value) _caller->newString(std::move(str)); }
    void setResult(const CoreString* str) { _argv[0] = (Value) str; }
    void setResult(const util::IPAddress* ip) { _argv[0] = (Value) ip; }
    void setResult(const util::Cidr* cidr) { _argv[0] = (Value) cidr; }

    [[deprecated("Use count()")]] [[nodiscard]] int size() const { return _argc; }
    [[nodiscard]] int count() const { return _argc; }

    [[nodiscard]] Value at(size_t i) const { return _argv[i]; }
    [[nodiscard]] Value operator[](size_t i) const { return _argv[i]; }
    [[nodiscard]] Value& operator[](size_t i) { return _argv[i]; }

    [[nodiscard]] bool getBool(size_t offset) const { return at(offset); }
    [[nodiscard]] CoreNumber getInt(size_t offset) const { return at(offset); }
    [[nodiscard]] const CoreString& getString(size_t offset) const { return *(CoreString*) at(offset); }
    [[nodiscard]] Handler* getHandler(size_t offset) const
    {
        return _caller->program()->handler(static_cast<size_t>(at(offset)));
    }
    [[nodiscard]] const util::IPAddress& getIPAddress(size_t offset) const
    {
        return *(util::IPAddress*) at(offset);
    }
    [[nodiscard]] const util::Cidr& getCidr(size_t offset) const { return *(util::Cidr*) at(offset); }

    [[nodiscard]] const CoreIntArray& getIntArray(size_t offset) const { return *(CoreIntArray*) at(offset); }
    [[nodiscard]] const CoreStringArray& getStringArray(size_t offset) const
    {
        return *(CoreStringArray*) at(offset);
    }
    [[nodiscard]] const CoreIPAddrArray& getIPAddressArray(size_t offset) const
    {
        return *(CoreIPAddrArray*) at(offset);
    }
    [[nodiscard]] const CoreCidrArray& getCidrArray(size_t offset) const
    {
        return *(CoreCidrArray*) at(offset);
    }

    class iterator // NOLINT
    {              // {{{
      private:
        Params* _params;
        size_t _current;

      public:
        iterator(Params* p, size_t init): _params(p), _current(init) {}
        iterator(const iterator& v) = default;

        [[nodiscard]] size_t offset() const { return _current; }
        [[nodiscard]] Value get() const { return _params->at(_current); }

        [[nodiscard]] Value& operator*() { return _params->_argv[_current]; }
        [[nodiscard]] const Value& operator*() const { return _params->_argv[_current]; }

        iterator& operator++()
        {
            if (_current != static_cast<decltype(_current)>(_params->_argc))
            {
                ++_current;
            }
            return *this;
        }

        [[nodiscard]] bool operator==(const iterator& other) const { return _current == other._current; }

        [[nodiscard]] bool operator!=(const iterator& other) const { return _current != other._current; }
    }; // }}}

    [[nodiscard]] iterator begin() { return iterator(this, std::min(1, _argc)); }
    [[nodiscard]] iterator end() { return iterator(this, _argc); }

  private:
    Runner* _caller;
    int _argc;
    std::vector<Value> _argv;
};



enum class Attribute : unsigned
{
    Experimental = 0x0001,   // implementation is experimental, hence, parser can warn on use
    NoReturn = 0x0002,       // implementation never returns to program code
    SideEffectFree = 0x0004, // implementation is side effect free
};


class Signature
{
  private:
    std::string _name;
    LiteralType _returnType;
    std::vector<LiteralType> _args;

  public:
    Signature();
    explicit Signature(const std::string& signature);
    Signature(Signature&&) = default;
    Signature(const Signature&) = default;
    Signature& operator=(Signature&&) = default;
    Signature& operator=(const Signature&) = default;

    void setName(std::string name) { _name = std::move(name); }
    void setReturnType(LiteralType rt) { _returnType = rt; }
    void setArgs(std::vector<LiteralType> args) { _args = std::move(args); }

    [[nodiscard]] const std::string& name() const { return _name; }
    [[nodiscard]] LiteralType returnType() const { return _returnType; }
    [[nodiscard]] const std::vector<LiteralType>& args() const { return _args; }
    [[nodiscard]] std::vector<LiteralType>& args() { return _args; }

    [[nodiscard]] std::string to_s() const;

    [[nodiscard]] bool operator==(const Signature& v) const { return to_s() == v.to_s(); }
    [[nodiscard]] bool operator!=(const Signature& v) const { return to_s() != v.to_s(); }
    [[nodiscard]] bool operator<(const Signature& v) const { return to_s() < v.to_s(); }
    [[nodiscard]] bool operator>(const Signature& v) const { return to_s() > v.to_s(); }
    [[nodiscard]] bool operator<=(const Signature& v) const { return to_s() <= v.to_s(); }
    [[nodiscard]] bool operator>=(const Signature& v) const { return to_s() >= v.to_s(); }
};

LiteralType typeSignature(char ch);
char signatureType(LiteralType t);




class NativeCallback
{
  public:
    using Functor = std::function<void(Params& args)>;
    using Verifier = std::function<bool(Instr*, IRBuilder*)>;
    using DefaultValue =
        std::variant<std::monostate, bool, CoreString, CoreNumber, util::IPAddress, util::Cidr, util::RegExp>;

  private:
    Runtime* _runtime;
    bool _isHandler;
    Verifier _verifier;
    Functor _function;
    Signature _signature;

    // function attributes
    unsigned _attributes;

    // following attribs are irrelevant to the VM but useful for the frontend
    std::vector<std::string> _names;
    std::vector<DefaultValue> _defaults;

  public:
    NativeCallback(Runtime* runtime, std::string name);
    NativeCallback(Runtime* runtime, std::string name, LiteralType returnType);
    ~NativeCallback() = default;

    [[nodiscard]] bool isHandler() const noexcept;
    [[nodiscard]] bool isFunction() const noexcept;
    [[nodiscard]] std::string const& name() const noexcept;
    [[nodiscard]] Signature const& signature() const noexcept;

    // signature builder

    /** Declare the return type. */
    NativeCallback& returnType(LiteralType type);

    /** Declare a single named parameter without default value. */
    template <typename T>
    NativeCallback& param(const std::string& name);

    /** Declare a single named parameter with default value. */
    template <typename T>
    NativeCallback& param(const std::string& name, T defaultValue);

    /** Declare ordered parameter signature. */
    template <typename... Args>
    NativeCallback& params(Args... args);

    // semantic verifier
    NativeCallback& verifier(const Verifier& vf);
    template <typename Class>
    NativeCallback& verifier(bool (Class::*method)(Instr*, IRBuilder*), Class* obj);
    template <typename Class>
    NativeCallback& verifier(bool (Class::*method)(Instr*, IRBuilder*));
    bool verify(Instr* call, IRBuilder* irBuilder);

    // bind callback
    NativeCallback& bind(const Functor& cb);
    template <typename Class>
    NativeCallback& bind(void (Class::*method)(Params&), Class* obj);
    template <typename Class>
    NativeCallback& bind(void (Class::*method)(Params&));

    // named parameter handling
    [[nodiscard]] bool parametersNamed() const;
    [[nodiscard]] const std::string& getParamNameAt(size_t i) const;
    [[nodiscard]] const DefaultValue& getDefaultParamAt(size_t i) const;
    [[nodiscard]] int findParamByName(const std::string& name) const;

    // attributes
    NativeCallback& setNoReturn() noexcept;
    NativeCallback& setReadOnly() noexcept;
    NativeCallback& setExperimental() noexcept;

    [[nodiscard]] bool getAttribute(Attribute t) const noexcept { return _attributes & unsigned(t); }
    [[nodiscard]] bool isNeverReturning() const noexcept { return getAttribute(Attribute::NoReturn); }
    [[nodiscard]] bool isReadOnly() const noexcept { return getAttribute(Attribute::SideEffectFree); }
    [[nodiscard]] bool isExperimental() const noexcept { return getAttribute(Attribute::Experimental); }

    // runtime
    void invoke(Params& args) const;
};


struct SourceLocation // {{{
{
    SourceLocation() = default;
    SourceLocation(std::string fileName): filename(std::move(fileName)) {}
    SourceLocation(std::string fileName, FilePos beg, FilePos end):
        filename(std::move(fileName)), begin(beg), end(end)
    {
    }

    std::string filename;
    FilePos begin;
    FilePos end;

    SourceLocation& update(const FilePos& endPos)
    {
        end = endPos;
        return *this;
    }

    SourceLocation& update(const SourceLocation& endLocation)
    {
        end = endLocation.end;
        return *this;
    }

    [[nodiscard]] std::string str() const;
    [[nodiscard]] std::string text() const;

    bool operator==(const SourceLocation& other) const noexcept
    {
        return filename == other.filename && begin == other.begin && end == other.end;
    }

    bool operator!=(const SourceLocation& other) const noexcept { return !(*this == other); }
}; // }}}

inline SourceLocation operator-(const SourceLocation& end, const SourceLocation& beg)
{
    return SourceLocation(beg.filename, beg.begin, end.end);
}


//!@}


class PassManager
{
  public:
    using HandlerPass = std::function<bool(IRHandler* handler)>;

    PassManager() = default;
    ~PassManager() = default;

    /** registers given pass to the pass manager.
     *
     * @param name uniquely identifyable name of the handler pass
     * @param handler callback to invoke to handle the transformation pass
     *
     * The handler must return @c true if it modified its input, @c false otherwise.
     */
    void registerPass(std::string name, HandlerPass handlerPass);

    /** runs passes on a complete program.
     */
    void run(IRProgram* program);

    /** runs passes on given handler.
     */
    void run(IRHandler* handler);

    template <typename... Args>
    void logDebug(fmt::format_string<Args...> msg, Args... args)
    {
        logDebug(fmt::vformat(msg, fmt::make_format_args(args...)));
    }

    void logDebug(const std::string& msg);

  private:
    std::list<std::pair<std::string, HandlerPass>> _handlerPasses;
};


/**
 * Defines an immutable IR value.
 */
class Value
{
  protected:
    Value(const Value& v);

  public:
    Value(LiteralType ty, std::string name);
    Value(Value&&) noexcept = delete;
    Value& operator=(Value&&) noexcept = delete;
    Value& operator=(Value const&) = delete;
    virtual ~Value();

    [[nodiscard]] LiteralType type() const { return _type; }
    void setType(LiteralType ty) { _type = ty; }

    [[nodiscard]] const std::string& name() const { return _name; }
    void setName(const std::string& n) { _name = n; }

    /**
     * adds @p user to the list of instructions that are "using" this value.
     */
    void addUse(Instr* user);

    /**
     * removes @p user from the list of instructions that determines the list of
     * instructions that are using this value.
     */
    void removeUse(Instr* user);

    /**
     * Determines whether or not this value is being used by at least one other
     * instruction.
     */
    [[nodiscard]] bool isUsed() const { return !_uses.empty(); }

    /**
     * Retrieves a range instructions that are *using* this value.
     */
    [[nodiscard]] const std::vector<Instr*>& uses() const { return _uses; }

    [[nodiscard]] size_t useCount() const { return _uses.size(); }

    /**
     * Replaces all uses of \c this value as operand with value \p newUse instead.
     *
     * @param newUse the new value to be used.
     */
    void replaceAllUsesWith(Value* newUse);

    [[nodiscard]] virtual std::string to_string() const;

  private:
    LiteralType _type;
    std::string _name;

    std::vector<Instr*> _uses; //! list of instructions that <b>use</b> this value.
};


class Constant: public Value
{
  public:
    Constant(LiteralType ty, std::string name): Value(ty, std::move(name)) {}

    [[nodiscard]] std::string to_string() const override;
};

class ConstantArray: public Constant
{
  public:
    ConstantArray(LiteralType elementType, std::vector<Constant*> elements, std::string name = ""):
        Constant(makeArrayType(elementType), std::move(name)), _elements(std::move(elements))
    {
    }

    ConstantArray(const std::vector<Constant*>& elements, const std::string& name = ""):
        Constant(makeArrayType(elements.front()->type()), name), _elements(elements)
    {
    }

    ConstantArray(ConstantArray&&) = delete;
    ConstantArray& operator=(ConstantArray&&) = delete;

    ConstantArray(const ConstantArray&) = delete;
    ConstantArray& operator=(const ConstantArray&) = delete;

    [[nodiscard]] const std::vector<Constant*>& get() const { return _elements; }

    [[nodiscard]] LiteralType elementType() const { return _elements[0]->type(); }

  private:
    std::vector<Constant*> _elements;

    LiteralType makeArrayType(LiteralType elementType);
};



class IRBuiltinFunction: public Constant
{
  public:
    explicit IRBuiltinFunction(const NativeCallback& cb):
        Constant(cb.signature().returnType(), cb.signature().name()), _native(cb)
    {
    }

    [[nodiscard]] const Signature& signature() const { return _native.signature(); }
    [[nodiscard]] const NativeCallback& getNative() const { return _native; }

  private:
    const NativeCallback& _native;
};




template <typename T, const LiteralType Ty>
class ConstantValue: public Constant
{
  public:
    ConstantValue(const T& value, const std::string& name = ""): Constant(Ty, name), _value(value) {}

    [[nodiscard]] T get() const { return _value; }

    [[nodiscard]] std::string to_string() const override
    {
        return fmt::format("Constant '{}': {} = {}", name(), type(), _value);
    }

  private:
    T _value;
};

using ConstantInt = ConstantValue<int64_t, LiteralType::Number>;
using ConstantBoolean = ConstantValue<bool, LiteralType::Boolean>;
using ConstantString = ConstantValue<std::string, LiteralType::String>;
using ConstantIP = ConstantValue<util::IPAddress, LiteralType::IPAddress>;
using ConstantCidr = ConstantValue<util::Cidr, LiteralType::Cidr>;
using ConstantRegExp = ConstantValue<util::RegExp, LiteralType::RegExp>;


/**
 * Base class for native instructions.
 *
 * An instruction is derived from base class \c Value because its result can be
 * used as an operand for other instructions.
 *
 * @see IRBuilder
 * @see BasicBlock
 * @see IRHandler
 */
class Instr: public Value
{
  protected:
    Instr(const Instr& v);

  public:
    Instr(LiteralType ty, const std::vector<Value*>& ops = {}, const std::string& name = "");
    ~Instr() override;

    /**
     * Retrieves parent basic block this instruction is part of.
     */
    [[nodiscard]] BasicBlock* getBasicBlock() const { return _basicBlock; }

    /**
     * Read-only access to operands.
     */
    [[nodiscard]] const std::vector<Value*>& operands() const { return _operands; }

    /**
     * Retrieves n'th operand at given \p index.
     */
    [[nodiscard]] Value* operand(size_t index) const { return _operands[index]; }

    /**
     * Adds given operand \p value to the end of the operand list.
     */
    void addOperand(Value* value);

    /**
     * Sets operand at index \p i to given \p value.
     *
     * This operation will potentially replace the value that has been at index \p
     *i before,
     * properly unlinking it from any uses or successor/predecessor links.
     */
    Value* setOperand(size_t i, Value* value);

    /**
     * Replaces \p old operand with \p replacement.
     *
     * @param old value to replace
     * @param replacement new value to put at the offset of \p old.
     *
     * @returns number of actual performed replacements.
     */
    size_t replaceOperand(Value* old, Value* replacement);

    /**
     * Clears out all operands.
     *
     * @see addOperand()
     */
    void clearOperands();

    /**
     * Replaces this instruction with the given @p newInstr.
     *
     * @returns ownership of this instruction.
     */
    std::unique_ptr<Instr> replace(std::unique_ptr<Instr> newInstr);

    /**
     * Clones given instruction.
     *
     * This will not clone any of its operands but reference them.
     */
    virtual std::unique_ptr<Instr> clone() = 0;

    /**
     * Generic extension interface.
     *
     * @param v extension to pass this instruction to.
     *
     * @see InstructionVisitor
     */
    virtual void accept(InstructionVisitor& v) = 0;

  protected:
    void dumpOne(const char* mnemonic);
    [[nodiscard]] std::string formatOne(std::string mnemonic) const;

    void setParent(BasicBlock* bb) { _basicBlock = bb; }

    friend class BasicBlock;

  private:
    BasicBlock* _basicBlock;
    std::vector<Value*> _operands;
};


class NopInstr: public Instr
{
  public:
    NopInstr(): Instr(LiteralType::Void, {}, "nop") {}

    [[nodiscard]] std::string to_string() const override;
    [[nodiscard]] std::unique_ptr<Instr> clone() override;
    void accept(InstructionVisitor& v) override;
};

/**
 * Allocates an array of given type and elements.
 */
class AllocaInstr: public Instr
{
  private:
    static LiteralType computeType(LiteralType elementType, Value* size)
    { // {{{
        if (auto* n = dynamic_cast<ConstantInt*>(size))
        {
            if (n->get() == 1)
                return elementType;
        }

        switch (elementType)
        {
            case LiteralType::Number: return LiteralType::IntArray;
            case LiteralType::String: return LiteralType::StringArray;
            default: return LiteralType::Void;
        }
    } // }}}

  public:
    AllocaInstr(LiteralType ty, Value* n, const std::string& name): Instr(ty, { n }, name) {}

    [[nodiscard]] LiteralType elementType() const
    {
        switch (type())
        {
            case LiteralType::StringArray: return LiteralType::String;
            case LiteralType::IntArray: return LiteralType::Number;
            default: return LiteralType::Void;
        }
    }

    [[nodiscard]] Value* arraySize() const { return operands()[0]; }

    [[nodiscard]] std::string to_string() const override;
    [[nodiscard]] std::unique_ptr<Instr> clone() override;
    void accept(InstructionVisitor& v) override;
};

class StoreInstr: public Instr
{
  public:
    StoreInstr(Value* variable, ConstantInt* index, Value* source, const std::string& name):
        Instr(LiteralType::Void, { variable, index, source }, name)
    {
    }

    [[nodiscard]] Value* variable() const { return operand(0); }
    [[nodiscard]] ConstantInt* index() const { return static_cast<ConstantInt*>(operand(1)); }
    [[nodiscard]] Value* source() const { return operand(2); }

    [[nodiscard]] std::string to_string() const override;
    [[nodiscard]] std::unique_ptr<Instr> clone() override;
    void accept(InstructionVisitor& v) override;
};

class RegExpGroupInstr: public Instr
{
  public:
    RegExpGroupInstr(ConstantInt* groupId, const std::string& name):
        Instr { LiteralType::String, { groupId }, name }
    {
    }

    [[nodiscard]] ConstantInt* groupId() const { return static_cast<ConstantInt*>(operand(0)); }

    [[nodiscard]] std::string to_string() const override;
    [[nodiscard]] std::unique_ptr<Instr> clone() override;
    void accept(InstructionVisitor& v) override;
};

class LoadInstr: public Instr
{
  public:
    LoadInstr(Value* variable, const std::string& name): Instr(variable->type(), { variable }, name) {}

    [[nodiscard]] Value* variable() const { return operand(0); }

    [[nodiscard]] std::string to_string() const override;
    [[nodiscard]] std::unique_ptr<Instr> clone() override;
    void accept(InstructionVisitor& v) override;
};

class CallInstr: public Instr
{
  public:
    CallInstr(const std::vector<Value*>& args, const std::string& name);
    CallInstr(IRBuiltinFunction* callee, const std::vector<Value*>& args, const std::string& name);

    [[nodiscard]] IRBuiltinFunction* callee() const { return (IRBuiltinFunction*) operand(0); }

    [[nodiscard]] std::string to_string() const override;
    [[nodiscard]] std::unique_ptr<Instr> clone() override;
    void accept(InstructionVisitor& v) override;
};

class HandlerCallInstr: public Instr
{
  public:
    explicit HandlerCallInstr(const std::vector<Value*>& args);
    HandlerCallInstr(IRBuiltinHandler* callee, const std::vector<Value*>& args);

    [[nodiscard]] IRBuiltinHandler* callee() const { return (IRBuiltinHandler*) operand(0); }

    [[nodiscard]] std::string to_string() const override;
    [[nodiscard]] std::unique_ptr<Instr> clone() override;
    void accept(InstructionVisitor& v) override;
};

class CastInstr: public Instr
{
  public:
    CastInstr(LiteralType resultType, Value* op, const std::string& name): Instr(resultType, { op }, name) {}

    [[nodiscard]] Value* source() const { return operand(0); }

    [[nodiscard]] std::string to_string() const override;
    [[nodiscard]] std::unique_ptr<Instr> clone() override;
    void accept(InstructionVisitor& v) override;
};

template <const UnaryOperator Operator, const LiteralType ResultType>
class UnaryInstr: public Instr
{
  public:
    UnaryInstr(Value* op, const std::string& name): Instr(ResultType, { op }, name), _operator(Operator) {}

    [[nodiscard]] UnaryOperator op() const { return _operator; }

    [[nodiscard]] std::string to_string() const override { return formatOne(cstr(_operator)); }

    [[nodiscard]] std::unique_ptr<Instr> clone() override
    {
        return std::make_unique<UnaryInstr<Operator, ResultType>>(operand(0), name());
    }

    void accept(InstructionVisitor& v) override { v.visit(*this); }

  private:
    UnaryOperator _operator;
};

template <const BinaryOperator Operator, const LiteralType ResultType>
class BinaryInstr: public Instr
{
  public:
    BinaryInstr(Value* lhs, Value* rhs, const std::string& name):
        Instr(ResultType, { lhs, rhs }, name), _operator(Operator)
    {
    }

    [[nodiscard]] BinaryOperator op() const { return _operator; }

    [[nodiscard]] std::string to_string() const override { return formatOne(cstr(_operator)); }

    [[nodiscard]] std::unique_ptr<Instr> clone() override
    {
        return std::make_unique<BinaryInstr<Operator, ResultType>>(operand(0), operand(1), name());
    }

    void accept(InstructionVisitor& v) override { v.visit(*this); }

  private:
    BinaryOperator _operator;
};

/**
 * Creates a PHI (phoney) instruction.
 *
 * Creates a synthetic instruction that purely informs the target register
 *allocator
 * to allocate the very same register for all given operands,
 * which is then used across all their basic blocks.
 */
class PhiNode: public Instr
{
  public:
    PhiNode(const std::vector<Value*>& ops, const std::string& name);

    [[nodiscard]] std::string to_string() const override;
    [[nodiscard]] std::unique_ptr<Instr> clone() override;
    void accept(InstructionVisitor& v) override;
};

class TerminateInstr: public Instr
{
  protected:
    TerminateInstr(const TerminateInstr& v) = default;

  public:
    TerminateInstr(const std::vector<Value*>& ops): Instr(LiteralType::Void, ops, "") {}
};

/**
 * Conditional branch instruction.
 *
 * Creates a terminate instruction that transfers control to one of the two
 * given alternate basic blocks, depending on the given input condition.
 */
class CondBrInstr: public TerminateInstr
{
  public:
    /**
     * Initializes the object.
     *
     * @param cond input condition that (if true) causes \p trueBlock to be jumped
     *to, \p falseBlock otherwise.
     * @param trueBlock basic block to run if input condition evaluated to true.
     * @param falseBlock basic block to run if input condition evaluated to false.
     */
    CondBrInstr(Value* cond, BasicBlock* trueBlock, BasicBlock* falseBlock);

    [[nodiscard]] Value* condition() const { return operands()[0]; }
    [[nodiscard]] BasicBlock* trueBlock() const { return (BasicBlock*) operands()[1]; }
    [[nodiscard]] BasicBlock* falseBlock() const { return (BasicBlock*) operands()[2]; }

    [[nodiscard]] std::string to_string() const override;
    [[nodiscard]] std::unique_ptr<Instr> clone() override;
    void accept(InstructionVisitor& v) override;
};

/**
 * Unconditional jump instruction.
 */
class BrInstr: public TerminateInstr
{
  public:
    explicit BrInstr(BasicBlock* targetBlock);

    [[nodiscard]] BasicBlock* targetBlock() const { return (BasicBlock*) operands()[0]; }

    [[nodiscard]] std::string to_string() const override;
    [[nodiscard]] std::unique_ptr<Instr> clone() override;
    void accept(InstructionVisitor& v) override;
};

/**
 * handler-return instruction.
 */
class RetInstr: public TerminateInstr
{
  public:
    RetInstr(Value* result);

    [[nodiscard]] std::string to_string() const override;
    [[nodiscard]] std::unique_ptr<Instr> clone() override;
    void accept(InstructionVisitor& v) override;
};

/**
 * Match instruction, implementing the CoreVM match-keyword.
 *
 * <li>operand[0] - condition</li>
 * <li>operand[1] - default block</li>
 * <li>operand[2n+2] - case label</li>
 * <li>operand[2n+3] - case block</li>
 */
class MatchInstr: public TerminateInstr
{
  public:
    MatchInstr(const MatchInstr&);
    MatchInstr(MatchClass op, Value* cond);

    [[nodiscard]] MatchClass op() const { return _op; }

    [[nodiscard]] Value* condition() const { return operand(0); }

    void addCase(Constant* label, BasicBlock* code);
    [[nodiscard]] std::vector<std::pair<Constant*, BasicBlock*>> cases() const;

    [[nodiscard]] BasicBlock* elseBlock() const;
    void setElseBlock(BasicBlock* code);

    [[nodiscard]] std::string to_string() const override;
    [[nodiscard]] std::unique_ptr<Instr> clone() override;
    void accept(InstructionVisitor& v) override;

  private:
    MatchClass _op;
    std::vector<std::pair<Constant*, BasicBlock*>> _cases;
};

class Instr;

class IsSameInstruction: public InstructionVisitor
{
  public:
    static bool test(Instr* a, Instr* b);
    static bool isSameOperands(Instr* a, Instr* b);

  private:
    Instr* _other;
    bool _result;

  protected:
    explicit IsSameInstruction(Instr* a);

    void visit(NopInstr& instr) override;

    // storage
    void visit(AllocaInstr& instr) override;
    void visit(StoreInstr& instr) override;
    void visit(LoadInstr& instr) override;
    void visit(PhiNode& instr) override;

    // calls
    void visit(CallInstr& instr) override;
    void visit(HandlerCallInstr& instr) override;

    // terminator
    void visit(CondBrInstr& instr) override;
    void visit(BrInstr& instr) override;
    void visit(RetInstr& instr) override;
    void visit(MatchInstr& instr) override;

    // regexp
    void visit(RegExpGroupInstr& instr) override;

    // type cast
    void visit(CastInstr& instr) override;

    // numeric
    void visit(INegInstr& instr) override;
    void visit(INotInstr& instr) override;
    void visit(IAddInstr& instr) override;
    void visit(ISubInstr& instr) override;
    void visit(IMulInstr& instr) override;
    void visit(IDivInstr& instr) override;
    void visit(IRemInstr& instr) override;
    void visit(IPowInstr& instr) override;
    void visit(IAndInstr& instr) override;
    void visit(IOrInstr& instr) override;
    void visit(IXorInstr& instr) override;
    void visit(IShlInstr& instr) override;
    void visit(IShrInstr& instr) override;
    void visit(ICmpEQInstr& instr) override;
    void visit(ICmpNEInstr& instr) override;
    void visit(ICmpLEInstr& instr) override;
    void visit(ICmpGEInstr& instr) override;
    void visit(ICmpLTInstr& instr) override;
    void visit(ICmpGTInstr& instr) override;

    // boolean
    void visit(BNotInstr& instr) override;
    void visit(BAndInstr& instr) override;
    void visit(BOrInstr& instr) override;
    void visit(BXorInstr& instr) override;

    // string
    void visit(SLenInstr& instr) override;
    void visit(SIsEmptyInstr& instr) override;
    void visit(SAddInstr& instr) override;
    void visit(SSubStrInstr& instr) override;
    void visit(SCmpEQInstr& instr) override;
    void visit(SCmpNEInstr& instr) override;
    void visit(SCmpLEInstr& instr) override;
    void visit(SCmpGEInstr& instr) override;
    void visit(SCmpLTInstr& instr) override;
    void visit(SCmpGTInstr& instr) override;
    void visit(SCmpREInstr& instr) override;
    void visit(SCmpBegInstr& instr) override;
    void visit(SCmpEndInstr& instr) override;
    void visit(SInInstr& instr) override;

    // ip
    void visit(PCmpEQInstr& instr) override;
    void visit(PCmpNEInstr& instr) override;
    void visit(PInCidrInstr& instr) override;
};

class IRHandler: public Constant
{
  public:
    IRHandler(const std::string& name, IRProgram* parent);
    ~IRHandler() override;

    IRHandler(IRHandler&&) = delete;
    IRHandler& operator=(IRHandler&&) = delete;

    BasicBlock* createBlock(const std::string& name = "");

    [[nodiscard]] IRProgram* getProgram() const { return _program; }
    void setParent(IRProgram* prog) { _program = prog; }

    void dump();

    [[nodiscard]] bool empty() const noexcept { return _blocks.empty(); }

    auto basicBlocks() { return util::unbox(_blocks); }

    [[nodiscard]] BasicBlock* getEntryBlock() const { return _blocks.front().get(); }
    void setEntryBlock(BasicBlock* bb);

    /**
     * Unlinks and deletes given basic block \p bb from handler.
     *
     * @note \p bb will be a dangling pointer after this call.
     */
    void erase(BasicBlock* bb);

    bool isAfter(const BasicBlock* bb, const BasicBlock* afterThat) const;
    void moveAfter(const BasicBlock* moveable, const BasicBlock* afterThat);
    void moveBefore(const BasicBlock* moveable, const BasicBlock* beforeThat);

    /**
     * Performs given transformation on this handler.
     */
    template <typename TheHandlerPass, typename... Args>
    size_t transform(Args&&... args)
    {
        return TheHandlerPass(std::forward(args)...).run(this);
    }

    /**
     * Performs sanity checks on internal data structures.
     *
     * This call does not return any success or failure as every failure is
     * considered fatal and will cause the program to exit with diagnostics
     * as this is most likely caused by an application programming error.
     *
     * @note Always call this on completely defined handlers and never on
     * half-contructed ones.
     */
    void verify();

  private:
    IRProgram* _program;
    std::list<std::unique_ptr<BasicBlock>> _blocks;

    friend class IRBuilder;
};

class IRBuiltinHandler: public Constant
{
  public:
    explicit IRBuiltinHandler(const NativeCallback& cb):
        Constant(LiteralType::Boolean, cb.signature().name()), _native(cb)
    {
    }

    const Signature& signature() const { return _native.signature(); }
    const NativeCallback& getNative() const { return _native; }

  private:
    const NativeCallback& _native;
};


class IRProgram
{
  public:
    IRProgram();
    ~IRProgram();

    void dump();

    ConstantBoolean* getBoolean(bool literal) { return literal ? &_trueLiteral : &_falseLiteral; }
    ConstantInt* get(int64_t literal) { return get<ConstantInt>(_numbers, literal); }
    ConstantString* get(const std::string& literal) { return get<ConstantString>(_strings, literal); }
    ConstantIP* get(const util::IPAddress& literal) { return get<ConstantIP>(_ipaddrs, literal); }
    ConstantCidr* get(const util::Cidr& literal) { return get<ConstantCidr>(_cidrs, literal); }
    ConstantRegExp* get(const util::RegExp& literal) { return get<ConstantRegExp>(_regexps, literal); }
    ConstantArray* get(const std::vector<Constant*>& elems)
    {
        return get<ConstantArray>(_constantArrays, elems);
    }

    [[nodiscard]] IRBuiltinHandler* findBuiltinHandler(const Signature& sig) const
    {
        for (const auto& builtinHandler: _builtinHandlers)
            if (builtinHandler->signature() == sig)
                return builtinHandler.get();

        return nullptr;
    }

    IRBuiltinHandler* getBuiltinHandler(const NativeCallback& cb)
    {
        for (const auto& builtinHandler: _builtinHandlers)
            if (builtinHandler->signature() == cb.signature())
                return builtinHandler.get();

        _builtinHandlers.emplace_back(std::make_unique<IRBuiltinHandler>(cb));
        return _builtinHandlers.back().get();
    }

    IRBuiltinFunction* getBuiltinFunction(const NativeCallback& cb)
    {
        for (const auto& builtinFunction: _builtinFunctions)
            if (builtinFunction->signature() == cb.signature())
                return builtinFunction.get();

        _builtinFunctions.emplace_back(std::make_unique<IRBuiltinFunction>(cb));
        return _builtinFunctions.back().get();
    }

    template <typename T, typename U>
    T* get(std::vector<T>& table, U&& literal);
    template <typename T, typename U>
    T* get(std::vector<std::unique_ptr<T>>& table, U&& literal);

    void addImport(const std::string& name, const std::string& path) { _modules.emplace_back(name, path); }
    void setModules(const std::vector<std::pair<std::string, std::string>>& modules) { _modules = modules; }

    [[nodiscard]] const std::vector<std::pair<std::string, std::string>>& modules() const { return _modules; }

    auto handlers() { return util::unbox(_handlers); }

    IRHandler* findHandler(const std::string& name)
    {
        for (IRHandler* handler: handlers())
            if (handler->name() == name)
                return handler;

        return nullptr;
    }

    IRHandler* createHandler(const std::string& name);

    /**
     * Performs given transformation on all handlers by given type.
     */
    template <typename TheHandlerPass, typename... Args>
    size_t transform(Args&&... args)
    {
        size_t count = 0;
        for (IRHandler* handler: handlers())
        {
            count += handler->transform<TheHandlerPass>(args...);
        }
        return count;
    }

  private:
    std::vector<std::pair<std::string, std::string>> _modules;
    ConstantBoolean _trueLiteral;
    ConstantBoolean _falseLiteral;
    std::vector<std::unique_ptr<ConstantArray>> _constantArrays;
    std::vector<std::unique_ptr<ConstantInt>> _numbers;
    std::vector<std::unique_ptr<ConstantString>> _strings;
    std::vector<std::unique_ptr<ConstantIP>> _ipaddrs;
    std::vector<std::unique_ptr<ConstantCidr>> _cidrs;
    std::vector<std::unique_ptr<ConstantRegExp>> _regexps;
    std::vector<std::unique_ptr<IRBuiltinFunction>> _builtinFunctions;
    std::vector<std::unique_ptr<IRBuiltinHandler>> _builtinHandlers;
    std::vector<std::unique_ptr<IRHandler>> _handlers;

    friend class IRBuilder;
};



class IRBuilder
{
  private:
    IRProgram* _program;
    IRHandler* _handler;
    BasicBlock* _insertPoint;
    std::unordered_map<std::string, unsigned long> _nameStore;

  public:
    IRBuilder();
    ~IRBuilder() = default;

    std::string makeName(std::string name);

    void setProgram(std::unique_ptr<IRProgram> program);
    IRProgram* program() const { return _program; }

    IRHandler* setHandler(IRHandler* hn);
    IRHandler* handler() const { return _handler; }

    BasicBlock* createBlock(const std::string& name);

    void setInsertPoint(BasicBlock* bb);
    BasicBlock* getInsertPoint() const { return _insertPoint; }

    Instr* insert(std::unique_ptr<Instr> instr);

    template <typename T, typename... Args>
    T* insert(Args&&... args)
    {
        return static_cast<T*>(insert(std::make_unique<T>(std::forward<Args>(args)...)));
    }

    IRHandler* getHandler(const std::string& name);
    IRHandler* findHandler(const std::string& name);

    // literals
    ConstantBoolean* getBoolean(bool literal) { return _program->getBoolean(literal); }
    ConstantInt* get(int64_t literal) { return _program->get(literal); }
    ConstantString* get(const std::string& literal) { return _program->get(literal); }
    ConstantIP* get(const util::IPAddress& literal) { return _program->get(literal); }
    ConstantCidr* get(const util::Cidr& literal) { return _program->get(literal); }
    ConstantRegExp* get(const util::RegExp& literal) { return _program->get(literal); }
    IRBuiltinHandler* findBuiltinHandler(const Signature& sig) { return _program->findBuiltinHandler(sig); }
    IRBuiltinHandler* getBuiltinHandler(const NativeCallback& cb) { return _program->getBuiltinHandler(cb); }
    IRBuiltinFunction* getBuiltinFunction(const NativeCallback& cb)
    {
        return _program->getBuiltinFunction(cb);
    }
    ConstantArray* get(const std::vector<Constant*>& arrayElements) { return _program->get(arrayElements); }

    // values
    AllocaInstr* createAlloca(LiteralType ty, Value* arraySize, const std::string& name = "");
    Value* createLoad(Value* value, const std::string& name = "");
    Instr* createStore(Value* lhs, Value* rhs, const std::string& name = "");
    Instr* createStore(Value* lhs, ConstantInt* index, Value* rhs, const std::string& name = "");
    Instr* createPhi(const std::vector<Value*>& incomings, const std::string& name = "");

    // boolean operations
    Value* createBNot(Value* rhs, const std::string& name = ""); // !
    Value* createBAnd(Value* lhs, Value* rhs,
                      const std::string& name = ""); // &&
    Value* createBXor(Value* lhs, Value* rhs,
                      const std::string& name = ""); // ||

    // numerical operations
    Value* createNeg(Value* rhs, const std::string& name = "");             // -
    Value* createNot(Value* rhs, const std::string& name = "");             // ~
    Value* createAdd(Value* lhs, Value* rhs, const std::string& name = ""); // +
    Value* createSub(Value* lhs, Value* rhs, const std::string& name = ""); // -
    Value* createMul(Value* lhs, Value* rhs, const std::string& name = ""); // *
    Value* createDiv(Value* lhs, Value* rhs, const std::string& name = ""); // /
    Value* createRem(Value* lhs, Value* rhs, const std::string& name = ""); // %
    Value* createShl(Value* lhs, Value* rhs, const std::string& name = ""); // <<
    Value* createShr(Value* lhs, Value* rhs, const std::string& name = ""); // >>
    Value* createPow(Value* lhs, Value* rhs, const std::string& name = ""); // **
    Value* createAnd(Value* lhs, Value* rhs, const std::string& name = ""); // &
    Value* createOr(Value* lhs, Value* rhs, const std::string& name = "");  // |
    Value* createXor(Value* lhs, Value* rhs, const std::string& name = ""); // ^
    Value* createNCmpEQ(Value* lhs, Value* rhs,
                        const std::string& name = ""); // ==
    Value* createNCmpNE(Value* lhs, Value* rhs,
                        const std::string& name = ""); // !=
    Value* createNCmpLE(Value* lhs, Value* rhs,
                        const std::string& name = ""); // <=
    Value* createNCmpGE(Value* lhs, Value* rhs,
                        const std::string& name = ""); // >=
    Value* createNCmpLT(Value* lhs, Value* rhs,
                        const std::string& name = ""); // <
    Value* createNCmpGT(Value* lhs, Value* rhs,
                        const std::string& name = ""); // >

    // string ops
    Value* createSAdd(Value* lhs, Value* rhs, const std::string& name = ""); // +
    Value* createSCmpEQ(Value* lhs, Value* rhs,
                        const std::string& name = ""); // ==
    Value* createSCmpNE(Value* lhs, Value* rhs,
                        const std::string& name = ""); // !=
    Value* createSCmpLE(Value* lhs, Value* rhs,
                        const std::string& name = ""); // <=
    Value* createSCmpGE(Value* lhs, Value* rhs,
                        const std::string& name = ""); // >=
    Value* createSCmpLT(Value* lhs, Value* rhs,
                        const std::string& name = ""); // <
    Value* createSCmpGT(Value* lhs, Value* rhs,
                        const std::string& name = ""); // >
    Value* createSCmpRE(Value* lhs, Value* rhs,
                        const std::string& name = ""); // =~
    Value* createSCmpEB(Value* lhs, Value* rhs,
                        const std::string& name = ""); // =^
    Value* createSCmpEE(Value* lhs, Value* rhs,
                        const std::string& name = "");                      // =$
    Value* createSIn(Value* lhs, Value* rhs, const std::string& name = ""); // in
    Value* createSLen(Value* value, const std::string& name = "");

    // IP address
    Value* createPCmpEQ(Value* lhs, Value* rhs,
                        const std::string& name = ""); // ==
    Value* createPCmpNE(Value* lhs, Value* rhs,
                        const std::string& name = ""); // !=
    Value* createPInCidr(Value* lhs, Value* rhs,
                         const std::string& name = ""); // in

    // cidr
    // ...

    // regexp
    RegExpGroupInstr* createRegExpGroup(ConstantInt* groupId, const std::string& name = "");

    // cast
    Value* createConvert(LiteralType ty, Value* rhs,
                         const std::string& name = ""); // cast<T>()
    Value* createB2S(Value* rhs, const std::string& name = "");
    Value* createN2S(Value* rhs, const std::string& name = "");
    Value* createP2S(Value* rhs, const std::string& name = "");
    Value* createC2S(Value* rhs, const std::string& name = "");
    Value* createR2S(Value* rhs, const std::string& name = "");
    Value* createS2N(Value* rhs, const std::string& name = "");

    // calls
    Instr* createCallFunction(IRBuiltinFunction* callee, std::vector<Value*> args, std::string name = "");
    Instr* createInvokeHandler(IRBuiltinHandler* callee, const std::vector<Value*>& args);

    // termination instructions
    Instr* createRet(Value* result);
    Instr* createBr(BasicBlock* target);
    Instr* createCondBr(Value* condValue, BasicBlock* trueBlock, BasicBlock* falseBlock);
    MatchInstr* createMatch(MatchClass opc, Value* cond);
    Value* createMatchSame(Value* cond);
    Value* createMatchHead(Value* cond);
    Value* createMatchTail(Value* cond);
    Value* createMatchRegExp(Value* cond);
};

/**
 * An SSA based instruction basic block.
 *
 * @see Instr, IRHandler, IRBuilder
 */
class BasicBlock: public Value
{
  public:
    BasicBlock(const std::string& name, IRHandler* parent);
    ~BasicBlock() override;

    [[nodiscard]] IRHandler* getHandler() const { return _handler; }
    void setParent(IRHandler* handler) { _handler = handler; }

    /*!
     * Retrieves the last terminating instruction in this basic block.
     *
     * This instruction must be a termination instruction, such as
     * a branching instruction or a handler terminating instruction.
     *
     * @see BrInstr, CondBrInstr, MatchInstr, RetInstr
     */
    [[nodiscard]] TerminateInstr* getTerminator() const;

    /**
     * Checks whether this BasicBlock is assured to terminate, hence, complete.
     *
     * This is either achieved by having a TerminateInstr at the end or a NativeCallback
     * that never returns.
     */
    [[nodiscard]] bool isComplete() const;

    /**
     * Retrieves the linear ordered list of instructions of instructions in this
     * basic block.
     */
    auto instructions() { return util::unbox(_code); }
    Instr* instruction(size_t i) { return _code[i].get(); }

    [[nodiscard]] Instr* front() const { return _code.front().get(); }
    [[nodiscard]] Instr* back() const { return _code.back().get(); }

    [[nodiscard]] size_t size() const { return _code.size(); }
    [[nodiscard]] bool empty() const { return _code.empty(); }

    [[nodiscard]] Instr* back(size_t sub) const
    {
        if (sub + 1 <= _code.size())
            return _code[_code.size() - (1 + sub)].get();
        else
            return nullptr;
    }

    /**
     * Appends a new instruction, \p instr, to this basic block.
     *
     * The basic block will take over ownership of the given instruction.
     */
    Instr* push_back(std::unique_ptr<Instr> instr);

    /**
     * Removes given instruction from this basic block.
     *
     * The basic block will pass ownership of the given instruction to the caller.
     * That means, the caller has to either delete \p childInstr or transfer it to
     * another basic block.
     *
     * @see push_back()
     */
    std::unique_ptr<Instr> remove(Instr* childInstr);

    /**
     * Replaces given @p oldInstr with @p newInstr.
     *
     * @return returns given @p oldInstr.
     */
    std::unique_ptr<Instr> replace(Instr* oldInstr, std::unique_ptr<Instr> newInstr);

    /**
     * Merges given basic block's instructions into this ones end.
     *
     * The passed basic block's instructions <b>WILL</b> be touched, that is
     * - all instructions will have been moved.
     * - any successor BBs will have been relinked
     */
    void merge_back(BasicBlock* bb);

    /**
     * Moves this basic block after the other basic block, \p otherBB.
     *
     * @param otherBB the future prior basic block.
     *
     * In a function, all basic blocks (starting from the entry block)
     * will be aligned linear into the execution segment.
     *
     * This function moves this basic block directly after
     * the other basic block, \p otherBB.
     *
     * @see moveBefore()
     */
    void moveAfter(const BasicBlock* otherBB);

    /**
     * Moves this basic block before the other basic block, \p otherBB.
     *
     * @see moveAfter()
     */
    void moveBefore(const BasicBlock* otherBB);

    /**
     * Tests whether or not given block is straight-line located after this block.
     *
     * @retval true \p otherBB is straight-line located after this block.
     * @retval false \p otherBB is not straight-line located after this block.
     *
     * @see moveAfter()
     */
    bool isAfter(const BasicBlock* otherBB) const;

    /**
     * Links given \p successor basic block to this predecessor.
     *
     * @param successor the basic block to link as an successor of this basic
     *                  block.
     *
     * This will also automatically link this basic block as
     * future predecessor of the \p successor.
     *
     * @see unlinkSuccessor()
     * @see successors(), predecessors()
     */
    void linkSuccessor(BasicBlock* successor);

    /**
     * Unlink given \p successor basic block from this predecessor.
     *
     * @see linkSuccessor()
     * @see successors(), predecessors()
     */
    void unlinkSuccessor(BasicBlock* successor);

    /** Retrieves all predecessors of given basic block. */
    [[nodiscard]] std::vector<BasicBlock*>& predecessors() { return _predecessors; }

    /** Retrieves all uccessors of the given basic block. */
    [[nodiscard]] std::vector<BasicBlock*>& successors() { return _successors; }
    [[nodiscard]] const std::vector<BasicBlock*>& successors() const { return _successors; }

    /** Retrieves all dominators of given basic block. */
    [[nodiscard]] std::vector<BasicBlock*> dominators();

    /** Retrieves all immediate dominators of given basic block. */
    [[nodiscard]] std::vector<BasicBlock*> immediateDominators();

    void dump();

    /**
     * Performs sanity checks on internal data structures.
     *
     * This call does not return any success or failure as every failure is
     * considered fatal and will cause the program to exit with diagnostics
     * as this is most likely caused by an application programming error.
     *
     * @note This function is automatically invoked by IRHandler::verify()
     *
     * @see IRHandler::verify()
     */
    void verify();

  private:
    void collectIDom(std::vector<BasicBlock*>& output);

  private:
    IRHandler* _handler;
    std::vector<std::unique_ptr<Instr>> _code;
    std::vector<BasicBlock*> _predecessors;
    std::vector<BasicBlock*> _successors;

    friend class IRBuilder;
    friend class Instr;
};

class Runtime
{
  public:
    using Value = uint64_t;

    virtual ~Runtime() = default;

    virtual bool import(const std::string& name,
                        const std::string& path,
                        std::vector<NativeCallback*>* builtins);

    [[nodiscard]] NativeCallback* find(const std::string& signature) const noexcept;
    [[nodiscard]] NativeCallback* find(const Signature& signature) const noexcept;
    [[nodiscard]] auto builtins() { return util::unbox(_builtins); }

    NativeCallback& registerHandler(const std::string& name);
    NativeCallback& registerFunction(const std::string& name);
    NativeCallback& registerFunction(const std::string& name, LiteralType returnType);

    void invoke(int id, int argc, Value* argv, Runner* cx);

    /**
     * Verifies all call instructions.
     *
     * @param program the IRProgram to verify.
     * @param builder the IRBuilder to pass to each verify(), that can be used for modifications.
     *
     * @see bool NativeCallback::verify(Instr* call, IRBuilder* irBuilder);
     */
    bool verifyNativeCalls(IRProgram* program, IRBuilder* builder) const;

  private:
    std::vector<std::unique_ptr<NativeCallback>> _builtins;
};

template <typename T, typename U>
T* IRProgram::get(std::vector<std::unique_ptr<T>>& table, U&& literal)
{
    for (size_t i = 0, e = table.size(); i != e; ++i)
        if (table[i]->get() == literal)
            return table[i].get();

    table.emplace_back(std::make_unique<T>(std::forward<U>(literal)));
    return table.back().get();
}

template <typename T, typename U>
T* IRProgram::get(std::vector<T>& table, U&& literal)
{
    if (auto i = std::ranges::find_if(table, [&](T const& elem) { return elem.get() == literal; });
        i != table.end())
    {
        fmt::print("IRProgram.get<{}, {}>: found existing entry\n", typeid(T).name(), typeid(U).name());
        return &*i;
    }

    // for (size_t i = 0, e = table.size(); i != e; ++i)
    //     if (table[i].get() == literal)
    //     {
    //         fmt::print("IRProgram.get<{}, {}>: found existing entry\n", typeid(T).name(),
    //         typeid(U).name()); return &table[i];
    //     }

    fmt::print("IRProgram.get<{}, {}>: creating new entry\n", typeid(T).name(), typeid(U).name());
    table.emplace_back(T { std::forward<U>(literal) });
    for (auto const& elem: literal)
        fmt::print(" - {}\n", elem->to_string());
    return &table.back();
}

// {{{ inlines
inline NativeCallback& NativeCallback::returnType(LiteralType type)
{
    _signature.setReturnType(type);
    return *this;
}

template <>
inline NativeCallback& NativeCallback::param<bool>(const std::string& name)
{
    _signature.args().push_back(LiteralType::Boolean);
    _names.push_back(name);
    _defaults.emplace_back(std::monostate {});

    return *this;
}

template <>
inline NativeCallback& NativeCallback::param<bool>(const std::string& name, bool defaultValue)
{
    _signature.args().push_back(LiteralType::Boolean);
    _names.push_back(name);
    _defaults.emplace_back(defaultValue);

    return *this;
}

template <>
inline NativeCallback& NativeCallback::param<CoreNumber>(const std::string& name)
{
    _signature.args().push_back(LiteralType::Number);
    _names.push_back(name);
    _defaults.emplace_back(std::monostate {});

    return *this;
}

template <>
inline NativeCallback& NativeCallback::param<CoreNumber>(const std::string& name, CoreNumber defaultValue)
{
    _signature.args().push_back(LiteralType::Number);
    _names.push_back(name);
    _defaults.emplace_back(defaultValue);

    return *this;
}

template <>
inline NativeCallback& NativeCallback::param<int>(const std::string& name)
{
    _signature.args().push_back(LiteralType::Number);
    _names.push_back(name);
    _defaults.emplace_back(std::monostate {});

    return *this;
}

template <>
inline NativeCallback& NativeCallback::param<int>(const std::string& name, int defaultValue)
{
    _signature.args().push_back(LiteralType::Number);
    _names.push_back(name);
    _defaults.emplace_back(CoreNumber { defaultValue });

    return *this;
}

template <>
inline NativeCallback& NativeCallback::param<CoreString>(const std::string& name)
{
    _signature.args().push_back(LiteralType::String);
    _names.push_back(name);
    _defaults.emplace_back(std::monostate {});

    return *this;
}

template <>
inline NativeCallback& NativeCallback::param<CoreString>(const std::string& name, CoreString defaultValue)
{
    _signature.args().push_back(LiteralType::String);
    _names.push_back(name);
    _defaults.emplace_back(defaultValue);

    return *this;
}

template <>
inline NativeCallback& NativeCallback::param<util::IPAddress>(const std::string& name)
{
    _signature.args().push_back(LiteralType::IPAddress);
    _names.push_back(name);
    _defaults.emplace_back(std::monostate {});

    return *this;
}

template <>
inline NativeCallback& NativeCallback::param<util::IPAddress>(const std::string& name,
                                                              util::IPAddress defaultValue)
{
    _signature.args().push_back(LiteralType::IPAddress);
    _names.push_back(name);
    _defaults.emplace_back(defaultValue);

    return *this;
}

template <>
inline NativeCallback& NativeCallback::param<util::Cidr>(const std::string& name)
{
    _signature.args().push_back(LiteralType::Cidr);
    _names.push_back(name);
    _defaults.emplace_back(std::monostate {});

    return *this;
}

template <>
inline NativeCallback& NativeCallback::param<util::Cidr>(const std::string& name, util::Cidr defaultValue)
{
    _signature.args().push_back(LiteralType::Cidr);
    _names.push_back(name);
    _defaults.emplace_back(defaultValue);

    return *this;
}

template <>
inline NativeCallback& NativeCallback::param<util::RegExp>(const std::string& name)
{
    _signature.args().push_back(LiteralType::RegExp);
    _names.push_back(name);
    _defaults.emplace_back(std::monostate {});

    return *this;
}

template <>
inline NativeCallback& NativeCallback::param<util::RegExp>(const std::string& name, util::RegExp defaultValue)
{
    _signature.args().push_back(LiteralType::RegExp);
    _names.push_back(name);
    _defaults.emplace_back(defaultValue);

    return *this;
}

template <>
inline NativeCallback& NativeCallback::param<CoreIntArray>(const std::string& name)
{
    assert(_defaults.size() == _names.size());

    _signature.args().push_back(LiteralType::IntArray);
    _names.push_back(name);
    _defaults.emplace_back(std::monostate {});

    return *this;
}

template <>
inline NativeCallback& NativeCallback::param<CoreStringArray>(const std::string& name)
{
    assert(_defaults.size() == _names.size());

    _signature.args().push_back(LiteralType::StringArray);
    _names.push_back(name);
    _defaults.emplace_back(std::monostate {});

    return *this;
}

// ------------------------------------------------------------------------------------------

template <typename... Args>
inline NativeCallback& NativeCallback::params(Args... args)
{
    _signature.setArgs({ args... });
    return *this;
}

inline NativeCallback& NativeCallback::verifier(const Verifier& vf)
{
    _verifier = vf;
    return *this;
}

template <typename Class>
inline NativeCallback& NativeCallback::verifier(bool (Class::*method)(Instr*, IRBuilder*), Class* obj)
{
    _verifier = std::bind(method, obj, std::placeholders::_1, std::placeholders::_2);
    return *this;
}

template <typename Class>
inline NativeCallback& NativeCallback::verifier(bool (Class::*method)(Instr*, IRBuilder*))
{
    _verifier =
        std::bind(method, static_cast<Class*>(_runtime), std::placeholders::_1, std::placeholders::_2);
    return *this;
}

inline bool NativeCallback::verify(Instr* call, IRBuilder* irBuilder)
{
    if (!_verifier)
        return true;

    return _verifier(call, irBuilder);
}

inline NativeCallback& NativeCallback::bind(const Functor& cb)
{
    _function = cb;
    return *this;
}

template <typename Class>
inline NativeCallback& NativeCallback::bind(void (Class::*method)(Params&), Class* obj)
{
    _function = std::bind(method, obj, std::placeholders::_1);
    return *this;
}

template <typename Class>
inline NativeCallback& NativeCallback::bind(void (Class::*method)(Params&))
{
    _function =
        std::bind(method, static_cast<Class*>(_runtime), std::placeholders::_1, std::placeholders::_2);
    return *this;
}

inline bool NativeCallback::parametersNamed() const
{
    return !_names.empty();
}

inline const std::string& NativeCallback::getParamNameAt(size_t i) const
{
    assert(i < _names.size());
    return _names[i];
}

inline const NativeCallback::DefaultValue& NativeCallback::getDefaultParamAt(size_t i) const
{
    assert(i < _defaults.size());
    return _defaults[i];
}
// }}}


using StackPointer = size_t;

class TargetCodeGenerator: public InstructionVisitor
{
  public:
    TargetCodeGenerator();

    std::unique_ptr<Program> generate(IRProgram* program);

  protected:
    void generate(IRHandler* handler);

    void dumpCurrentStack();

    /**
     * Ensures @p value is available on top of the stack.
     *
     * May emit a LOAD instruction if stack[sp] is not on top of the stack.
     */
    void emitLoad(Value* value);

    void emitInstr(Opcode opc) { emitInstr(makeInstruction(opc)); }
    void emitInstr(Opcode opc, Operand op1) { emitInstr(makeInstruction(opc, op1)); }
    void emitInstr(Opcode opc, Operand op1, Operand op2) { emitInstr(makeInstruction(opc, op1, op2)); }
    void emitInstr(Opcode opc, Operand op1, Operand op2, Operand op3)
    {
        emitInstr(makeInstruction(opc, op1, op2, op3));
    }
    void emitInstr(Instruction instr);

    /**
     * Emits conditional jump instruction.
     *
     * @param opcode Opcode for the conditional jump.
     * @param bb Target basic block to jump to by \p opcode.
     *
     * This function will just emit a placeholder and will remember the
     * instruction pointer and passed operands for later back-patching once all
     * basic block addresses have been computed.
     */
    void emitCondJump(Opcode opcode, BasicBlock* bb);

    /**
     * Emits unconditional jump instruction.
     *
     * @param bb Target basic block to jump to by \p opcode.
     *
     * This function will just emit a placeholder and will remember the
     * instruction pointer and passed operands for later back-patching once all
     * basic block addresses have been computed.
     */
    void emitJump(BasicBlock* bb);

    void emitBinaryAssoc(Instr& instr, Opcode opcode);
    void emitBinary(Instr& instr, Opcode opcode);
    void emitUnary(Instr& instr, Opcode opcode);

    Operand getConstantInt(Value* value);

    /**
     * Retrieves the instruction pointer of the next instruction to be emitted.
     */
    size_t getInstructionPointer() const { return _code.size(); }

    /**
     * Finds given variable on global storage and returns its absolute offset if found or -1 if not.
     */
    std::optional<size_t> findGlobal(const Value* variable) const;

    /**
     * Retrieves the current number of elements on the stack.
     *
     * This also means, this value will be the absolute index for the next value
     * to be placed on top of the stack.
     */
    StackPointer getStackPointer() const { return _stack.size(); }

    /** Locates given @p value on the stack.
     */
    StackPointer getStackPointer(const Value* value);

    void changeStack(size_t pops, const Value* pushValue);
    void pop(size_t count);
    void push(const Value* alias);

    void visit(NopInstr& instr) override;

    // storage
    void visit(AllocaInstr& instr) override;
    void visit(StoreInstr& instr) override;
    void visit(LoadInstr& instr) override;
    void visit(PhiNode& instr) override;

    // calls
    void visit(CallInstr& instr) override;
    void visit(HandlerCallInstr& instr) override;

    // terminator
    void visit(CondBrInstr& instr) override;
    void visit(BrInstr& instr) override;
    void visit(RetInstr& instr) override;
    void visit(MatchInstr& instr) override;

    // regexp
    void visit(RegExpGroupInstr& instr) override;

    // type cast
    void visit(CastInstr& instr) override;

    // numeric
    void visit(INegInstr& instr) override;
    void visit(INotInstr& instr) override;
    void visit(IAddInstr& instr) override;
    void visit(ISubInstr& instr) override;
    void visit(IMulInstr& instr) override;
    void visit(IDivInstr& instr) override;
    void visit(IRemInstr& instr) override;
    void visit(IPowInstr& instr) override;
    void visit(IAndInstr& instr) override;
    void visit(IOrInstr& instr) override;
    void visit(IXorInstr& instr) override;
    void visit(IShlInstr& instr) override;
    void visit(IShrInstr& instr) override;
    void visit(ICmpEQInstr& instr) override;
    void visit(ICmpNEInstr& instr) override;
    void visit(ICmpLEInstr& instr) override;
    void visit(ICmpGEInstr& instr) override;
    void visit(ICmpLTInstr& instr) override;
    void visit(ICmpGTInstr& instr) override;

    // boolean
    void visit(BNotInstr& instr) override;
    void visit(BAndInstr& instr) override;
    void visit(BOrInstr& instr) override;
    void visit(BXorInstr& instr) override;

    // string
    void visit(SLenInstr& instr) override;
    void visit(SIsEmptyInstr& instr) override;
    void visit(SAddInstr& instr) override;
    void visit(SSubStrInstr& instr) override;
    void visit(SCmpEQInstr& instr) override;
    void visit(SCmpNEInstr& instr) override;
    void visit(SCmpLEInstr& instr) override;
    void visit(SCmpGEInstr& instr) override;
    void visit(SCmpLTInstr& instr) override;
    void visit(SCmpGTInstr& instr) override;
    void visit(SCmpREInstr& instr) override;
    void visit(SCmpBegInstr& instr) override;
    void visit(SCmpEndInstr& instr) override;
    void visit(SInInstr& instr) override;

    // ip
    void visit(PCmpEQInstr& instr) override;
    void visit(PCmpNEInstr& instr) override;
    void visit(PInCidrInstr& instr) override;

  private:
    struct ConditionalJump
    {
        size_t pc;
        Opcode opcode;
    };

    struct UnconditionalJump
    {
        size_t pc;
        Opcode opcode;
    };

    //! list of raised errors during code generation.
    std::vector<std::string> _errors;

    std::unordered_map<BasicBlock*, std::list<ConditionalJump>> _conditionalJumps;
    std::unordered_map<BasicBlock*, std::list<UnconditionalJump>> _unconditionalJumps;
    std::list<std::pair<MatchInstr*, size_t /*matchId*/>> _matchHints;

    size_t _handlerId;              //!< current handler's ID
    std::vector<Instruction> _code; //!< current handler's code

    /** target stack during target code generation */
    std::deque<const Value*> _stack;

    /** global scope mapping */
    std::deque<const Value*> _globals;

    // target program output
    ConstantPool _cp;
};


/** Implements SMATCHEQ instruction. */
class MatchSame: public Match
{
  public:
    MatchSame(const MatchDef& def, Program* program);

    uint64_t evaluate(const CoreString* condition, Runner* env) const override;

  private:
    std::unordered_map<CoreString, uint64_t> _map;
};

/** Implements SMATCHBEG instruction. */
class MatchHead: public Match
{
  public:
    MatchHead(const MatchDef& def, Program* program);

    uint64_t evaluate(const CoreString* condition, Runner* env) const override;

  private:
    util::PrefixTree<CoreString, uint64_t> _map;
};

/** Implements SMATCHBEG instruction. */
class MatchTail: public Match
{
  public:
    MatchTail(const MatchDef& def, Program* program);

    uint64_t evaluate(const CoreString* condition, Runner* env) const override;

  private:
    util::SuffixTree<CoreString, uint64_t> _map;
};

/** Implements SMATCHR instruction. */
class MatchRegEx: public Match
{
  public:
    MatchRegEx(const MatchDef& def, Program* program);

    uint64_t evaluate(const CoreString* condition, Runner* env) const override;

  private:
    std::vector<std::pair<util::RegExp, uint64_t>> _map;
};


} // namespace CoreVM

export namespace CoreVM::transform
{

/**
 * Eliminates empty blocks, that are just jumping to the next block.
 */
bool emptyBlockElimination(IRHandler* handler);
bool rewriteCondBrToSameBranches(IRHandler* handler);
bool eliminateUnusedInstr(IRHandler* handler);
bool eliminateLinearBr(IRHandler* handler);
bool foldConstantCondBr(IRHandler* handler);
bool rewriteBrToExit(IRHandler* handler);

/**
 * Merges equal blocks into one, eliminating duplicated blocks.
 *
 * A block is equal if their instructions and their successors are equal.
 */
bool mergeSameBlocks(IRHandler* handler);

/**
 * Eliminates empty blocks, that are just jumping to the next block.
 */
bool eliminateUnusedBlocks(IRHandler* handler);

} // namespace CoreVM::transform


export namespace CoreVM::diagnostics
{

enum class Type
{
    TokenError,
    SyntaxError,
    TypeError,
    Warning,
    LinkError
};

struct Message
{
    Type type;
    SourceLocation sourceLocation;
    std::string text;

    Message(Type ty, SourceLocation sl, std::string t):
        type { ty }, sourceLocation { std::move(sl) }, text { std::move(t) }
    {
    }

    [[nodiscard]] std::string string() const;

    bool operator==(const Message& other) const noexcept;
    bool operator!=(const Message& other) const noexcept { return !(*this == other); }
};

class Report
{
  public:
    virtual ~Report() = default;

    template <typename... Args>
    void tokenError(const SourceLocation& sloc, const std::string& f, Args... args)
    {
        emplace_back(Type::TokenError, sloc, fmt::format(f, std::move(args)...));
    }

    template <typename... Args>
    void syntaxError(const SourceLocation& sloc, fmt::format_string<Args...> f, Args... args)
    {
        emplace_back(Type::SyntaxError, sloc, fmt::format(f, std::move(args)...));
    }

    template <typename... Args>
    void typeError(const SourceLocation& sloc, fmt::format_string<Args...> f, Args... args)
    {
        emplace_back(Type::TypeError, sloc, fmt::format(f, std::move(args)...));
    }

    template <typename... Args>
    void warning(const SourceLocation& sloc, fmt::format_string<Args...> f, Args... args)
    {
        emplace_back(Type::Warning, sloc, fmt::format(f, std::move(args)...));
    }

    template <typename... Args>
    void linkError(fmt::format_string<Args...> f, Args... args)
    {
        emplace_back(Type::LinkError, SourceLocation {}, fmt::vformat(f, fmt::make_format_args(args)...));
    }

    void emplace_back(Type ty, SourceLocation sl, std::string t)
    {
        push_back(Message(ty, std::move(sl), std::move(t)));
    }

    virtual void push_back(Message msg) = 0;
    [[nodiscard]] virtual bool containsFailures() const noexcept = 0;
};

using MessageList = std::vector<Message>;

class BufferedReport: public Report
{
  public:
    void push_back(Message msg) override;
    [[nodiscard]] bool containsFailures() const noexcept override;

    void log() const;

    [[nodiscard]] const MessageList& messages() const noexcept { return _messages; }

    void clear();
    [[nodiscard]] size_t size() const noexcept { return _messages.size(); }
    const Message& operator[](size_t i) const { return _messages[i]; }

    using iterator = MessageList::iterator;
    using const_iterator = MessageList::const_iterator;
    iterator begin() noexcept { return _messages.begin(); }
    iterator end() noexcept { return _messages.end(); }
    [[nodiscard]] const_iterator begin() const noexcept { return _messages.begin(); }
    [[nodiscard]] const_iterator end() const noexcept { return _messages.end(); }

    [[nodiscard]] bool contains(const Message& m) const noexcept;

    bool operator==(const BufferedReport& other) const noexcept;
    bool operator!=(const BufferedReport& other) const noexcept { return !(*this == other); }

  private:
    MessageList _messages;
};

class ConsoleReport: public Report
{
  public:
    ConsoleReport();

    void push_back(Message msg) override;
    [[nodiscard]] bool containsFailures() const noexcept override;

  private:
    size_t _errorCount;
};

std::ostream& operator<<(std::ostream& os, const Report& report);

using DifferenceReport = std::pair<MessageList, MessageList>;

DifferenceReport difference(const BufferedReport& first, const BufferedReport& second);

class DiagnosticsError: public std::runtime_error
{
  public:
    explicit DiagnosticsError(SourceLocation sloc, const std::string& msg):
        std::runtime_error { msg }, _sloc { std::move(sloc) }
    {
    }

    [[nodiscard]] const SourceLocation& sourceLocation() const noexcept { return _sloc; }

  private:
    SourceLocation _sloc;
};

class LexerError: public DiagnosticsError
{
  public:
    LexerError(SourceLocation sloc, const std::string& msg): DiagnosticsError { std::move(sloc), msg } {}
};

class SyntaxError: public DiagnosticsError
{
  public:
    SyntaxError(SourceLocation sloc, const std::string& msg): DiagnosticsError { std::move(sloc), msg } {}
};

class TypeError: public DiagnosticsError
{
  public:
    TypeError(SourceLocation sloc, const std::string& msg): DiagnosticsError { std::move(sloc), msg } {}
};

} // namespace CoreVM::diagnostics

export namespace std
{

template <>
struct hash<CoreVM::util::Cidr>
{
    size_t operator()(const CoreVM::util::Cidr& v) const noexcept
    {
        // TODO: let it honor IPv6 better
        return static_cast<size_t>(*(uint32_t*) (v.address().data()) + v.prefix());
    }
};

template <>
struct hash<CoreVM::LiteralType>
{
    uint32_t operator()(CoreVM::LiteralType v) const noexcept { return static_cast<uint32_t>(v); }
};

} // namespace std

export namespace fmt
{
template <>
struct formatter<CoreVM::util::Cidr>: formatter<std::string>
{
    auto format(CoreVM::util::Cidr const& value, format_context& ctx) -> format_context::iterator
    {
        return formatter<std::string>::format(value.str(), ctx);
    }
};



template <>
struct formatter<CoreVM::Signature>
{
    template <typename ParseContext>
    auto parse(ParseContext& ctx)
    {
        return ctx.begin();
    }

    template <typename FormatContext>
    auto format(const CoreVM::Signature& v, FormatContext& ctx)
    {
        return format_to(ctx.begin(), v.to_s());
    }
};


} // namespace fmt

export
{
    template <>
    struct fmt::formatter<CoreVM::util::RegExp>: fmt::formatter<std::string>
    {
        using RegExp = CoreVM::util::RegExp;

        auto format(RegExp const& v, format_context& ctx) -> format_context::iterator
        {
            return formatter<std::string>::format(v.pattern(), ctx);
        }
    };

    template <>
    struct fmt::formatter<CoreVM::diagnostics::Type>: fmt::formatter<std::string_view>
    {
        using Type = CoreVM::diagnostics::Type;

        auto format(const Type& value, format_context& ctx) -> format_context::iterator
        {
            string_view name;
            switch (value)
            {
                case Type::TokenError: name = "TokenError"; break;
                case Type::SyntaxError: name = "SyntaxError"; break;
                case Type::TypeError: name = "TypeError"; break;
                case Type::Warning: name = "Warning"; break;
                case Type::LinkError: name = "LinkError"; break;
            }
            return formatter<string_view>::format(name, ctx);
        }
    };

    template <>
    struct fmt::formatter<CoreVM::diagnostics::Message>: fmt::formatter<std::string>
    {
        using Message = CoreVM::diagnostics::Message;

        auto format(const Message& value, format_context& ctx) -> format_context::iterator
        {
            return formatter<std::string>::format(value.string(), ctx);
        }
    };

    template <>
    struct fmt::formatter<CoreVM::LiteralType>: fmt::formatter<std::string_view>
    {
        using LiteralType = CoreVM::LiteralType;

        auto format(LiteralType id, format_context& ctx) -> format_context::iterator
        {
            std::string_view name;
            switch (id)
            {
                case LiteralType::Void: name = "Void"; break;
                case LiteralType::Boolean: name = "Boolean"; break;
                case LiteralType::Number: name = "Number"; break;
                case LiteralType::String: name = "String"; break;
                case LiteralType::IPAddress: name = "IPAddress"; break;
                case LiteralType::Cidr: name = "Cidr"; break;
                case LiteralType::RegExp: name = "RegExp"; break;
                case LiteralType::Handler: name = "Handler"; break;
                case LiteralType::IntArray: name = "IntArray"; break;
                case LiteralType::StringArray: name = "StringArray"; break;
                case LiteralType::IPAddrArray: name = "IPAddrArray"; break;
                case LiteralType::CidrArray: name = "CidrArray"; break;
                case LiteralType::IntPair: name = "IntPair"; break;
            }
            return formatter<std::string_view>::format(name, ctx);
        }
    };


template <>
struct fmt::formatter<CoreVM::FilePos>: fmt::formatter<std::string>
{
    auto format(const CoreVM::FilePos& value, format_context& ctx) -> format_context::iterator
    {
        return formatter<std::string>::format(fmt::format("{}:{}", value.line, value.column), ctx);
    }
};

template <>
struct fmt::formatter<CoreVM::SourceLocation>: fmt::formatter<std::string>
{
    auto format(const CoreVM::SourceLocation& value, format_context& ctx) -> format_context::iterator
    {
        if (!value.filename.empty())
            return formatter<std::string>::format(fmt::format("{}:{}", value.filename, value.begin), ctx);
        else
            return formatter<std::string>::format(fmt::format("{}", value.begin), ctx);
    }
};


}
