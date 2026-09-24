// SPDX-License-Identifier: Apache-2.0
#include <core/cli/App.hpp>
#include <core/log/LogStore.hpp>
#include <core/testing/SuppressWindowsDialogs.hpp>

#include <catch2/catch_session.hpp>

int main(int argc, char const* argv[])
{
    core::testing::suppressWindowsDialogs();
    char const* logFilterString = getenv("LOG");
    if (logFilterString)
    {
        core::log::configure(logFilterString);
        core::cli::App::customizeLogStoreOutput();
    }
    int const result = Catch::Session().run(argc, argv);

    return result;
}
