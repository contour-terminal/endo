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
    return onSet(_type, std::move(cb));
}

NativeProperty& NativeProperty::onSet(LiteralType argType, Setter cb)
{
    for (auto& [t, setter]: _setters)
    {
        if (t == argType)
        {
            setter = std::move(cb);
            return *this;
        }
    }
    _setters.emplace_back(argType, std::move(cb));
    _setterTypes.push_back(argType);
    return *this;
}

bool NativeProperty::hasSetter(LiteralType argType) const noexcept
{
    for (auto const& [t, setter]: _setters)
        if (t == argType)
            return setter != nullptr;
    return false;
}

void NativeProperty::invokeGet(Params& args) const
{
    if (_getter)
        _getter(args);
}

void NativeProperty::invokeSet(LiteralType argType, Params& args) const
{
    for (auto const& [t, setter]: _setters)
    {
        if (t == argType && setter)
        {
            setter(args);
            return;
        }
    }
}

} // namespace CoreVM
