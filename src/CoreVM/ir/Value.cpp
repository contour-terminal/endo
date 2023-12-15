// SPDX-License-Identifier: Apache-2.0
module;
#include <CoreVM/util/assert.h>
#include <CoreVM/util/strings.h>

#include <fmt/format.h>

#include <algorithm>
#include <cassert>

module CoreVM;
namespace CoreVM
{

static unsigned long long valueCounter = 1;

Value::Value(const Value& v): _type(v._type)
{
    char buf[256];
    snprintf(buf, sizeof(buf), "%s_%llu", v.name().c_str(), valueCounter);
    valueCounter++;
    _name = buf;
}

Value::Value(LiteralType ty, std::string name): _type(ty), _name(std::move(name))
{
    if (_name.empty())
    {
        _name = fmt::format("unnamed{}", valueCounter);
        valueCounter++;
        // printf("default-create name: %s\n", _name.c_str());
    }
}

// Value::Value(Value&& other) noexcept: _type(other._type), _name(std::move(other._name)), _uses(std::move(other._uses))
// {
//     other._uses = {};
// }
//
// Value& Value::operator=(Value&& other) noexcept
// {
//     COREVM_ASSERT(!isUsed(),
//                   fmt::format("Value {} being destroyed is still in use by: {}.",
//                               name(),
//                               join(_uses, ", ", &Instr::name)));
//
//     _type = other._type;
//     _name = std::move(other._name);
//     _uses = std::move(other._uses);
//     other._uses = {};
//
//     return *this;
// }

Value::~Value()
{
    COREVM_ASSERT(!isUsed(),
                  fmt::format("Value {} being destroyed is still in use by: {}.",
                              name(),
                              join(_uses, ", ", &Instr::name)));
}

void Value::addUse(Instr* user)
{
    _uses.push_back(user);
}

void Value::removeUse(Instr* user)
{
    auto i = std::find(_uses.begin(), _uses.end(), user);

    assert(i != _uses.end());

    if (i != _uses.end())
    {
        _uses.erase(i);
    }
}

void Value::replaceAllUsesWith(Value* newUse)
{
    auto myUsers = _uses;

    for (Instr* user: myUsers)
    {
        user->replaceOperand(this, newUse);
    }
}

std::string Value::to_string() const
{
    return fmt::format("Value {} of type {}", _name, _type);
}

} // namespace CoreVM
