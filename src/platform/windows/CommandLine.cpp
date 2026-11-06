// SPDX-License-Identifier: Apache-2.0
#include <platform/windows/CommandLine.hpp>

#if defined(_WIN32)
    #include <windows.h>

namespace endo::platform
{

auto quoteArgument(std::string_view arg) -> std::string
{
    if (!arg.empty() && arg.find_first_of(" \t\"") == std::string_view::npos)
        return std::string(arg);

    // Backslashes are literal except in front of a quote, where each must be doubled and the quote
    // escaped -- and in front of the closing quote, where each must be doubled. So a run of them is
    // held back until the character after it says which.
    auto result = std::string {};
    result.reserve(arg.size() + 4);
    result += '"';

    auto pendingBackslashes = std::size_t { 0 };
    for (auto const ch: arg)
    {
        if (ch == '\\')
        {
            ++pendingBackslashes;
            continue;
        }
        if (ch == '"')
        {
            result.append((pendingBackslashes * 2) + 1, '\\');
            result += '"';
        }
        else
        {
            result.append(pendingBackslashes, '\\');
            result += ch;
        }
        pendingBackslashes = 0;
    }
    result.append(pendingBackslashes * 2, '\\');

    result += '"';
    return result;
}

auto buildCommandLine(std::string_view program, std::span<std::string const> arguments) -> std::wstring
{
    auto cmdLine = quoteArgument(program);
    for (auto const& arg: arguments)
    {
        cmdLine += ' ';
        cmdLine += quoteArgument(arg);
    }

    auto const size = MultiByteToWideChar(CP_UTF8, 0, cmdLine.c_str(), -1, nullptr, 0);
    auto wideCmdLine = std::wstring(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, cmdLine.c_str(), -1, wideCmdLine.data(), size);

    if (!wideCmdLine.empty() && wideCmdLine.back() == L'\0')
        wideCmdLine.pop_back();

    return wideCmdLine;
}

} // namespace endo::platform

#endif // _WIN32
