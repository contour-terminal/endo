// SPDX-License-Identifier: Apache-2.0
#include <CoreVM/CoreVM.hpp>

#include <crispy/assert.h>

#include <cassert>
#include <format>
#include <ranges>
#include <utility> // make_pair

namespace CoreVM
{

template <typename T, typename U>
inline std::vector<U> join(const T& a, const std::vector<U>& vec) // {{{
{
    std::vector<U> res;

    res.push_back(a);
    for (const U& v: vec)
        res.push_back(v);

    return std::move(res);
}

// }}}
const char* cstr(UnaryOperator op) // {{{
{
    switch (op)
    {
        case UnaryOperator::INeg: return "ineg";
        case UnaryOperator::INot: return "inot";
        case UnaryOperator::BNot: return "bnot";
        case UnaryOperator::SLen: return "slen";
        case UnaryOperator::SIsEmpty: return "sisempty";
        case UnaryOperator::FNeg: return "fneg";
    }
    crispy::unreachable();
}

// }}}
const char* cstr(BinaryOperator op) // {{{
{
    switch (op)
    {
        // numerical
        case BinaryOperator::IAdd: return "iadd";
        case BinaryOperator::ISub: return "isub";
        case BinaryOperator::IMul: return "imul";
        case BinaryOperator::IDiv: return "idiv";
        case BinaryOperator::IRem: return "irem";
        case BinaryOperator::IPow: return "ipow";
        case BinaryOperator::IAnd: return "iand";
        case BinaryOperator::IOr: return "ior";
        case BinaryOperator::IXor: return "ixor";
        case BinaryOperator::IShl: return "ishl";
        case BinaryOperator::IShr: return "ishr";
        case BinaryOperator::ICmpEQ: return "icmpeq";
        case BinaryOperator::ICmpNE: return "icmpne";
        case BinaryOperator::ICmpLE: return "icmple";
        case BinaryOperator::ICmpGE: return "icmpge";
        case BinaryOperator::ICmpLT: return "icmplt";
        case BinaryOperator::ICmpGT: return "icmpgt";
        // boolean
        case BinaryOperator::BAnd: return "band";
        case BinaryOperator::BOr: return "bor";
        case BinaryOperator::BXor: return "bxor";
        // string
        case BinaryOperator::SAdd: return "sadd";
        case BinaryOperator::SSubStr: return "ssubstr";
        case BinaryOperator::SCmpEQ: return "scmpeq";
        case BinaryOperator::SCmpNE: return "scmpne";
        case BinaryOperator::SCmpLE: return "scmple";
        case BinaryOperator::SCmpGE: return "scmpge";
        case BinaryOperator::SCmpLT: return "scmplt";
        case BinaryOperator::SCmpGT: return "scmpgt";
        case BinaryOperator::SCmpRE: return "scmpre";
        case BinaryOperator::SCmpBeg: return "scmpbeg";
        case BinaryOperator::SCmpEnd: return "scmpend";
        case BinaryOperator::SIn: return "sin";
        // ip
        case BinaryOperator::PCmpEQ: return "pcmpeq";
        case BinaryOperator::PCmpNE: return "pcmpne";
        case BinaryOperator::PInCidr: return "pincidr";
        // float
        case BinaryOperator::FAdd: return "fadd";
        case BinaryOperator::FSub: return "fsub";
        case BinaryOperator::FMul: return "fmul";
        case BinaryOperator::FDiv: return "fdiv";
        case BinaryOperator::FRem: return "frem";
        case BinaryOperator::FPow: return "fpow";
        case BinaryOperator::FCmpEQ: return "fcmpeq";
        case BinaryOperator::FCmpNE: return "fcmpne";
        case BinaryOperator::FCmpLE: return "fcmple";
        case BinaryOperator::FCmpGE: return "fcmpge";
        case BinaryOperator::FCmpLT: return "fcmplt";
        case BinaryOperator::FCmpGT: return "fcmpgt";
        default: return "?";
    };
}

// }}}
// {{{ NopInstr
std::string NopInstr::to_string() const
{
    return formatOne("nop");
}

std::unique_ptr<Instr> NopInstr::clone()
{
    return std::make_unique<NopInstr>();
}

void NopInstr::accept(InstructionVisitor& v)
{
    v.visit(*this);
}

// }}}
// {{{ CastInstr
std::string CastInstr::to_string() const
{
    return formatOne("cast " + tos(type()));
}

std::unique_ptr<Instr> CastInstr::clone()
{
    return std::make_unique<CastInstr>(type(), source(), name());
}

void CastInstr::accept(InstructionVisitor& v)
{
    v.visit(*this);
}

// }}}
// {{{ CondBrInstr
CondBrInstr::CondBrInstr(Value* cond, BasicBlock* trueBlock, BasicBlock* falseBlock):
    TerminateInstr({ cond, trueBlock, falseBlock })
{
}

std::string CondBrInstr::to_string() const
{
    return formatOne("condbr");
}

std::unique_ptr<Instr> CondBrInstr::clone()
{
    return std::make_unique<CondBrInstr>(condition(), trueBlock(), falseBlock());
}

void CondBrInstr::accept(InstructionVisitor& visitor)
{
    visitor.visit(*this);
}

// }}}
// {{{ BrInstr
BrInstr::BrInstr(BasicBlock* targetBlock): TerminateInstr({ targetBlock })
{
}

std::string BrInstr::to_string() const
{
    return formatOne("br");
}

std::unique_ptr<Instr> BrInstr::clone()
{
    return std::make_unique<BrInstr>(targetBlock());
}

void BrInstr::accept(InstructionVisitor& visitor)
{
    visitor.visit(*this);
}

// }}}
// {{{ MatchInstr
MatchInstr::MatchInstr(MatchClass op, Value* cond): TerminateInstr({ cond, nullptr }), _op(op)
{
}

void MatchInstr::addCase(Constant* label, BasicBlock* code)
{
    addOperand(label);
    addOperand(code);
}

void MatchInstr::setElseBlock(BasicBlock* code)
{
    setOperand(1, code);
}

BasicBlock* MatchInstr::elseBlock() const
{
    return static_cast<BasicBlock*>(operand(1));
}

std::string MatchInstr::to_string() const
{
    switch (op())
    {
        case MatchClass::Same: return formatOne("match.same");
        case MatchClass::Head: return formatOne("match.head");
        case MatchClass::Tail: return formatOne("match.tail");
        case MatchClass::RegExp: return formatOne("match.re");
        default: abort();
    }
}

MatchInstr::MatchInstr(const MatchInstr& v): TerminateInstr(v), _op(v.op())
{
}

std::unique_ptr<Instr> MatchInstr::clone()
{
    return std::make_unique<MatchInstr>(*this);
}

std::vector<std::pair<Constant*, BasicBlock*>> MatchInstr::cases() const
{
    std::vector<std::pair<Constant*, BasicBlock*>> out;

    size_t caseCount = (operands().size() - 2) / 2;

    for (auto const i: std::views::iota(0uz, caseCount))
    {
        auto* label = static_cast<Constant*>(operand(2 + (2 * i) + 0));
        auto* code = static_cast<BasicBlock*>(operand(2 + (2 * i) + 1));

        out.emplace_back(label, code);
    }

    return out;
}

void MatchInstr::accept(InstructionVisitor& visitor)
{
    visitor.visit(*this);
}

// }}}
// {{{ RetInstr
RetInstr::RetInstr(Value* result): TerminateInstr({ result })
{
}

std::string RetInstr::to_string() const
{
    return formatOne("ret");
}

std::unique_ptr<Instr> RetInstr::clone()
{
    return std::make_unique<RetInstr>(operand(0));
}

void RetInstr::accept(InstructionVisitor& visitor)
{
    visitor.visit(*this);
}

// }}}
// {{{ CallInstr
CallInstr::CallInstr(const std::vector<Value*>& args, const std::string& name):
    Instr(static_cast<IRBuiltinFunction*>(args[0])->signature().returnType(), args, name)
{
}

CallInstr::CallInstr(IRBuiltinFunction* callee, const std::vector<Value*>& args, const std::string& name):
    Instr(callee->signature().returnType(), join(callee, args), name)
{
}

std::string CallInstr::to_string() const
{
    return formatOne("call");
}

std::unique_ptr<Instr> CallInstr::clone()
{
    return std::make_unique<CallInstr>(operands(), name());
}

void CallInstr::accept(InstructionVisitor& visitor)
{
    visitor.visit(*this);
}

// }}}
// {{{ PhiNode
PhiNode::PhiNode(const std::vector<Value*>& ops, const std::string& name): Instr(ops[0]->type(), ops, name)
{
}

std::string PhiNode::to_string() const
{
    return formatOne("phi");
}

std::unique_ptr<Instr> PhiNode::clone()
{
    return std::make_unique<PhiNode>(operands(), name());
}

void PhiNode::accept(InstructionVisitor& visitor)
{
    visitor.visit(*this);
}

// }}}
// {{{ other instructions
void AllocaInstr::accept(InstructionVisitor& visitor)
{
    visitor.visit(*this);
}

void StoreInstr::accept(InstructionVisitor& visitor)
{
    visitor.visit(*this);
}

void LoadInstr::accept(InstructionVisitor& visitor)
{
    visitor.visit(*this);
}

std::string AllocaInstr::to_string() const
{
    return formatOne("alloca");
}

std::unique_ptr<Instr> AllocaInstr::clone()
{
    return std::make_unique<AllocaInstr>(type(), operand(0), name());
}

std::string LoadInstr::to_string() const
{
    return formatOne("load");
}

std::unique_ptr<Instr> LoadInstr::clone()
{
    return std::make_unique<LoadInstr>(variable(), name());
}

std::string StoreInstr::to_string() const
{
    return formatOne("store");
}

std::unique_ptr<Instr> StoreInstr::clone()
{
    return std::make_unique<StoreInstr>(variable(), index(), source(), name());
}

std::string RegExpGroupInstr::to_string() const
{
    return formatOne("reggroup");
}

std::unique_ptr<Instr> RegExpGroupInstr::clone()
{
    return std::make_unique<RegExpGroupInstr>(groupId(), name());
}

void RegExpGroupInstr::accept(InstructionVisitor& v)
{
    v.visit(*this);
}

// }}}
// {{{ Object instructions

// ObjAllocInstr
std::string ObjAllocInstr::to_string() const
{
    return formatOne("oalloc");
}

std::unique_ptr<Instr> ObjAllocInstr::clone()
{
    return std::make_unique<ObjAllocInstr>(typeId(), name());
}

void ObjAllocInstr::accept(InstructionVisitor& v)
{
    v.visit(*this);
}

// ObjRetainInstr
std::string ObjRetainInstr::to_string() const
{
    return formatOne("oretain");
}

std::unique_ptr<Instr> ObjRetainInstr::clone()
{
    return std::make_unique<ObjRetainInstr>(object(), name());
}

void ObjRetainInstr::accept(InstructionVisitor& v)
{
    v.visit(*this);
}

// ObjReleaseInstr
std::string ObjReleaseInstr::to_string() const
{
    return formatOne("orelease");
}

std::unique_ptr<Instr> ObjReleaseInstr::clone()
{
    return std::make_unique<ObjReleaseInstr>(storage(), name());
}

void ObjReleaseInstr::accept(InstructionVisitor& v)
{
    v.visit(*this);
}

// ObjGetTagInstr
std::string ObjGetTagInstr::to_string() const
{
    return formatOne("ogettag");
}

std::unique_ptr<Instr> ObjGetTagInstr::clone()
{
    return std::make_unique<ObjGetTagInstr>(object(), name());
}

void ObjGetTagInstr::accept(InstructionVisitor& v)
{
    v.visit(*this);
}

// ObjSetTagInstr
std::string ObjSetTagInstr::to_string() const
{
    return formatOne("osettag");
}

std::unique_ptr<Instr> ObjSetTagInstr::clone()
{
    return std::make_unique<ObjSetTagInstr>(object(), tag(), name());
}

void ObjSetTagInstr::accept(InstructionVisitor& v)
{
    v.visit(*this);
}

// ObjGetSlotInstr
std::string ObjGetSlotInstr::to_string() const
{
    return formatOne("ogetslot");
}

std::unique_ptr<Instr> ObjGetSlotInstr::clone()
{
    return std::make_unique<ObjGetSlotInstr>(object(), slotIndex(), name());
}

void ObjGetSlotInstr::accept(InstructionVisitor& v)
{
    v.visit(*this);
}

// ObjSetSlotInstr
std::string ObjSetSlotInstr::to_string() const
{
    return formatOne("osetslot");
}

std::unique_ptr<Instr> ObjSetSlotInstr::clone()
{
    return std::make_unique<ObjSetSlotInstr>(object(), slotIndex(), value(), name());
}

void ObjSetSlotInstr::accept(InstructionVisitor& v)
{
    v.visit(*this);
}

// ObjTypeIdInstr
std::string ObjTypeIdInstr::to_string() const
{
    return formatOne("otypeid");
}

std::unique_ptr<Instr> ObjTypeIdInstr::clone()
{
    return std::make_unique<ObjTypeIdInstr>(object(), name());
}

void ObjTypeIdInstr::accept(InstructionVisitor& v)
{
    v.visit(*this);
}

// ObjIsTypeInstr
std::string ObjIsTypeInstr::to_string() const
{
    return formatOne("oistype");
}

std::unique_ptr<Instr> ObjIsTypeInstr::clone()
{
    return std::make_unique<ObjIsTypeInstr>(object(), typeId(), name());
}

void ObjIsTypeInstr::accept(InstructionVisitor& v)
{
    v.visit(*this);
}

// VCmpEQInstr
std::string VCmpEQInstr::to_string() const
{
    return formatOne("vcmpeq");
}

std::unique_ptr<Instr> VCmpEQInstr::clone()
{
    return std::make_unique<VCmpEQInstr>(lhs(), rhs(), name());
}

void VCmpEQInstr::accept(InstructionVisitor& v)
{
    v.visit(*this);
}

// VCmpNEInstr
std::string VCmpNEInstr::to_string() const
{
    return formatOne("vcmpne");
}

std::unique_ptr<Instr> VCmpNEInstr::clone()
{
    return std::make_unique<VCmpNEInstr>(lhs(), rhs(), name());
}

void VCmpNEInstr::accept(InstructionVisitor& v)
{
    v.visit(*this);
}

// VCmpLTInstr
std::string VCmpLTInstr::to_string() const
{
    return formatOne("vcmplt");
}

std::unique_ptr<Instr> VCmpLTInstr::clone()
{
    return std::make_unique<VCmpLTInstr>(lhs(), rhs(), name());
}

void VCmpLTInstr::accept(InstructionVisitor& v)
{
    v.visit(*this);
}

// VCmpLEInstr
std::string VCmpLEInstr::to_string() const
{
    return formatOne("vcmple");
}

std::unique_ptr<Instr> VCmpLEInstr::clone()
{
    return std::make_unique<VCmpLEInstr>(lhs(), rhs(), name());
}

void VCmpLEInstr::accept(InstructionVisitor& v)
{
    v.visit(*this);
}

// VCmpGTInstr
std::string VCmpGTInstr::to_string() const
{
    return formatOne("vcmpgt");
}

std::unique_ptr<Instr> VCmpGTInstr::clone()
{
    return std::make_unique<VCmpGTInstr>(lhs(), rhs(), name());
}

void VCmpGTInstr::accept(InstructionVisitor& v)
{
    v.visit(*this);
}

// VCmpGEInstr
std::string VCmpGEInstr::to_string() const
{
    return formatOne("vcmpge");
}

std::unique_ptr<Instr> VCmpGEInstr::clone()
{
    return std::make_unique<VCmpGEInstr>(lhs(), rhs(), name());
}

void VCmpGEInstr::accept(InstructionVisitor& v)
{
    v.visit(*this);
}

// }}}

// {{{ FunctionCallInstr
FunctionCallInstr::FunctionCallInstr(IRFunction* callee,
                                     const std::vector<Value*>& args,
                                     const std::string& name,
                                     LiteralType returnType):
    Instr(returnType, args, name), _callee(callee)
{
}

std::string FunctionCallInstr::to_string() const
{
    return formatOne("ucall");
}

std::unique_ptr<Instr> FunctionCallInstr::clone()
{
    std::vector<Value*> args(operands().begin(), operands().end());
    return std::make_unique<FunctionCallInstr>(_callee, std::move(args), name());
}

void FunctionCallInstr::accept(InstructionVisitor& v)
{
    v.visit(*this);
}

// }}}
// {{{ FunctionRetInstr
FunctionRetInstr::FunctionRetInstr(Value* result, const std::string& name):
    Instr(LiteralType::Void, { result }, name)
{
}

std::string FunctionRetInstr::to_string() const
{
    return formatOne("uret");
}

std::unique_ptr<Instr> FunctionRetInstr::clone()
{
    return std::make_unique<FunctionRetInstr>(result(), name());
}

void FunctionRetInstr::accept(InstructionVisitor& v)
{
    v.visit(*this);
}

// }}}
// {{{ TailCallInstr
TailCallInstr::TailCallInstr(IRFunction* callee, const std::vector<Value*>& args, const std::string& name):
    Instr(LiteralType::Void, args, name), _callee(callee)
{
}

std::string TailCallInstr::to_string() const
{
    return formatOne("utcall");
}

std::unique_ptr<Instr> TailCallInstr::clone()
{
    std::vector<Value*> args(operands().begin(), operands().end());
    return std::make_unique<TailCallInstr>(_callee, std::move(args), name());
}

void TailCallInstr::accept(InstructionVisitor& v)
{
    v.visit(*this);
}

// }}}
// {{{ IndirectCallInstr
IndirectCallInstr::IndirectCallInstr(Value* callable, std::vector<Value*> args, const std::string& name):
    Instr(
        LiteralType::Void,
        [&]() {
            std::vector<Value*> ops;
            ops.reserve(1 + args.size());
            ops.push_back(callable);
            ops.insert(ops.end(), args.begin(), args.end());
            return ops;
        }(),
        name)
{
}

std::string IndirectCallInstr::to_string() const
{
    return formatOne("iucall");
}

std::unique_ptr<Instr> IndirectCallInstr::clone()
{
    std::vector<Value*> args(operands().begin() + 1, operands().end());
    return std::make_unique<IndirectCallInstr>(callable(), std::move(args), name());
}

void IndirectCallInstr::accept(InstructionVisitor& v)
{
    v.visit(*this);
}

// }}}
// {{{ IndirectTailCallInstr
IndirectTailCallInstr::IndirectTailCallInstr(Value* callable,
                                             std::vector<Value*> args,
                                             const std::string& name):
    Instr(
        LiteralType::Void,
        [&]() {
            std::vector<Value*> ops;
            ops.reserve(1 + args.size());
            ops.push_back(callable);
            ops.insert(ops.end(), args.begin(), args.end());
            return ops;
        }(),
        name)
{
}

std::string IndirectTailCallInstr::to_string() const
{
    return formatOne("iutcall");
}

std::unique_ptr<Instr> IndirectTailCallInstr::clone()
{
    std::vector<Value*> args(operands().begin() + 1, operands().end());
    return std::make_unique<IndirectTailCallInstr>(callable(), std::move(args), name());
}

void IndirectTailCallInstr::accept(InstructionVisitor& v)
{
    v.visit(*this);
}

// }}}
// {{{ FunctionRefInstr
std::string FunctionRefInstr::to_string() const
{
    return std::format("funcref {}", _function->name());
}

std::unique_ptr<Instr> FunctionRefInstr::clone()
{
    return std::make_unique<FunctionRefInstr>(_function, name());
}

void FunctionRefInstr::accept(InstructionVisitor& v)
{
    v.visit(*this);
}

// }}}
// {{{ LazyForceInstr
std::string LazyForceInstr::to_string() const
{
    return formatOne("lforce");
}

std::unique_ptr<Instr> LazyForceInstr::clone()
{
    return std::make_unique<LazyForceInstr>(lazyObj(), name());
}

void LazyForceInstr::accept(InstructionVisitor& v)
{
    v.visit(*this);
}

// }}}

} // namespace CoreVM
