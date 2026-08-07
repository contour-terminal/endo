// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <crispy/LogStore.hpp>

#include <format>
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

    static auto write(std::string const& message) { return std::format("{}{}\n", indentation(), message); }

    auto writeInternal(std::string const& message) { _category()("{}{}\n", indentation(), message); }

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

  private:
    std::string _message;
    logstore::Category const& _category;
};
