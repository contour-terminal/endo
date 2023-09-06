// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <compare>
#include <string>
#include <vector>

#include <boxed-cpp/boxed.hpp>
#include <libunicode/scan.h>

namespace InputEditor
{

// clang-format off
namespace detail 
{
    struct LineOffset {};
    struct ColumnOffset {};
}
// clang-format on
using LineOffset = boxed::boxed<unsigned, detail::LineOffset>;
using ColumnOffset = boxed::boxed<unsigned, detail::ColumnOffset>;

struct Cursor
{
    LineOffset line;
    ColumnOffset column;

    constexpr std::strong_ordering operator<=>(const Cursor& rhs) const noexcept = default;
};

struct Rect
{
    Cursor start;
    Cursor end;
};

struct TTY
{
    int inputFd;
    int outputFd;
};

struct GraphemeCluster
{
    std::vector<char32_t> codepoints;

    [[nodiscard]] std::string as_utf8() const
    {
        std::string result;
        result.reserve(codepoints.size() * 4);
        for (auto const codepoint: codepoints)
            result += unicode::to_utf8(codepoint);
        return result;
    }
};

struct StyledGraphemeCluster
{
    GraphemeCluster grapheme;
    std::string style;
};

struct Prompt
{
    std::string text;
    std::vector<StyledGraphemeCluster> styledGraphemes;
};

class InputEditor
{
  public:
    InputEditor(TTY tty, Rect rect, Prompt prompt);
    ~InputEditor();

    void run();

  private:
    TTY _tty;
    Rect _rect;
    Prompt _prompt;

    std::string _buffer;
    std::vector<GraphemeCluster> _graphemes;
};

} // namespace InputEditor
