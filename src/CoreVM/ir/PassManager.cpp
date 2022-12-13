// SPDX-License-Identifier: Apache-2.0

#include <CoreVM/ir/IRProgram.h>
#include <CoreVM/ir/PassManager.h>

#include <fmt/format.h>

#include <cstdio>
#include <cstdlib>

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
    const char* debugFlag = getenv("COREVM_DEBUG_TRANSFORMS");
    if (debugFlag && strcmp(debugFlag, "1") == 0)
    {
        fprintf(stderr, "PassManager: %s\n", msg.c_str());
    }
}

} // namespace CoreVM
