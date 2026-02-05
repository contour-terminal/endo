// SPDX-License-Identifier: Apache-2.0
module;

#include <shell/LogConfig.hpp>

#include <string>
#include <utility>

module CoreVM;

namespace
{
// Use function-local static to avoid C++20 module static initialization issues
auto& passManagerLog()
{
    static auto instance =
        logstore::category("vm.pass", "VM pass manager log", endo::log::categoryState("vm.pass"));
    return instance;
}
} // namespace

namespace CoreVM
{

void PassManager::registerPass(std::string name, HandlerPass handlerPass)
{
    _handlerPasses.emplace_back(std::move(name), std::move(handlerPass));
}

void PassManager::run(IRProgram* program)
{
    for (IRHandler* handler: program->handlers())
    {
        logDebug("optimizing handler {}", handler->name());
        run(handler);
    }
}

void PassManager::run(IRHandler* handler)
{
    for (;;)
    {
        int changes = 0;
        for (const std::pair<std::string, HandlerPass>& pass: _handlerPasses)
        {
            logDebug("executing pass {}:", pass.first);
            if (pass.second(handler))
            {
                logDebug("pass {}: changes detected", pass.first);
                handler->verify();
                changes++;
            }
        }
        logDebug("{} changes detected", changes);
        if (!changes)
        {
            break;
        }
    }
}

void PassManager::logDebug(const std::string& msg)
{
    if (passManagerLog().is_enabled())
        passManagerLog()()("PassManager: {}\n", msg);
}

} // namespace CoreVM
