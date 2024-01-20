// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <fmt/format.h>
#include <crispy/logstore.h>
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

    static auto write(std::string message) { return fmt::format("{}{}\n", indentation(), message); }
    auto writeInternal(std::string message) { _category()("{}{}\n", indentation(), message); }

    ScopedLogger(std::string message, auto&& log): _message(std::move(message)), _category(log)
    {
        ++depth;
        writeInternal("{{ " + _message);
    }

    ~ScopedLogger()
    {
        writeInternal("}} " + _message);
        --depth;
    }

    std::string _message;
    logstore::category const& _category;
};
