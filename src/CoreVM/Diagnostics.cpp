// SPDX-License-Identifier: Apache-2.0
#include <endo-language/LogConfig.hpp>

#include <CoreVM/CoreVM.hpp>

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

std::string_view tos(Type type)
{
    switch (type)
    {
        case Type::TokenError: return "TokenError";
        case Type::SyntaxError: return "SyntaxError";
        case Type::TypeError: return "TypeError";
        case Type::Warning: return "Warning";
        case Type::LinkError: return "LinkError";
    }
    return "Unknown";
}

// {{{ Message
std::string Message::string() const
{
    switch (type)
    {
        case Type::Warning: return "[" + sourceLocation.str() + "] " + text;
        case Type::LinkError: return std::string(tos(type)) + ": " + text;
        default: return "[" + sourceLocation.str() + "] " + std::string(tos(type)) + ": " + text;
    }
}

bool Message::operator==(const Message& other) const noexcept
{
    // XXX ignore SourceLocation's filename & end
    return type == other.type && sourceLocation.begin == other.sourceLocation.begin && text == other.text;
}

// }}}
// {{{ ConsoleReport
ConsoleReport::ConsoleReport() = default;

bool ConsoleReport::containsFailures() const noexcept
{
    return _errorCount != 0;
}

void ConsoleReport::push_back(Message msg)
{
    if (msg.type != Type::Warning)
        _errorCount++;

    // Format: filename:line:column: type: message
    auto const& loc = msg.sourceLocation;
    std::string_view typeStr;
    switch (msg.type)
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
            "{}:{}:{}: {}: {}\n", loc.filename, loc.begin.line, loc.begin.column, typeStr, msg.text);
    }
    else
    {
        std::cerr << std::format("{}: {}\n", typeStr, msg.text);
    }

    // Print context snippet with caret if available
    if (msg.contextSnippet.has_value())
    {
        std::cerr << std::format("  | {}\n", msg.contextSnippet.value());

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
    for (auto const& suggestion: msg.suggestions)
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
            case Type::Warning: diagnosticsLog()()("Warning: {}\n", message.string()); break;
            default: diagnosticsLog()()("Error: {}\n", message.string()); break;
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
            case Type::Warning: os << "Warning: " + message.string() + "\n"; break;
            default: os << "Error: " + message.string() + "\n"; break;
        }
    }
    return os;
}

// }}}

} // namespace CoreVM::diagnostics
