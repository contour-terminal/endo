// SPDX-License-Identifier: Apache-2.0
#include <CoreVM/CoreVM.hpp>

#include <string>
#include <vector>

namespace CoreVM
{

NativeCallback::NativeCallback(Runtime* runtime, std::string name, LiteralType returnType): _runtime(runtime)
{
    _signature.setName(std::move(name));
    _signature.setReturnType(returnType);
}

std::string const& NativeCallback::name() const noexcept
{
    return _signature.name();
}

const Signature& NativeCallback::signature() const noexcept
{
    return _signature;
}

NativeCallback& NativeCallback::param(LiteralType type, const std::string& name)
{
    _signature.args().push_back(type);
    _names.push_back(name);
    _defaults.emplace_back(std::monostate {});
    return *this;
}

int NativeCallback::findParamByName(const std::string& name) const
{
    for (size_t i = 0, e = _names.size(); i != e; ++i)
        if (_names[i] == name)
            return static_cast<int>(i);

    return -1;
}

NativeCallback& NativeCallback::setNoReturn() noexcept
{
    _attributes |= static_cast<unsigned>(Attribute::NoReturn);
    return *this;
}

NativeCallback& NativeCallback::setReadOnly() noexcept
{
    _attributes |= static_cast<unsigned>(Attribute::SideEffectFree);
    return *this;
}

NativeCallback& NativeCallback::setExperimental() noexcept
{
    _attributes |= static_cast<unsigned>(Attribute::Experimental);
    return *this;
}

void NativeCallback::invoke(Params& args) const
{
    _function(args);
}

// Out-of-line Params definitions that require Function/Program to be complete types.

void Params::setResult(const Function* fn)
{
    _argv[0] = _caller->program()->indexOf(fn);
}

void Params::setResult(const char* str)
{
    _argv[0] = (Value) _caller->newString(str);
}

void Params::setResult(std::string str)
{
    _argv[0] = (Value) _caller->newString(std::move(str));
}

Function* Params::getFunction(size_t offset) const
{
    return _caller->program()->function(static_cast<size_t>(at(offset)));
}

} // namespace CoreVM
