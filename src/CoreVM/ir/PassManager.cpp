// SPDX-License-Identifier: Apache-2.0
#include <endo-language/LogConfig.hpp>

#include <CoreVM/CoreVM.hpp>

#include <string>
#include <utility>

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

void PassManager::registerPass(std::string name, FunctionPass functionPass)
{
    _functionPasses.emplace_back(std::move(name), std::move(functionPass));
}

void PassManager::run(IRProgram* program)
{
    for (IRFunction* function: program->functions())
    {
        logDebug("optimizing function {}", function->name());
        run(function);
    }
}

void PassManager::run(IRFunction* function)
{
    for (;;)
    {
        int changes = 0;
        for (const std::pair<std::string, FunctionPass>& pass: _functionPasses)
        {
            logDebug("executing pass {}:", pass.first);
            if (pass.second(function))
            {
                logDebug("pass {}: changes detected", pass.first);
                function->verify();
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
