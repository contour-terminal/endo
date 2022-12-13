// SPDX-License-Identifier: Apache-2.0

#include <CoreVM/Diagnostics.h>

#include <fmt/format.h>

#include <iostream>

namespace CoreVM::diagnostics
{

// {{{ Message
std::string Message::string() const
{
    switch (type)
    {
        case Type::Warning: return fmt::format("[{}] {}", sourceLocation, text);
        case Type::LinkError: return fmt::format("{}: {}", type, text);
        default: return fmt::format("[{}] {}: {}", sourceLocation, type, text);
    }
}

bool Message::operator==(const Message& other) const noexcept
{
    // XXX ignore SourceLocation's filename & end
    return type == other.type && sourceLocation.begin == other.sourceLocation.begin && text == other.text;
}
// }}}
// {{{ ConsoleReport
ConsoleReport::ConsoleReport(): _errorCount { 0 }
{
}

bool ConsoleReport::containsFailures() const noexcept
{
    return _errorCount != 0;
}

void ConsoleReport::push_back(Message message)
{
    if (message.type != Type::Warning)
        _errorCount++;

    switch (message.type)
    {
        case Type::Warning: std::cerr << fmt::format("Warning: {}\n", message); break;
        default: std::cerr << fmt::format("Error: {}\n", message); break;
    }
}
// }}}
// {{{ BufferedReport
void BufferedReport::push_back(Message msg)
{
    _messages.emplace_back(std::move(msg));
}

bool BufferedReport::containsFailures() const noexcept
{
    return std::count_if(begin(), end(), [](const Message& m) { return m.type != Type::Warning; }) != 0;
}

void BufferedReport::clear()
{
    _messages.clear();
}

void BufferedReport::log() const
{
    for (const Message& message: _messages)
    {
        switch (message.type)
        {
            case Type::Warning: fmt::print("Warning: {}\n", message); break;
            default: fmt::print("Error: {}\n", message); break;
        }
    }
}

bool BufferedReport::operator==(const BufferedReport& other) const noexcept
{
    if (size() != other.size())
        return false;

    for (size_t i = 0, e = size(); i != e; ++i)
        if (_messages[i] != other._messages[i])
            return false;

    return true;
}

bool BufferedReport::contains(const Message& message) const noexcept
{
    for (const Message& m: _messages)
        if (m == message)
            return true;

    return false;
}

DifferenceReport difference(const BufferedReport& first, const BufferedReport& second)
{
    DifferenceReport diff;

    for (const Message& m: first)
        if (!second.contains(m))
            diff.first.push_back(m);

    for (const Message& m: second)
        if (!first.contains(m))
            diff.second.push_back(m);

    return diff;
}

std::ostream& operator<<(std::ostream& os, const BufferedReport& report)
{
    for (const Message& message: report)
    {
        switch (message.type)
        {
            case Type::Warning: os << fmt::format("Warning: {}\n", message); break;
            default: os << fmt::format("Error: {}\n", message); break;
        }
    }
    return os;
}
// }}}

} // namespace CoreVM::diagnostics
