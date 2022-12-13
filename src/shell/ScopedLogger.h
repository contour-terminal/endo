// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <fmt/format.h>
#include <string>

struct ScopedLogger
{
    static inline int depth = 0;

    static std::string indentation()
    {
        std::string result;
        for (int i = 0; i < depth; ++i)
            result += "  ";
        return result;
    }

    static void write(std::string message) { fmt::print("{}{}\n", indentation(), message); }

    ScopedLogger(std::string message): _message(std::move(message))
    {
        ++depth;
        write("{{ " + _message);
    }

    ~ScopedLogger()
    {
        write("}} " + _message);
        --depth;
    }

    std::string _message;
};
