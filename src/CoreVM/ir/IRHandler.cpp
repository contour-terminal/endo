// SPDX-License-Identifier: Apache-2.0

#include <CoreVM/ir/BasicBlock.h>
#include <CoreVM/ir/IRHandler.h>
#include <CoreVM/ir/Instructions.h>
#include <CoreVM/util/assert.h>

#include <algorithm>
#include <cassert>

namespace CoreVM
{

IRHandler::IRHandler(const std::string& name, IRProgram* program):
    Constant(LiteralType::Handler, name), _program(program), _blocks()
{
}

IRHandler::~IRHandler()
{
    for (BasicBlock* bb: basicBlocks())
    {
        for (Instr* instr: bb->instructions())
        {
            instr->clearOperands();
        }
    }

    while (!_blocks.empty())
    {
        auto i = _blocks.begin();
        auto e = _blocks.end();

        while (i != e)
        {
            BasicBlock* bb = i->get();

            if (bb->predecessors().empty())
            {
                i = _blocks.erase(i);
            }
            else
            {
                // skip BBs that other BBs still point to (we never point to ourself).
                ++i;
            }
        }
    }
}

BasicBlock* IRHandler::createBlock(const std::string& name)
{
    _blocks.emplace_back(std::make_unique<BasicBlock>(name, this));
    return _blocks.back().get();
}

void IRHandler::setEntryBlock(BasicBlock* bb)
{
    COREVM_ASSERT(bb->getHandler(), "BasicBlock must belong to this handler.");

    auto i = std::find_if(_blocks.begin(), _blocks.end(), [&](const auto& obj) { return obj.get() == bb; });
    COREVM_ASSERT(i != _blocks.end(), "BasicBlock must belong to this handler.");
    std::unique_ptr<BasicBlock> t = std::move(*i);
    _blocks.erase(i);
    _blocks.push_front(std::move(t));
}

void IRHandler::dump()
{
    printf(".handler %s %*c; entryPoint = %%%s\n",
           name().c_str(),
           10 - (int) name().size(),
           ' ',
           getEntryBlock()->name().c_str());

    for (auto& bb: _blocks)
        bb->dump();

    printf("\n");
}

bool IRHandler::isAfter(const BasicBlock* bb, const BasicBlock* afterThat) const
{
    assert(bb->getHandler() == this);
    assert(afterThat->getHandler() == this);

    auto i = std::find_if(_blocks.cbegin(), _blocks.cend(), [&](const auto& obj) { return obj.get() == bb; });

    if (i == _blocks.cend())
        return false;

    ++i;

    if (i == _blocks.cend())
        return false;

    return i->get() == afterThat;
}

void IRHandler::moveAfter(const BasicBlock* moveable, const BasicBlock* after)
{
    assert(moveable->getHandler() == this && after->getHandler() == this);

    auto i =
        std::find_if(_blocks.begin(), _blocks.end(), [&](const auto& obj) { return obj.get() == moveable; });
    std::unique_ptr<BasicBlock> m = std::move(*i);
    _blocks.erase(i);

    i = std::find_if(_blocks.begin(), _blocks.end(), [&](const auto& obj) { return obj.get() == after; });
    ++i;
    _blocks.insert(i, std::move(m));
}

void IRHandler::moveBefore(const BasicBlock* moveable, const BasicBlock* before)
{
    assert(moveable->getHandler() == this && before->getHandler() == this);

    auto i =
        std::find_if(_blocks.begin(), _blocks.end(), [&](const auto& obj) { return obj.get() == moveable; });
    std::unique_ptr<BasicBlock> m = std::move(*i);
    _blocks.erase(i);

    i = std::find_if(_blocks.begin(), _blocks.end(), [&](const auto& obj) { return obj.get() == before; });
    ++i;
    _blocks.insert(i, std::move(m));
}

void IRHandler::erase(BasicBlock* bb)
{
    auto i = std::find_if(_blocks.begin(), _blocks.end(), [&](const auto& obj) { return obj.get() == bb; });
    COREVM_ASSERT(i != _blocks.end(), "Given basic block must be a member of this handler to be removed.");

    for (Instr* instr: bb->instructions())
    {
        instr->clearOperands();
    }

    if (TerminateInstr* terminator = bb->getTerminator())
    {
        (*i)->remove(terminator);
    }

    _blocks.erase(i);
}

void IRHandler::verify()
{
    for (std::unique_ptr<BasicBlock>& bb: _blocks)
    {
        bb->verify();
    }
}

} // namespace CoreVM
