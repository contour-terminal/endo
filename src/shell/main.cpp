// SPDX-License-Identifier: Apache-2.0
#include <crispy/utils.h>

using namespace std::string_literals;

import Shell;

#if defined(ENDO_USE_LLVM)
using Shell = endo::ShellLLVM;
#else
using Shell = endo::ShellCoreVM;
#endif

std::string_view getEnvironment(std::string_view name, std::string_view defaultValue)
{
    auto const* const value = getenv(name.data());
    return value ? value : defaultValue;
}

int main(int argc, char const* argv[])
{
    auto shell = Shell {};

    setsid();

    return shell.run(argc, argv);
}
