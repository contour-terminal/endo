// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

#include "CompleterFunctionRegistry.hpp"

TEST_CASE("CompleterFunctionRegistry.register_and_lookup")
{
    endo::CompleterFunctionRegistry registry;
    registry.registerFunction("flatpak", "flatpak_complete");

    auto result = registry.functionForCommand("flatpak");
    REQUIRE(result.has_value());
    CHECK(*result == "flatpak_complete");
}

TEST_CASE("CompleterFunctionRegistry.unknown_command")
{
    endo::CompleterFunctionRegistry registry;

    auto result = registry.functionForCommand("nonexistent");
    CHECK_FALSE(result.has_value());
}

TEST_CASE("CompleterFunctionRegistry.overwrite")
{
    endo::CompleterFunctionRegistry registry;
    registry.registerFunction("flatpak", "flatpak_complete_v1");
    registry.registerFunction("flatpak", "flatpak_complete_v2");

    auto result = registry.functionForCommand("flatpak");
    REQUIRE(result.has_value());
    CHECK(*result == "flatpak_complete_v2");
}

TEST_CASE("CompleterFunctionRegistry.commands_list")
{
    endo::CompleterFunctionRegistry registry;
    registry.registerFunction("flatpak", "flatpak_complete");
    registry.registerFunction("cmake", "cmake_complete");
    registry.registerFunction("ssh", "ssh_complete");

    auto commands = registry.commands();
    CHECK(commands.size() == 3);

    std::ranges::sort(commands);
    CHECK(commands[0] == "cmake");
    CHECK(commands[1] == "flatpak");
    CHECK(commands[2] == "ssh");
}

TEST_CASE("CompleterFunctionRegistry.hasCommand")
{
    endo::CompleterFunctionRegistry registry;
    registry.registerFunction("flatpak", "flatpak_complete");

    CHECK(registry.hasCommand("flatpak"));
    CHECK_FALSE(registry.hasCommand("unknown"));
}
