// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <platform/testing/TestEnvironmentProvider.hpp>

using namespace endo::platform;

TEST_CASE("TestEnvironmentProvider.set_and_get", "[platform]")
{
    TestEnvironmentProvider env;
    env.set("FOO", "bar");
    auto const val = env.get("FOO");
    REQUIRE(val.has_value());
    CHECK(*val == "bar");
}

TEST_CASE("TestEnvironmentProvider.get_missing", "[platform]")
{
    TestEnvironmentProvider env;
    CHECK(!env.get("MISSING").has_value());
}

TEST_CASE("TestEnvironmentProvider.unset", "[platform]")
{
    TestEnvironmentProvider env;
    env.set("KEY", "value");
    env.unset("KEY");
    CHECK(!env.get("KEY").has_value());
}

TEST_CASE("TestEnvironmentProvider.keys", "[platform]")
{
    TestEnvironmentProvider env;
    env.set("A", "1");
    env.set("B", "2");
    auto const keys = env.keys();
    CHECK(keys.size() == 2);
}

TEST_CASE("TestEnvironmentProvider.changeDirectory", "[platform]")
{
    TestEnvironmentProvider env("/home/user");
    CHECK(env.currentDirectory() == "/home/user");

    auto const result = env.changeDirectory("/tmp");
    CHECK(result.has_value());
    CHECK(env.currentDirectory() == "/tmp");
}

TEST_CASE("TestEnvironmentProvider.changeDirectory_invalid", "[platform]")
{
    TestEnvironmentProvider env("/home/user");
    env.addValidPath("/allowed");

    auto const result = env.changeDirectory("/forbidden");
    CHECK(!result.has_value());
    CHECK(result.error() == PlatformError::FileNotFound);
    CHECK(env.currentDirectory() == "/home/user");
}
