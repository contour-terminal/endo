// SPDX-License-Identifier: Apache-2.0
#include <CoreVM/CoreVM.hpp>
#include <CoreVM/util/assert.hpp>

#include <algorithm>
#include <cassert>
#include <format>
#include <iostream>
#include <memory>
#include <sstream>

namespace CoreVM
{

IRFunction::IRFunction(const std::string& name, IRProgram* program):
    Constant(LiteralType::Function, name), _program(program)
{
}

IRFunction::~IRFunction()
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

BasicBlock* IRFunction::createBlock(const std::string& name)
{
    _blocks.emplace_back(std::make_unique<BasicBlock>(name, this));
    return _blocks.back().get();
}

void IRFunction::setEntryBlock(BasicBlock* bb)
{
    COREVM_ASSERT(bb->getFunction(), "BasicBlock must belong to this function.");

    auto i = std::ranges::find_if(_blocks, [&](const auto& obj) { return obj.get() == bb; });
    COREVM_ASSERT(i != _blocks.end(), "BasicBlock must belong to this function.");
    std::unique_ptr<BasicBlock> t = std::move(*i);
    _blocks.erase(i);
    _blocks.push_front(std::move(t));
}

void IRFunction::dump()
{
    std::cerr << dumpToString();
}

std::string IRFunction::dumpToString() const
{
    std::ostringstream sstr;
    sstr << std::format(".function {} {:>{}}; entryPoint = %{}\n",
                        name(),
                        "",
                        std::max(0, 10 - static_cast<int>(name().size())),
                        getEntryBlock()->name());

    for (auto const& bb: _blocks)
        sstr << bb->dumpToString();

    sstr << "\n";
    return sstr.str();
}

bool IRFunction::isAfter(const BasicBlock* bb, const BasicBlock* afterThat) const
{
    assert(bb->getFunction() == this);
    assert(afterThat->getFunction() == this);

    auto i = std::ranges::find_if(_blocks, [&](const auto& obj) { return obj.get() == bb; });

    if (i == _blocks.cend())
        return false;

    ++i;

    if (i == _blocks.cend())
        return false;

    return i->get() == afterThat;
}

void IRFunction::moveAfter(const BasicBlock* moveable, const BasicBlock* after)
{
    assert(moveable->getFunction() == this && after->getFunction() == this);

    auto i = std::ranges::find_if(_blocks, [&](const auto& obj) { return obj.get() == moveable; });
    std::unique_ptr<BasicBlock> m = std::move(*i);
    _blocks.erase(i);

    i = std::ranges::find_if(_blocks, [&](const auto& obj) { return obj.get() == after; });
    ++i;
    _blocks.insert(i, std::move(m));
}

void IRFunction::moveBefore(const BasicBlock* moveable, const BasicBlock* before)
{
    assert(moveable->getFunction() == this && before->getFunction() == this);

    auto i = std::ranges::find_if(_blocks, [&](const auto& obj) { return obj.get() == moveable; });
    std::unique_ptr<BasicBlock> m = std::move(*i);
    _blocks.erase(i);

    i = std::ranges::find_if(_blocks, [&](const auto& obj) { return obj.get() == before; });
    ++i;
    _blocks.insert(i, std::move(m));
}

void IRFunction::erase(BasicBlock* bb)
{
    auto i = std::ranges::find_if(_blocks, [&](const auto& obj) { return obj.get() == bb; });
    COREVM_ASSERT(i != _blocks.end(), "Given basic block must be a member of this function to be removed.");

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

void IRFunction::verify()
{
    for (std::unique_ptr<BasicBlock>& bb: _blocks)
    {
        bb->verify();
    }
}

} // namespace CoreVM
