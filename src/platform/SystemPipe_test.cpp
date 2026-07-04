// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstring>

#include <platform/SystemPipe.hpp>

using endo::platform::createSystemPipe;
using endo::platform::InvalidHandle;

TEST_CASE("SystemPipe round-trips bytes between its ends", "[systempipe]")
{
    auto pipe = createSystemPipe();
    REQUIRE(pipe.has_value());
    REQUIRE((*pipe)->good());

    char const payload[] = "hello";
    auto const written = (*pipe)->write(payload, sizeof(payload));
    REQUIRE(written.has_value());
    REQUIRE(*written == sizeof(payload));

    auto buf = std::array<char, sizeof(payload)> {};
    auto const got = (*pipe)->read(buf.data(), buf.size());
    REQUIRE(got.has_value());
    REQUIRE(*got == sizeof(payload));
    REQUIRE(std::memcmp(buf.data(), payload, sizeof(payload)) == 0);
}

TEST_CASE("SystemPipe exposes a valid wait handle", "[systempipe]")
{
    auto pipe = createSystemPipe();
    REQUIRE(pipe.has_value());
    REQUIRE((*pipe)->waitHandle() != InvalidHandle);
    REQUIRE((*pipe)->readFd() != InvalidHandle);
    REQUIRE((*pipe)->writeFd() != InvalidHandle);
}
