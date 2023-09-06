#pragma once

#include <boxed-cpp/boxed.hpp>
#include <compare>

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

class InputEditor
{
  public:
    InputEditor(TTY& tty, Rect rect);
    ~InputEditor();

    void run();

  private:
    TTY& _tty;
    Rect _rect;
};

} // namespace InputEditor
