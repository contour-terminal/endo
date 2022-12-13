// SPDX-License-Identifier: Apache-2.0

#include <CoreVM/ir/BasicBlock.h>
#include <CoreVM/ir/IRBuiltinFunction.h>
#include <CoreVM/ir/IRBuiltinHandler.h>
#include <CoreVM/ir/IRHandler.h>
#include <CoreVM/ir/Instr.h>
#include <CoreVM/ir/Instructions.h>
#include <CoreVM/util/assert.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <iterator>
#include <sstream>

/*
 * TODO assert() on last instruction in current BB is not a terminator instr.
 */

namespace CoreVM
{

BasicBlock::BasicBlock(const std::string& name, IRHandler* parent):
    Value(LiteralType::Void, name), _handler(parent)
{
}

BasicBlock::~BasicBlock()
{
    // XXX we *must* destruct the code in reverse order to ensure proper release
    // The validity checks will fail otherwise.
    while (!empty())
    {
        remove(back());
    }

    COREVM_ASSERT(_predecessors.empty(),
                  "Cannot remove a BasicBlock that another BasicBlock still refers to.");

    for (BasicBlock* bb: _predecessors)
    {
        bb->unlinkSuccessor(this);
    }

    for (BasicBlock* bb: _successors)
    {
        unlinkSuccessor(bb);
    }
}

TerminateInstr* BasicBlock::getTerminator() const
{
    return _code.empty() ? nullptr : dynamic_cast<TerminateInstr*>(_code.back().get());
}

std::unique_ptr<Instr> BasicBlock::remove(Instr* instr)
{
    // if we're removing the terminator instruction
    if (instr == getTerminator())
    {
        // then unlink all successors also
        for (Value* operand: instr->operands())
        {
            if (auto* bb = dynamic_cast<BasicBlock*>(operand))
            {
                unlinkSuccessor(bb);
            }
        }
    }

    auto i = std::find_if(_code.begin(), _code.end(), [&](const auto& obj) { return obj.get() == instr; });
    assert(i != _code.end());

    std::unique_ptr<Instr> removedInstr = std::move(*i);
    _code.erase(i);
    instr->setParent(nullptr);
    return removedInstr;
}

std::unique_ptr<Instr> BasicBlock::replace(Instr* oldInstr, std::unique_ptr<Instr> newInstr)
{
    assert(oldInstr->getBasicBlock() == this);
    assert(newInstr->getBasicBlock() == nullptr);

    oldInstr->replaceAllUsesWith(newInstr.get());

    if (oldInstr == getTerminator())
    {
        std::unique_ptr<Instr> removedInstr = remove(oldInstr);
        push_back(std::move(newInstr));
        return removedInstr;
    }
    else
    {
        assert(dynamic_cast<TerminateInstr*>(newInstr.get()) == nullptr
               && "Most not be a terminator instruction.");

        auto i =
            std::find_if(_code.begin(), _code.end(), [&](const auto& obj) { return obj.get() == oldInstr; });

        assert(i != _code.end());

        oldInstr->setParent(nullptr);
        newInstr->setParent(this);

        std::unique_ptr<Instr> removedInstr = std::move(*i);
        *i = std::move(newInstr);
        return removedInstr;
    }
}

Instr* BasicBlock::push_back(std::unique_ptr<Instr> instr)
{
    assert(instr != nullptr);
    assert(instr->getBasicBlock() == nullptr);

    // FIXME: do we need this? I'd say NOPE.
    setType(instr->type());

    instr->setParent(this);

    // are we're now adding the terminate instruction?
    if (dynamic_cast<TerminateInstr*>(instr.get()))
    {
        // then check for possible successors
        for (Value* operand: instr->operands())
        {
            if (auto* bb = dynamic_cast<BasicBlock*>(operand))
            {
                linkSuccessor(bb);
            }
        }
    }

    _code.push_back(std::move(instr));
    return _code.back().get();
}

void BasicBlock::merge_back(BasicBlock* bb)
{
    assert(getTerminator() == nullptr);

#if 0
  for (const std::unique_ptr<Instr>& instr : bb->_code) {
    push_back(instr->clone());
  }
#else
    for (std::unique_ptr<Instr>& instr: bb->_code)
    {
        instr->setParent(this);
        if (dynamic_cast<TerminateInstr*>(instr.get()))
        {
            // then check for possible successors
            for (Value* operand: instr->operands())
            {
                if (auto* succ = dynamic_cast<BasicBlock*>(operand))
                {
                    bb->unlinkSuccessor(succ);
                    linkSuccessor(succ);
                }
            }
        }
        _code.push_back(std::move(instr));
    }
    bb->_code.clear();
    for (BasicBlock* succ: bb->_successors)
    {
        unlinkSuccessor(succ);
    }
    bb->getHandler()->erase(bb);
#endif
}

void BasicBlock::moveAfter(const BasicBlock* otherBB)
{
    _handler->moveAfter(this, otherBB);
}

void BasicBlock::moveBefore(const BasicBlock* otherBB)
{
    _handler->moveBefore(this, otherBB);
}

bool BasicBlock::isAfter(const BasicBlock* otherBB) const
{
    return _handler->isAfter(this, otherBB);
}

void BasicBlock::dump()
{
    std::stringstream sstr;

    sstr << '%' << name() << ':';
    if (!_predecessors.empty())
    {
        sstr << " ; [preds: ";
        for (size_t i = 0, e = _predecessors.size(); i != e; ++i)
        {
            if (i)
                sstr << ", ";
            sstr << '%' << _predecessors[i]->name();
        }
        sstr << ']';
    }
    sstr << '\n';

    if (!_successors.empty())
    {
        sstr << " ; [succs: ";
        for (size_t i = 0, e = _successors.size(); i != e; ++i)
        {
            if (i)
                sstr << ", ";
            sstr << '%' << _successors[i]->name();
        }
        sstr << "]\n";
    }

    for (auto const& instr: _code)
    {
        sstr << '\t' << instr->to_string() << '\n';
    }

    sstr << "\n";

    std::cerr << sstr.str();
}

void BasicBlock::linkSuccessor(BasicBlock* successor)
{
    assert(successor != nullptr);

    _successors.push_back(successor);
    successor->_predecessors.push_back(this);
}

void BasicBlock::unlinkSuccessor(BasicBlock* successor)
{
    assert(successor != nullptr);

    auto p = std::find(successor->_predecessors.begin(), successor->_predecessors.end(), this);
    assert(p != successor->_predecessors.end());
    successor->_predecessors.erase(p);

    auto s = std::find(_successors.begin(), _successors.end(), successor);
    assert(s != _successors.end());
    _successors.erase(s);
}

std::vector<BasicBlock*> BasicBlock::dominators()
{
    std::vector<BasicBlock*> result;
    collectIDom(result);
    result.push_back(this);
    return result;
}

std::vector<BasicBlock*> BasicBlock::immediateDominators()
{
    std::vector<BasicBlock*> result;
    collectIDom(result);
    return result;
}

void BasicBlock::collectIDom(std::vector<BasicBlock*>& output)
{
    for (BasicBlock* p: _predecessors)
    {
        p->collectIDom(output);
    }
}

bool BasicBlock::isComplete() const
{
    if (empty())
        return false;

    if (getTerminator())
        return true;

    if (auto* instr = dynamic_cast<HandlerCallInstr*>(back()))
        return instr->callee()->getNative().isNeverReturning();

    if (auto* instr = dynamic_cast<CallInstr*>(back()))
        return instr->callee()->getNative().isNeverReturning();

    return false;
}

void BasicBlock::verify()
{
    COREVM_ASSERT(!_code.empty(),
                  fmt::format("BasicBlock {}: verify: Must contain at least one instruction.", name()));
    COREVM_ASSERT(
        isComplete(),
        fmt::format("BasicBlock {}: verify: Last instruction must be a terminator instruction.", name()));
    COREVM_ASSERT(
        std::find_if(_code.begin(),
                     std::prev(_code.end()),
                     [&](std::unique_ptr<Instr>& instr) -> bool {
                         return dynamic_cast<TerminateInstr*>(instr.get()) != nullptr;
                     })
            == std::prev(_code.end()),
        fmt::format("BasicBlock {}: verify: Found a terminate instruction in the middle of the block.",
                    name()));
}

} // namespace CoreVM
