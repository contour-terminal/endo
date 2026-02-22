// SPDX-License-Identifier: Apache-2.0
#include <CoreVM/CoreVM.hpp>

#include <list>
#include <memory>
#include <string>
#include <vector>

namespace CoreVM
{

bool Runtime::import(const std::string& /*name*/,
                     const std::string& /*path*/,
                     std::vector<NativeCallback*>* /*builtins*/)
{
    return false;
}

NativeCallback& Runtime::registerFunction(const std::string& name)
{
    _builtins.push_back(std::make_unique<NativeCallback>(this, name, LiteralType::Void));
    return *_builtins[_builtins.size() - 1];
}

NativeCallback& Runtime::registerFunction(const std::string& name, LiteralType returnType)
{
    _builtins.push_back(std::make_unique<NativeCallback>(this, name, returnType));
    return *_builtins[_builtins.size() - 1];
}

NativeProperty& Runtime::registerProperty(std::string const& name, LiteralType type)
{
    _properties.push_back(std::make_unique<NativeProperty>(name, type));
    auto& prop = *_properties.back();

    // Register getter callback: name()T — 0 args, returns the property type
    auto& getter = registerFunction(name, type);
    getter.bind([&prop](Params& args) { prop.invokeGet(args); });

    // Register setter callback: name(T)V — 1 arg (the new value), returns void
    auto& setter = registerFunction(name);
    switch (type)
    {
        case LiteralType::String: setter.param<CoreString>("value"); break;
        case LiteralType::Number: setter.param<CoreNumber>("value"); break;
        case LiteralType::Boolean: setter.param<bool>("value"); break;
        case LiteralType::Object: setter.param<TypedObject*>("value"); break;
        default: setter.param<CoreNumber>("value"); break;
    }
    setter.returnType(LiteralType::Void);
    setter.bind([&prop](Params& args) { prop.invokeSet(args); });

    return prop;
}

NativeProperty* Runtime::findProperty(std::string const& name) const noexcept
{
    for (auto const& prop: _properties)
    {
        if (prop->name() == name)
            return prop.get();
    }
    return nullptr;
}

NativeCallback* Runtime::find(const std::string& signature) const noexcept
{
    for (const auto& callback: _builtins)
    {
        if (callback->signature().to_s() == signature)
        {
            return callback.get();
        }
    }

    return nullptr;
}

NativeCallback* Runtime::find(const Signature& signature) const noexcept
{
    return find(signature.to_s());
}

bool Runtime::verifyNativeCalls(IRProgram* program, IRBuilder* builder) const
{
    std::list<std::pair<Instr*, NativeCallback*>> calls;

    for (IRFunction* function: program->functions())
    {
        for (BasicBlock* bb: function->basicBlocks())
        {
            for (Instr* instr: bb->instructions())
            {
                if (auto* ci = dynamic_cast<CallInstr*>(instr))
                {
                    if (auto* native = find(ci->callee()->signature()))
                    {
                        calls.emplace_back(instr, native);
                    }
                }
            }
        }
    }

    for (const std::pair<Instr*, NativeCallback*>& call: calls)
    {
        if (!call.second->verify(call.first, builder))
        {
            return false;
        }
    }

    return true;
}

} // namespace CoreVM
