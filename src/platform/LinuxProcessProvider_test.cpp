// SPDX-License-Identifier: Apache-2.0
#if !defined(_WIN32)

    #include "linux/ProcStatusParser.hpp"

    #include <catch2/catch_test_macros.hpp>

using namespace endo::platform;

TEST_CASE("parseUidFromStatusLine.typical", "[platform][linux]")
{
    CHECK(parseUidFromStatusLine("Uid:\t1000\t1000\t1000\t1000") == 1000);
    CHECK(parseUidFromStatusLine("Uid:\t0\t0\t0\t0") == 0);
    CHECK(parseUidFromStatusLine("Uid:\t65534\t65534\t65534\t65534") == 65534);
}

TEST_CASE("parseUidFromStatusLine.whitespace_variants", "[platform][linux]")
{
    CHECK(parseUidFromStatusLine("Uid:  1000") == 1000);
    CHECK(parseUidFromStatusLine("Uid:\t 1000") == 1000);
    CHECK(parseUidFromStatusLine("Uid: \t1000") == 1000);
}

TEST_CASE("parseUidFromStatusLine.rejects_invalid", "[platform][linux]")
{
    CHECK(parseUidFromStatusLine("Gid:\t1000") == std::nullopt);
    CHECK(parseUidFromStatusLine("Uid:") == std::nullopt);
    CHECK(parseUidFromStatusLine("Uid:\t") == std::nullopt);
    CHECK(parseUidFromStatusLine("Uid:\tabc") == std::nullopt);
    CHECK(parseUidFromStatusLine("") == std::nullopt);
}

TEST_CASE("parseVmRssFromStatusLine.typical", "[platform][linux]")
{
    CHECK(parseVmRssFromStatusLine("VmRSS:\t  12345 kB") == 12345);
    CHECK(parseVmRssFromStatusLine("VmRSS:\t0 kB") == 0);
    CHECK(parseVmRssFromStatusLine("VmRSS:\t123456789 kB") == 123456789);
}

TEST_CASE("parseVmRssFromStatusLine.whitespace_variants", "[platform][linux]")
{
    CHECK(parseVmRssFromStatusLine("VmRSS:  12345 kB") == 12345);
    CHECK(parseVmRssFromStatusLine("VmRSS:\t12345 kB") == 12345);
}

TEST_CASE("parseVmRssFromStatusLine.rejects_invalid", "[platform][linux]")
{
    CHECK(parseVmRssFromStatusLine("VmSize:\t12345 kB") == std::nullopt);
    CHECK(parseVmRssFromStatusLine("VmRSS:") == std::nullopt);
    CHECK(parseVmRssFromStatusLine("VmRSS:\t") == std::nullopt);
    CHECK(parseVmRssFromStatusLine("VmRSS:\tabc kB") == std::nullopt);
    CHECK(parseVmRssFromStatusLine("") == std::nullopt);
}

#endif // !_WIN32
