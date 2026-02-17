// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_session.hpp>

#include <testing/SuppressWindowsDialogs.hpp>

int main(int argc, char const* argv[])
{
    testing::suppressWindowsDialogs();
    return Catch::Session().run(argc, argv);
}
