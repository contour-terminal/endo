// SPDX-License-Identifier: Apache-2.0
#include "LogCategories.hpp"

#include "LogConfig.hpp"

namespace endo::log
{

logstore::Category& shellDebug()
{
    static auto instance =
        logstore::Category("shell.debug", "Shell execution debug output", categoryState("shell.debug"));
    return instance;
}

logstore::Category& vmTrace()
{
    static auto instance =
        logstore::Category("vm.trace", "VM instruction execution trace", categoryState("vm.trace"));
    return instance;
}

logstore::Category& vmIR()
{
    static auto instance = logstore::Category("vm.ir", "VM IR and bytecode dump", categoryState("vm.ir"));
    return instance;
}

logstore::Category& parser()
{
    static auto instance = logstore::Category("parser", "Parser debug output", categoryState("parser"));
    return instance;
}

logstore::Category& pipe()
{
    static auto instance = logstore::Category("pipe", "Unix pipe operations", categoryState("pipe"));
    return instance;
}

void registerAllCategories()
{
    // Force initialization of all categories by calling the accessors
    (void) shellDebug();
    (void) vmTrace();
    (void) vmIR();
    (void) parser();
    (void) pipe();
    // vm.diag and vm.pass are in CoreVM, they'll be registered when that code runs
}

} // namespace endo::log
