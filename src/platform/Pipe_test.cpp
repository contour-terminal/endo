// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <platform/Pipe.hpp>
#include <platform/testing/MockPipe.hpp>

#if !defined(_WIN32)
    #include <fcntl.h>
    #include <unistd.h>
#endif

using namespace endo::platform;

#if !defined(_WIN32)
TEST_CASE("Pipe.createPipe_with_O_CLOEXEC_closes_both_ends_on_exec", "[platform]")
{
    // Where there is no pipe2 (macOS), the flag is applied with fcntl, and passing O_CLOEXEC itself to
    // F_SETFD sets nothing: a child another thread started would inherit both ends.
    auto result = createPipe(O_CLOEXEC);
    REQUIRE(result.has_value());
    CHECK((fcntl((*result)->reader(), F_GETFD) & FD_CLOEXEC) != 0);
    CHECK((fcntl((*result)->writer(), F_GETFD) & FD_CLOEXEC) != 0);
}

TEST_CASE("Pipe.createPipe_with_O_NONBLOCK_makes_both_ends_non_blocking", "[platform]")
{
    auto result = createPipe(O_NONBLOCK);
    REQUIRE(result.has_value());
    CHECK((fcntl((*result)->reader(), F_GETFL) & O_NONBLOCK) != 0);
    CHECK((fcntl((*result)->writer(), F_GETFL) & O_NONBLOCK) != 0);
}
#endif

TEST_CASE("Pipe.createPipe", "[platform]")
{
    auto result = createPipe();
    REQUIRE(result.has_value());
    auto& pipe = *result;
    CHECK(pipe->good());
    CHECK(pipe->reader() != core::platform::InvalidHandle);
    CHECK(pipe->writer() != core::platform::InvalidHandle);
}

TEST_CASE("Pipe.releaseHandles", "[platform]")
{
    auto result = createPipe();
    REQUIRE(result.has_value());
    auto& pipe = *result;

    auto const reader = pipe->releaseReader();
    CHECK(reader != core::platform::InvalidHandle);
    CHECK(pipe->reader() == core::platform::InvalidHandle);

    auto const writer = pipe->releaseWriter();
    CHECK(writer != core::platform::InvalidHandle);
    CHECK(pipe->writer() == core::platform::InvalidHandle);

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
    CHECK(pipe.reader() == core::platform::InvalidHandle);
    CHECK(pipe.writer() == core::platform::InvalidHandle);
}
