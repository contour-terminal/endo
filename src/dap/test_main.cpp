// SPDX-License-Identifier: Apache-2.0
#include <core/testing/SuppressWindowsDialogs.hpp>

#include <catch2/catch_session.hpp>

int main(int argc, char const* argv[])
{
    core::testing::suppressWindowsDialogs();
    return Catch::Session().run(argc, argv);
}
