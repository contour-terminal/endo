// SPDX-License-Identifier: Apache-2.0

#include <CoreVM/ir/BasicBlock.h>
#include <CoreVM/ir/ConstantArray.h>
#include <CoreVM/ir/ConstantValue.h>
#include <CoreVM/ir/IRBuiltinFunction.h>
#include <CoreVM/ir/IRBuiltinHandler.h>
#include <CoreVM/ir/Instr.h>

#include <fmt/format.h>

#include <cassert>
#include <cinttypes>
#include <sstream>
#include <utility>

namespace CoreVM
{

Instr::Instr(const Instr& v): Value(v), _basicBlock(nullptr), _operands(v._operands)
{
    for (Value* op: _operands)
    {
        if (op)
        {
            op->addUse(this);
        }
    }
}

Instr::Instr(LiteralType ty, const std::vector<Value*>& ops, const std::string& name):
    Value(ty, name), _basicBlock(nullptr), _operands(ops)
{
    for (Value* op: _operands)
    {
        if (op)
        {
            op->addUse(this);
        }
    }
}

Instr::~Instr()
{
    for (Value* op: _operands)
    {
        if (op != nullptr)
        {
            // remove this instruction as user of that operand
            op->removeUse(this);

            // if operand is a BasicBlock, unlink it as successor
            if (BasicBlock* parent = getBasicBlock(); parent != nullptr)
            {
                if (BasicBlock* oldBB = dynamic_cast<BasicBlock*>(op))
                {
                    parent->unlinkSuccessor(oldBB);
                }
            }
        }
    }
}

void Instr::addOperand(Value* value)
{
    _operands.push_back(value);

    value->addUse(this);

    if (BasicBlock* newBB = dynamic_cast<BasicBlock*>(value))
    {
        getBasicBlock()->linkSuccessor(newBB);
    }
}

Value* Instr::setOperand(size_t i, Value* value)
{
    Value* old = _operands[i];
    assert(old != value && "cannot set operand to itself");
    _operands[i] = value;

    if (old)
    {
        old->removeUse(this);

        if (BasicBlock* oldBB = dynamic_cast<BasicBlock*>(old))
        {
            getBasicBlock()->unlinkSuccessor(oldBB);
        }
    }

    if (value)
    {
        value->addUse(this);

        if (BasicBlock* newBB = dynamic_cast<BasicBlock*>(value))
        {
            getBasicBlock()->linkSuccessor(newBB);
        }
    }

    return old;
}

size_t Instr::replaceOperand(Value* old, Value* replacement)
{
    assert(old != replacement && "cannot replace operand with itself");
    size_t count = 0;

    for (size_t i = 0, e = _operands.size(); i != e; ++i)
    {
        if (_operands[i] == old)
        {
            setOperand(i, replacement);
            ++count;
        }
    }

    return count;
}

void Instr::clearOperands()
{
    for (size_t i = 0, e = _operands.size(); i != e; ++i)
        setOperand(i, nullptr);

    _operands.clear();
}

std::unique_ptr<Instr> Instr::replace(std::unique_ptr<Instr> newInstr)
{
    if (_basicBlock)
    {
        return _basicBlock->replace(this, std::move(newInstr));
    }
    else
    {
        return nullptr;
    }
}

void Instr::dumpOne(const char* mnemonic)
{
    fmt::print("\t{}\n", formatOne(mnemonic));
}

std::string Instr::formatOne(std::string mnemonic) const
{
    std::stringstream sstr;

    if (type() == LiteralType::Void)
        sstr << mnemonic;
    else if (name().empty())
        sstr << fmt::format("%??? = {}", mnemonic);
    else
        sstr << fmt::format("%{} = {}", name(), mnemonic);

    for (size_t i = 0, e = _operands.size(); i != e; ++i)
    {
        sstr << (i ? ", " : " ");
        Value* arg = _operands[i];
        if (dynamic_cast<Constant*>(arg))
        {
            if (auto* i = dynamic_cast<ConstantInt*>(arg))
                sstr << i->get();
            else if (auto* i = dynamic_cast<ConstantBoolean*>(arg))
                sstr << (i->get() ? "true" : "false");
            else if (auto* s = dynamic_cast<ConstantString*>(arg))
                sstr << '"' << s->get() << '"';
            else if (auto* ip = dynamic_cast<ConstantIP*>(arg))
                sstr << ip->get().c_str();
            else if (auto* cidr = dynamic_cast<ConstantCidr*>(arg))
                sstr << cidr->get().str();
            else if (auto* re = dynamic_cast<ConstantRegExp*>(arg))
                sstr << '/' << re->get().pattern() << '/';
            else if (auto* bh = dynamic_cast<IRBuiltinHandler*>(arg))
                sstr << bh->signature().to_s();
            else if (auto* bf = dynamic_cast<IRBuiltinFunction*>(arg))
                sstr << bf->signature().to_s();
            else if (auto* ar = dynamic_cast<ConstantArray*>(arg))
            {
                sstr << '[';
                size_t i = 0;
                switch (ar->type())
                {
                    case LiteralType::IntArray:
                        for (const auto& v: ar->get())
                        {
                            if (i)
                                sstr << ", ";
                            sstr << static_cast<ConstantInt*>(v)->get();
                            ++i;
                        }
                        break;
                    case LiteralType::StringArray:
                        for (const auto& v: ar->get())
                        {
                            if (i)
                                sstr << ", ";
                            sstr << '"' << static_cast<ConstantString*>(v)->get() << '"';
                            ++i;
                        }
                        break;
                    case LiteralType::IPAddrArray:
                        for (const auto& v: ar->get())
                        {
                            if (i)
                                sstr << ", ";
                            sstr << static_cast<ConstantIP*>(v)->get().str();
                            ++i;
                        }
                        break;
                    case LiteralType::CidrArray:
                        for (const auto& v: ar->get())
                        {
                            if (i)
                                sstr << ", ";
                            sstr << static_cast<ConstantCidr*>(v)->get().str();
                            ++i;
                        }
                        break;
                    default: abort();
                }
                sstr << ']';
            }
            else
                sstr << fmt::format("?UnknownConstant({})", typeid(*arg).name());
        }
        else if (auto* bb = dynamic_cast<Instr*>(arg))
            sstr << '%' << arg->name();
        else if (auto* bb = dynamic_cast<BasicBlock*>(arg))
            sstr << '%' << arg->name();
        else
            sstr << fmt::format(
                "?UnknownValue({}): name={}, parent={}", typeid(*arg).name(), arg->to_string(), name());
    }

    // XXX sometimes u're interested in the name of the instr, even though it
    // doesn't yield a result value on the stack
    // if (type() == LiteralType::Void) {
    //   sstr << "\t; (%" << name() << ")";
    // }

    return sstr.str();
}

} // namespace CoreVM
