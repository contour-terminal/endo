// SPDX-License-Identifier: Apache-2.0
#include <shell/Shell.h>

#include <crispy/utils.h>

using namespace std::string_literals;

std::string_view getEnvironment(std::string_view name, std::string_view defaultValue)
{
    auto const* const value = getenv(name.data());
    return value ? value : defaultValue;
}

int main(int argc, char const* argv[])
{
    auto shell = crush::Shell {};

    if (argc == 2)
        // This here only exists for early-development debugging purposes.
        return shell.execute(argv[1]);
    else
        return shell.run();
        // return shell.execute(R"(
        //     if true; then
        //         echo YES;
        //     else
        //         echo NO;
        //     fi
        // )");
}
