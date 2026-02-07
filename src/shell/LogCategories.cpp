// SPDX-License-Identifier: Apache-2.0
#include "LogCategories.hpp"

#include "LogConfig.hpp"

namespace endo::log
{

logstore::category& shellDebug()
{
    static auto instance =
        logstore::category("shell.debug", "Shell execution debug output", categoryState("shell.debug"));
    return instance;
}

logstore::category& vmTrace()
{
    static auto instance =
        logstore::category("vm.trace", "VM instruction execution trace", categoryState("vm.trace"));
    return instance;
}

logstore::category& vmIR()
{
    static auto instance = logstore::category("vm.ir", "VM IR and bytecode dump", categoryState("vm.ir"));
    return instance;
}

logstore::category& parser()
{
    static auto instance = logstore::category("parser", "Parser debug output", categoryState("parser"));
    return instance;
}

logstore::category& pipe()
{
    static auto instance = logstore::category("pipe", "Unix pipe operations", categoryState("pipe"));
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
