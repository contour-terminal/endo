// SPDX-License-Identifier: Apache-2.0
#include <CoreVM/CoreVM.hpp>

namespace CoreVM
{

NativeProperty::NativeProperty(std::string name, LiteralType type): _name(std::move(name)), _type(type)
{
}

NativeProperty& NativeProperty::description(std::string desc)
{
    _description = std::move(desc);
    return *this;
}

NativeProperty& NativeProperty::onGet(Getter cb)
{
    _getter = std::move(cb);
    return *this;
}

NativeProperty& NativeProperty::onSet(Setter cb)
{
    _setter = std::move(cb);
    return *this;
}

void NativeProperty::invokeGet(Params& args) const
{
    if (_getter)
        _getter(args);
}

void NativeProperty::invokeSet(Params& args) const
{
    if (_setter)
        _setter(args);
}

} // namespace CoreVM
