// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <fmt/format.h>

#include <memory>
#include <regex>
#include <string>
#include <vector>

namespace CoreVM::util
{

class BufferRef;

class RegExp
{
  private:
    std::string _pattern;
    std::regex _re;

  public:
    using Result = std::smatch;

  public:
    explicit RegExp(std::string const& pattern);
    RegExp() = default;
    RegExp(RegExp const& v) = default;
    ~RegExp() = default;

    RegExp(RegExp&& v) noexcept = default;
    RegExp& operator=(RegExp&& v) noexcept = default;

    bool match(const std::string& target, Result* result = nullptr) const;

    [[nodiscard]] const std::string& pattern() const { return _pattern; }
    [[nodiscard]] const char* c_str() const;

    operator const std::string&() const { return _pattern; }

    friend bool operator==(const RegExp& a, const RegExp& b) { return a.pattern() == b.pattern(); }
    friend bool operator!=(const RegExp& a, const RegExp& b) { return a.pattern() != b.pattern(); }
    friend bool operator<=(const RegExp& a, const RegExp& b) { return a.pattern() <= b.pattern(); }
    friend bool operator>=(const RegExp& a, const RegExp& b) { return a.pattern() >= b.pattern(); }
    friend bool operator<(const RegExp& a, const RegExp& b) { return a.pattern() < b.pattern(); }
    friend bool operator>(const RegExp& a, const RegExp& b) { return a.pattern() > b.pattern(); }
};

class RegExpContext
{
  public:
    const RegExp::Result* regexMatch() const
    {
        if (!_regexMatch)
            _regexMatch = std::make_unique<RegExp::Result>();

        return _regexMatch.get();
    }

    RegExp::Result* regexMatch()
    {
        if (!_regexMatch)
            _regexMatch = std::make_unique<RegExp::Result>();

        return _regexMatch.get();
    }

  private:
    mutable std::unique_ptr<RegExp::Result> _regexMatch;
};

} // namespace CoreVM::util

template <>
struct fmt::formatter<CoreVM::util::RegExp>: fmt::formatter<std::string>
{
    using RegExp = CoreVM::util::RegExp;

    auto format(RegExp const& v, format_context& ctx) -> format_context::iterator
    {
        return formatter<std::string>::format(v.pattern(), ctx);
    }
};
