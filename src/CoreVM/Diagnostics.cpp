// SPDX-License-Identifier: Apache-2.0
#include <CoreVM/CoreVM.hpp>

#include <shell/LogConfig.hpp>

#include <format>
#include <iostream>
#include <print>

namespace
{
// Use function-local static to avoid C++20 module static initialization issues
auto& diagnosticsLog()
{
    static auto instance =
        logstore::category("vm.diag", "VM diagnostics log", endo::log::categoryState("vm.diag"));
    return instance;
}
} // namespace

namespace CoreVM::diagnostics
{

// {{{ Message
std::string Message::string() const
{
    switch (type)
    {
        case Type::Warning: return std::format("[{}] {}", sourceLocation, text);
        case Type::LinkError: return std::format("{}: {}", type, text);
        default: return std::format("[{}] {}: {}", sourceLocation, type, text);
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

    // Format: filename:line:column: type: message
    auto const& loc = message.sourceLocation;
    std::string_view typeStr;
    switch (message.type)
    {
        case Type::TokenError: typeStr = "token error"; break;
        case Type::SyntaxError: typeStr = "syntax error"; break;
        case Type::TypeError: typeStr = "type error"; break;
        case Type::Warning: typeStr = "warning"; break;
        case Type::LinkError: typeStr = "link error"; break;
    }

    // Print location and message
    if (!loc.filename.empty())
    {
        std::cerr << std::format(
            "{}:{}:{}: {}: {}\n", loc.filename, loc.begin.line, loc.begin.column, typeStr, message.text);
    }
    else
    {
        std::cerr << std::format("{}: {}\n", typeStr, message.text);
    }

    // Print context snippet with caret if available
    if (message.contextSnippet.has_value())
    {
        std::cerr << std::format("  | {}\n", message.contextSnippet.value());

        // Create caret line pointing to the error column
        if (loc.begin.column > 0)
        {
            auto const column = static_cast<int>(loc.begin.column) - 1; // Convert to 0-based for display
            auto const length = (loc.end.column > loc.begin.column) ? (loc.end.column - loc.begin.column) : 1;
            std::string caretLine(static_cast<size_t>(column), ' ');
            caretLine += '^';
            if (length > 1)
                caretLine += std::string(static_cast<size_t>(length - 1), '~');
            std::cerr << std::format("  | {}\n", caretLine);
        }
    }

    // Print suggestions as hints
    for (auto const& suggestion: message.suggestions)
    {
        std::cerr << std::format("  hint: {}\n", suggestion);
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
    if (!diagnosticsLog().is_enabled())
        return;

    for (const Message& message: _messages)
    {
        switch (message.type)
        {
            case Type::Warning: diagnosticsLog()()("Warning: {}\n", message); break;
            default: diagnosticsLog()()("Error: {}\n", message); break;
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
            case Type::Warning: os << std::format("Warning: {}\n", message); break;
            default: os << std::format("Error: {}\n", message); break;
        }
    }
    return os;
}

// }}}

} // namespace CoreVM::diagnostics
