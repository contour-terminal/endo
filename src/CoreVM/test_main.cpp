// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_session.hpp>

#include <testing/SuppressWindowsDialogs.hpp>

int main(int argc, char* argv[])
{
    testing::suppressWindowsDialogs();
    return Catch::Session().run(argc, argv);
}
