// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <CoreVM/SourceLocation.h>

#include <fmt/format.h>

#include <algorithm>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace CoreVM::diagnostics
{

enum class Type
{
    TokenError,
    SyntaxError,
    TypeError,
    Warning,
    LinkError
};

struct Message
{
    Type type;
    SourceLocation sourceLocation;
    std::string text;

    Message(Type ty, SourceLocation sl, std::string t):
        type { ty }, sourceLocation { std::move(sl) }, text { std::move(t) }
    {
    }

    [[nodiscard]] std::string string() const;

    bool operator==(const Message& other) const noexcept;
    bool operator!=(const Message& other) const noexcept { return !(*this == other); }
};

class Report
{
  public:
    virtual ~Report() = default;

    template <typename... Args>
    void tokenError(const SourceLocation& sloc, const std::string& f, Args... args)
    {
        emplace_back(Type::TokenError, sloc, fmt::format(f, std::move(args)...));
    }

    template <typename... Args>
    void syntaxError(const SourceLocation& sloc, fmt::format_string<Args...> f, Args... args)
    {
        emplace_back(Type::SyntaxError, sloc, fmt::format(f, std::move(args)...));
    }

    template <typename... Args>
    void typeError(const SourceLocation& sloc, fmt::format_string<Args...> f, Args... args)
    {
        emplace_back(Type::TypeError, sloc, fmt::format(f, std::move(args)...));
    }

    template <typename... Args>
    void warning(const SourceLocation& sloc, fmt::format_string<Args...> f, Args... args)
    {
        emplace_back(Type::Warning, sloc, fmt::format(f, std::move(args)...));
    }

    template <typename... Args>
    void linkError(fmt::format_string<Args...> f, Args... args)
    {
        emplace_back(Type::LinkError, SourceLocation {}, fmt::vformat(f, fmt::make_format_args(args)...));
    }

    void emplace_back(Type ty, SourceLocation sl, std::string t)
    {
        push_back(Message(ty, std::move(sl), std::move(t)));
    }

    virtual void push_back(Message msg) = 0;
    [[nodiscard]] virtual bool containsFailures() const noexcept = 0;
};

using MessageList = std::vector<Message>;

class BufferedReport: public Report
{
  public:
    void push_back(Message msg) override;
    [[nodiscard]] bool containsFailures() const noexcept override;

    void log() const;

    [[nodiscard]] const MessageList& messages() const noexcept { return _messages; }

    void clear();
    [[nodiscard]] size_t size() const noexcept { return _messages.size(); }
    const Message& operator[](size_t i) const { return _messages[i]; }

    using iterator = MessageList::iterator;
    using const_iterator = MessageList::const_iterator;
    iterator begin() noexcept { return _messages.begin(); }
    iterator end() noexcept { return _messages.end(); }
    [[nodiscard]] const_iterator begin() const noexcept { return _messages.begin(); }
    [[nodiscard]] const_iterator end() const noexcept { return _messages.end(); }

    [[nodiscard]] bool contains(const Message& m) const noexcept;

    bool operator==(const BufferedReport& other) const noexcept;
    bool operator!=(const BufferedReport& other) const noexcept { return !(*this == other); }

  private:
    MessageList _messages;
};

class ConsoleReport: public Report
{
  public:
    ConsoleReport();

    void push_back(Message msg) override;
    [[nodiscard]] bool containsFailures() const noexcept override;

  private:
    size_t _errorCount;
};

std::ostream& operator<<(std::ostream& os, const Report& report);

using DifferenceReport = std::pair<MessageList, MessageList>;

DifferenceReport difference(const BufferedReport& first, const BufferedReport& second);

class DiagnosticsError: public std::runtime_error
{
  public:
    explicit DiagnosticsError(SourceLocation sloc, const std::string& msg):
        std::runtime_error { msg }, _sloc { std::move(sloc) }
    {
    }

    [[nodiscard]] const SourceLocation& sourceLocation() const noexcept { return _sloc; }

  private:
    SourceLocation _sloc;
};

class LexerError: public DiagnosticsError
{
  public:
    LexerError(SourceLocation sloc, const std::string& msg): DiagnosticsError { std::move(sloc), msg } {}
};

class SyntaxError: public DiagnosticsError
{
  public:
    SyntaxError(SourceLocation sloc, const std::string& msg): DiagnosticsError { std::move(sloc), msg } {}
};

class TypeError: public DiagnosticsError
{
  public:
    TypeError(SourceLocation sloc, const std::string& msg): DiagnosticsError { std::move(sloc), msg } {}
};

} // namespace CoreVM::diagnostics

template <>
struct fmt::formatter<CoreVM::diagnostics::Type>: fmt::formatter<std::string_view>
{
    using Type = CoreVM::diagnostics::Type;

    auto format(const Type& value, format_context& ctx) -> format_context::iterator
    {
        string_view name;
        switch (value)
        {
            case Type::TokenError: name = "TokenError"; break;
            case Type::SyntaxError: name = "SyntaxError"; break;
            case Type::TypeError: name = "TypeError"; break;
            case Type::Warning: name = "Warning"; break;
            case Type::LinkError: name = "LinkError"; break;
        }
        return formatter<string_view>::format(name, ctx);
    }
};

template <>
struct fmt::formatter<CoreVM::diagnostics::Message>: fmt::formatter<std::string>
{
    using Message = CoreVM::diagnostics::Message;

    auto format(const Message& value, format_context& ctx) -> format_context::iterator
    {
        return formatter<std::string>::format(value.string(), ctx);
    }
};
