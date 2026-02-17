// SPDX-License-Identifier: Apache-2.0
#include "TerminalInput.hpp"

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <print>
#include <string>

#if defined(_WIN32)
    #include <conio.h>
    #include <windows.h>
#else
    #include <termios.h>
    #include <unistd.h>
#endif

namespace endo::agent
{

auto readSecretLine(std::string_view prompt) -> std::optional<std::string>
{
    if (!prompt.empty())
        std::print("{}", prompt);

#if defined(_WIN32)
    auto result = std::string {};
    while (true)
    {
        auto const ch = _getch();
        if (ch == '\r' || ch == '\n')
            break;
        if (ch == 3) // Ctrl+C
            return std::nullopt;
        if (ch == '\b' || ch == 127) // Backspace
        {
            if (!result.empty())
                result.pop_back();
        }
        else
        {
            result += static_cast<char>(ch);
        }
    }
    std::print("\n");
    return result;
#else
    // Save current terminal settings
    struct termios oldSettings {};
    if (tcgetattr(STDIN_FILENO, &oldSettings) != 0)
        return std::nullopt;

    // Disable echo
    auto newSettings = oldSettings;
    newSettings.c_lflag &= ~static_cast<tcflag_t>(ECHO);
    if (tcsetattr(STDIN_FILENO, TCSANOW, &newSettings) != 0)
        return std::nullopt;

    auto result = std::string {};
    auto const success = static_cast<bool>(std::getline(std::cin, result));

    // Restore terminal settings
    tcsetattr(STDIN_FILENO, TCSANOW, &oldSettings);

    std::print("\n");

    if (!success)
        return std::nullopt;
    return result;
#endif
}

auto openBrowser(std::string_view url) -> bool
{
    auto const urlStr = std::string(url);

#if defined(__APPLE__)
    auto const command = std::string("open '") + urlStr + "' 2>/dev/null";
#elif defined(_WIN32)
    auto const command = std::string("start \"\" '") + urlStr + "' 2>NUL";
#else
    auto const command = std::string("xdg-open '") + urlStr + "' 2>/dev/null";
#endif

    // NOLINTNEXTLINE(cert-env33-c) - intentional: launching user's default browser
    auto const exitCode = std::system(command.c_str());
    return exitCode == 0;
}

} // namespace endo::agent
