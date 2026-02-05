// SPDX-License-Identifier: Apache-2.0
#include <algorithm>

#include <tui/StatusBar.hpp>

namespace tui
{

void StatusBar::setHints(std::vector<KeyHint> hints)
{
    _hints = std::move(hints);
}

void StatusBar::addHint(std::string key, std::string action)
{
    _hints.push_back(KeyHint { .key = std::move(key), .action = std::move(action) });
}

void StatusBar::clearHints()
{
    _hints.clear();
}

void StatusBar::setLeftText(std::string text)
{
    _leftText = std::move(text);
}

void StatusBar::setCenterText(std::string text)
{
    _centerText = std::move(text);
}

void StatusBar::setRightText(std::string text)
{
    _rightText = std::move(text);
}

void StatusBar::setStyle(StatusBarStyle style)
{
    _style = std::move(style);
}

auto StatusBar::style() const noexcept -> StatusBarStyle const&
{
    return _style;
}

void StatusBar::render(TerminalOutput& output, int row, int width) const
{
    output.moveTo(row, 1);

    // Fill background
    auto const bgSpaces = std::string(static_cast<std::size_t>(width), ' ');
    output.write(bgSpaces, _style.background);

    // Calculate section widths
    auto const rightWidth = static_cast<int>(_rightText.size());
    auto const centerWidth = static_cast<int>(_centerText.size());

    // If we have hints, render them on the left
    if (!_hints.empty())
    {
        output.moveTo(row, 2);

        auto first = true;
        for (auto const& hint: _hints)
        {
            if (!first)
                output.write(_style.separatorChar, _style.separator);
            first = false;

            output.write(hint.key, _style.keyStyle);
            output.writeRaw(" ");
            output.write(hint.action, _style.actionStyle);
        }
    }
    else if (!_leftText.empty())
    {
        output.moveTo(row, 2);
        output.write(_leftText, _style.actionStyle);
    }

    // Center text
    if (!_centerText.empty())
    {
        auto const centerCol = (width - centerWidth) / 2 + 1;
        output.moveTo(row, centerCol);
        output.write(_centerText, _style.actionStyle);
    }

    // Right text
    if (!_rightText.empty())
    {
        auto const rightCol = width - rightWidth;
        output.moveTo(row, rightCol);
        output.write(_rightText, _style.actionStyle);
    }
}

void StatusBar::renderAtBottom(TerminalOutput& output) const
{
    render(output, output.rows(), output.columns());
}

auto StatusBar::formatHints() const -> std::string
{
    auto result = std::string {};
    auto first = true;

    for (auto const& hint: _hints)
    {
        if (!first)
            result += _style.separatorChar;
        first = false;

        result += hint.key;
        result += " ";
        result += hint.action;
    }

    return result;
}

auto StatusBar::hintsWidth() const -> int
{
    auto width = 0;

    for (auto i = std::size_t { 0 }; i < _hints.size(); ++i)
    {
        if (i > 0)
            width += static_cast<int>(_style.separatorChar.size());

        width += static_cast<int>(_hints[i].key.size());
        width += 1; // space
        width += static_cast<int>(_hints[i].action.size());
    }

    return width;
}

auto defaultStatusBarStyle() -> StatusBarStyle
{
    auto style = StatusBarStyle {};

    // Dark background
    style.background.bg = static_cast<std::uint8_t>(236); // Dark gray
    style.background.fg = static_cast<std::uint8_t>(252); // Light gray

    // Keys in bold/accent color
    style.keyStyle.bold = true;
    style.keyStyle.fg = RgbColor { .r = 130, .g = 180, .b = 255 }; // Light blue

    // Actions in normal color
    style.actionStyle.fg = static_cast<std::uint8_t>(252); // Light gray

    // Separator dim
    style.separator.dim = true;

    return style;
}

} // namespace tui
