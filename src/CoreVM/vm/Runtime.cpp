// SPDX-License-Identifier: Apache-2.0

#include <CoreVM/NativeCallback.h>
#include <CoreVM/ir/BasicBlock.h>
#include <CoreVM/ir/IRHandler.h>
#include <CoreVM/ir/IRProgram.h>
#include <CoreVM/ir/Instructions.h>
#include <CoreVM/vm/Runtime.h>

namespace CoreVM
{

bool Runtime::import(const std::string& /*name*/,
                     const std::string& /*path*/,
                     std::vector<NativeCallback*>* /*builtins*/)
{
    return false;
}

NativeCallback& Runtime::registerHandler(const std::string& name)
{
    _builtins.push_back(std::make_unique<NativeCallback>(this, name));
    return *_builtins[_builtins.size() - 1];
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

    for (IRHandler* handler: program->handlers())
    {
        for (BasicBlock* bb: handler->basicBlocks())
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
                else if (auto* hi = dynamic_cast<HandlerCallInstr*>(instr))
                {
                    if (auto* native = find(hi->callee()->signature()))
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
