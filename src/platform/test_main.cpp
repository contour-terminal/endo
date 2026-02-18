// SPDX-License-Identifier: Apache-2.0
#include <crispy/App.h>
#include <crispy/logstore.h>

#include <catch2/catch_session.hpp>

#include <testing/SuppressWindowsDialogs.hpp>

int main(int argc, char const* argv[])
{
    testing::suppressWindowsDialogs();
    char const* logFilterString = getenv("LOG");
    if (logFilterString)
    {
        logstore::configure(logFilterString);
        crispy::app::customizeLogStoreOutput();
    }
    int const result = Catch::Session().run(argc, argv);

    return result;
}
