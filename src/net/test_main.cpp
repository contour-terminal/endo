// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_session.hpp>

#include <testing/SuppressWindowsDialogs.hpp>

#if defined(_WIN32)
// clang-format off
    #include <winsock2.h>
    #include <windows.h>
// clang-format on
#endif

int main(int argc, char const* argv[])
{
    testing::suppressWindowsDialogs();
#if defined(_WIN32)
    WSADATA wsaData {};
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
    auto const rc = Catch::Session().run(argc, argv);
#if defined(_WIN32)
    WSACleanup();
#endif
    return rc;
}
