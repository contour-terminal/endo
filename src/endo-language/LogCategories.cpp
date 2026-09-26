// SPDX-License-Identifier: Apache-2.0
#include "LogCategories.hpp"

#include "LogConfig.hpp"

namespace endo::log
{

core::log::Category& shellDebug()
{
    static auto instance =
        core::log::Category("shell.debug", "Shell execution debug output", categoryState("shell.debug"));
    return instance;
}

core::log::Category& vmTrace()
{
    static auto instance =
        core::log::Category("vm.trace", "VM instruction execution trace", categoryState("vm.trace"));
    return instance;
}

core::log::Category& vmIR()
{
    static auto instance = core::log::Category("vm.ir", "VM IR and bytecode dump", categoryState("vm.ir"));
    return instance;
}

core::log::Category& parser()
{
    static auto instance = core::log::Category("parser", "Parser debug output", categoryState("parser"));
    return instance;
}

core::log::Category& pipe()
{
    static auto instance = core::log::Category("pipe", "Unix pipe operations", categoryState("pipe"));
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
