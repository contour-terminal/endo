// SPDX-License-Identifier: Apache-2.0
#include <CoreVM/CoreVM.hpp>

#include <string>
#include <vector>

namespace CoreVM
{

NativeCallback::NativeCallback(Runtime* runtime, std::string name, LiteralType returnType):
    _runtime(runtime), _verifier(), _function(), _signature(), _attributes(0)
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
    for (int i = 0, e = _names.size(); i != e; ++i)
        if (_names[i] == name)
            return i;

    return -1;
}

NativeCallback& NativeCallback::setNoReturn() noexcept
{
    _attributes |= (unsigned) Attribute::NoReturn;
    return *this;
}

NativeCallback& NativeCallback::setReadOnly() noexcept
{
    _attributes |= (unsigned) Attribute::SideEffectFree;
    return *this;
}

NativeCallback& NativeCallback::setExperimental() noexcept
{
    _attributes |= (unsigned) Attribute::Experimental;
    return *this;
}

void NativeCallback::invoke(Params& args) const
{
    _function(args);
}

} // namespace CoreVM
