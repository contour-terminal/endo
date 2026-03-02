// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <platform/Pipe.hpp>
#include <platform/testing/MockPipe.hpp>

using namespace endo::platform;

TEST_CASE("Pipe.createPipe", "[platform]")
{
    auto result = createPipe();
    REQUIRE(result.has_value());
    auto& pipe = *result;
    CHECK(pipe->good());
    CHECK(pipe->reader() != InvalidHandle);
    CHECK(pipe->writer() != InvalidHandle);
}

TEST_CASE("Pipe.releaseHandles", "[platform]")
{
    auto result = createPipe();
    REQUIRE(result.has_value());
    auto& pipe = *result;

    auto const reader = pipe->releaseReader();
    CHECK(reader != InvalidHandle);
    CHECK(pipe->reader() == InvalidHandle);

    auto const writer = pipe->releaseWriter();
    CHECK(writer != InvalidHandle);
    CHECK(pipe->writer() == InvalidHandle);

    CHECK(!pipe->good());

    // Clean up released handles
#if !defined(_WIN32)
    close(reader);
    close(writer);
#endif
}

TEST_CASE("MockPipe.basic", "[platform][mock]")
{
    testing::MockPipe pipe(testing::testHandle(42), testing::testHandle(43));
    CHECK(pipe.reader() == testing::testHandle(42));
    CHECK(pipe.writer() == testing::testHandle(43));
    CHECK(pipe.good());

    pipe.closeReader();
    CHECK(!pipe.good());

    pipe.closeWriter();
    CHECK(pipe.reader() == InvalidHandle);
    CHECK(pipe.writer() == InvalidHandle);
}
