// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <fmt/format.h>

#include <string>
#include <utility>

namespace CoreVM
{

//! \addtogroup CoreVM
//@{

struct FilePos
{ // {{{
    FilePos(): FilePos { 1, 1, 0 } {}
    FilePos(unsigned r, unsigned c): FilePos { r, c, 0 } {}
    FilePos(unsigned r, unsigned c, unsigned o): line(r), column(c), offset(o) {}

    FilePos& set(unsigned r, unsigned c, unsigned o)
    {
        line = r;
        column = c;
        offset = o;

        return *this;
    }

    void advance(char ch)
    {
        offset++;
        if (ch != '\n')
        {
            column++;
        }
        else
        {
            line++;
            column = 1;
        }
    }

    bool operator==(const FilePos& other) const noexcept
    {
        return line == other.line && column == other.column && offset == other.offset;
    }

    bool operator!=(const FilePos& other) const noexcept { return !(*this == other); }

    unsigned line;
    unsigned column;
    unsigned offset;
};

inline size_t operator-(const FilePos& a, const FilePos& b)
{
    if (b.offset > a.offset)
        return 1 + b.offset - a.offset;
    else
        return 1 + a.offset - b.offset;
}
// }}}

struct SourceLocation // {{{
{
    SourceLocation() = default;
    SourceLocation(std::string fileName): filename(std::move(fileName)) {}
    SourceLocation(std::string fileName, FilePos beg, FilePos end):
        filename(std::move(fileName)), begin(beg), end(end)
    {
    }

    std::string filename;
    FilePos begin;
    FilePos end;

    SourceLocation& update(const FilePos& endPos)
    {
        end = endPos;
        return *this;
    }

    SourceLocation& update(const SourceLocation& endLocation)
    {
        end = endLocation.end;
        return *this;
    }

    [[nodiscard]] std::string str() const;
    [[nodiscard]] std::string text() const;

    bool operator==(const SourceLocation& other) const noexcept
    {
        return filename == other.filename && begin == other.begin && end == other.end;
    }

    bool operator!=(const SourceLocation& other) const noexcept { return !(*this == other); }
}; // }}}

inline SourceLocation operator-(const SourceLocation& end, const SourceLocation& beg)
{
    return SourceLocation(beg.filename, beg.begin, end.end);
}

//!@}

} // namespace CoreVM

template <>
struct fmt::formatter<CoreVM::FilePos>: fmt::formatter<std::string>
{
    auto format(const CoreVM::FilePos& value, format_context& ctx) -> format_context::iterator
    {
        return formatter<std::string>::format(fmt::format("{}:{}", value.line, value.column), ctx);
    }
};

template <>
struct fmt::formatter<CoreVM::SourceLocation>: fmt::formatter<std::string>
{
    auto format(const CoreVM::SourceLocation& value, format_context& ctx) -> format_context::iterator
    {
        if (!value.filename.empty())
            return formatter<std::string>::format(fmt::format("{}:{}", value.filename, value.begin), ctx);
        else
            return formatter<std::string>::format(fmt::format("{}", value.begin), ctx);
    }
};
