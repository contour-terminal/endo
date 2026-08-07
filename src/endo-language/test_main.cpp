// SPDX-License-Identifier: Apache-2.0
#include <crispy/App.hpp>
#include <crispy/LogStore.hpp>

#include <catch2/catch_session.hpp>

#include <testing/SuppressWindowsDialogs.hpp>

int main(int argc, char const* argv[])
{
    testing::suppressWindowsDialogs();
    char const* logFilterString = getenv("LOG");
    if (logFilterString)
    {
        logstore::configure(logFilterString);
        crispy::App::customizeLogStoreOutput();
    }
    int const result = Catch::Session().run(argc, argv);

    // avoid closing extern console to close on VScode/windows
    // system("pause");

    return result;
}
