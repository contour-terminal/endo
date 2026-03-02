// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <CoreVM/SourceLocation.hpp>

#include <cstdint>
#include <format>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace CoreVM::diagnostics
{

enum class Type : uint8_t
{
    TokenError,
    SyntaxError,
    TypeError,
    Warning,
    LinkError
};

std::string_view tos(Type type);

/// Diagnostic message with optional suggestions and source context.
struct Message
{
    Type type;
    SourceLocation sourceLocation;
    std::string text;
    std::vector<std::string> suggestions;      ///< Optional hints for fixing the error
    std::optional<std::string> contextSnippet; ///< Source line for display context

    /// Constructs a message without suggestions (backward compatible).
    Message(Type ty, SourceLocation sl, std::string t):
        type { ty }, sourceLocation { std::move(sl) }, text { std::move(t) }
    {
    }

    /// Constructs a message with suggestions and optional context snippet.
    Message(Type ty,
            SourceLocation sl,
            std::string t,
            std::vector<std::string> sugg,
            std::optional<std::string> ctx = std::nullopt):
        type { ty },
        sourceLocation { std::move(sl) },
        text { std::move(t) },
        suggestions { std::move(sugg) },
        contextSnippet { std::move(ctx) }
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
    void tokenError(const SourceLocation& sloc, std::format_string<Args...> f, Args... args)
    {
        emplace_back(Type::TokenError, sloc, std::format(f, std::move(args)...));
    }

    template <typename... Args>
    void syntaxError(const SourceLocation& sloc, std::format_string<Args...> f, Args... args)
    {
        emplace_back(Type::SyntaxError, sloc, std::format(f, std::move(args)...));
    }

    /// Reports a syntax error with suggestions for fixing.
    template <typename... Args>
    void syntaxErrorWithSuggestions(SourceLocation const& sloc,
                                    std::vector<std::string> suggestions,
                                    std::optional<std::string> contextSnippet,
                                    std::format_string<Args...> f,
                                    Args... args)
    {
        push_back(Message(Type::SyntaxError,
                          sloc,
                          std::format(f, std::move(args)...),
                          std::move(suggestions),
                          std::move(contextSnippet)));
    }

    template <typename... Args>
    void typeError(const SourceLocation& sloc, std::format_string<Args...> f, Args... args)
    {
        emplace_back(Type::TypeError, sloc, std::format(f, std::move(args)...));
    }

    /// Reports a type error with suggestions for fixing.
    template <typename... Args>
    void typeErrorWithSuggestions(SourceLocation const& sloc,
                                  std::vector<std::string> suggestions,
                                  std::optional<std::string> contextSnippet,
                                  std::format_string<Args...> f,
                                  Args... args)
    {
        push_back(Message(Type::TypeError,
                          sloc,
                          std::format(f, std::move(args)...),
                          std::move(suggestions),
                          std::move(contextSnippet)));
    }

    template <typename... Args>
    void warning(const SourceLocation& sloc, std::format_string<Args...> f, Args... args)
    {
        emplace_back(Type::Warning, sloc, std::format(f, std::move(args)...));
    }

    template <typename... Args>
    void linkError(std::format_string<Args...> f, Args... args)
    {
        emplace_back(
            Type::LinkError, SourceLocation {}, std::vformat(f.get(), std::make_format_args(args...)));
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

    /// Truncates the message list to the given size, discarding newer messages.
    void truncate(size_t n)
    {
        if (n < _messages.size())
            _messages.erase(_messages.begin() + static_cast<ptrdiff_t>(n), _messages.end());
    }

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
    size_t _errorCount = 0;
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
