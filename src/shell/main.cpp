// SPDX-License-Identifier: Apache-2.0
#include <crispy/logstore.h>
#include <crispy/utils.h>

#include <cstdlib>
#include <cstring>
#include <span>
#include <string_view>

#include <unistd.h>

using namespace std::string_literals;

import Shell;

std::string_view getEnvironment(std::string_view name, std::string_view defaultValue)
{
    auto const* const value = getenv(name.data());
    return value ? value : defaultValue;
}

int main(int argc, char const* argv[])
{
    auto args = std::span(argv, static_cast<size_t>(argc));

    // Parse command line arguments for --debug flag
    for (auto const& arg: args.subspan(1))
    {
        if (std::strcmp(arg, "--debug") == 0)
            logstore::enable("debug");
    }

    auto shell = endo::Shell {};

    setsid();

    // Check for command argument (skip --debug if present)
    for (auto const& arg: args.subspan(1))
    {
        if (std::strcmp(arg, "--debug") != 0)
            return shell.execute(arg);
    }

    return shell.run();
}
