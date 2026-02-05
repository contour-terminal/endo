// SPDX-License-Identifier: Apache-2.0
#include <algorithm>

#include <tui/Dialog.hpp>

namespace tui
{

// =============================================================================
// SelectDialog
// =============================================================================

SelectDialog::SelectDialog(SelectDialogConfig config):
    _config(std::move(config)), _list(std::move(_config.items))
{
    _list.setStyle(defaultListStyle());
}

auto SelectDialog::processEvent(InputEvent const& event) -> DialogResult
{
    auto const action = _list.processEvent(event);

    switch (action)
    {
        case ListAction::Selected: return DialogResult::Confirmed;
        case ListAction::Cancelled: return DialogResult::Cancelled;
        case ListAction::Changed: return DialogResult::Changed;
        case ListAction::None: break;
    }

    return DialogResult::None;
}

void SelectDialog::render(TerminalOutput& output)
{
    _cachedTermCols = output.columns();
    _cachedTermRows = output.rows();

    if (_config.dimBackground)
        renderBackground(output);

    auto boxConfig = calculateBounds(_cachedTermCols, _cachedTermRows);
    auto box = Box(boxConfig);
    box.render(output);

    // Render the list inside the box
    auto const listStartRow = box.contentStartRow();
    auto const listStartCol = box.contentStartCol();
    auto const listWidth = box.innerWidth();
    auto const listHeight = box.innerHeight();

    _list.render(output, listStartRow, listStartCol, listWidth, listHeight);

    // Render hint bar at bottom
    auto const hintRow = boxConfig.row + boxConfig.height;
    auto const hintText =
        std::string(_config.confirmHint) + " select  " + std::string(_config.cancelHint) + " cancel";
    auto const hintCol = boxConfig.col + (boxConfig.width - static_cast<int>(hintText.size())) / 2;

    auto hintStyle = Style {};
    hintStyle.dim = true;

    output.moveTo(hintRow, hintCol);
    output.write(hintText, hintStyle);
}

auto SelectDialog::selectedIndex() const noexcept -> std::size_t
{
    return _list.selectedIndex();
}

auto SelectDialog::selectedItem() const noexcept -> std::optional<ListItem const*>
{
    return _list.selectedItem();
}

void SelectDialog::setFilter(std::string_view filter)
{
    _list.setFilter(filter);
}

void SelectDialog::clearFilter()
{
    _list.clearFilter();
}

void SelectDialog::setConfig(SelectDialogConfig config)
{
    _config = std::move(config);
    _list.setItems(std::move(_config.items));
}

void SelectDialog::renderBackground(TerminalOutput& output)
{
    auto dimStyle = Style {};
    dimStyle.dim = true;

    // Fill the entire screen with a dim character or just apply dim attribute
    for (auto row = 1; row <= _cachedTermRows; ++row)
    {
        output.moveTo(row, 1);
        auto const spaces = std::string(static_cast<std::size_t>(_cachedTermCols), ' ');
        output.write(spaces, dimStyle);
    }
}

auto SelectDialog::calculateBounds(int termCols, int termRows) const -> BoxConfig
{
    auto const itemCount = static_cast<int>(_list.visibleItems().size());
    auto const contentHeight = std::min(itemCount, _config.maxHeight - 2);
    auto const dialogHeight = contentHeight + 2; // +2 for borders

    auto const dialogWidth = std::min(_config.width, termCols - 4);

    auto const startRow = (termRows - dialogHeight) / 2;
    auto const startCol = (termCols - dialogWidth) / 2 + 1;

    return BoxConfig {
        .row = startRow,
        .col = startCol,
        .width = dialogWidth,
        .height = dialogHeight,
        .border = _config.border,
        .borderStyle = _config.borderStyle,
        .title = _config.title,
        .titleAlign = TitleAlign::Center,
        .titleStyle = _config.titleStyle,
        .paddingLeft = 1,
        .paddingRight = 1,
        .paddingTop = 0,
        .paddingBottom = 0,
        .fillBackground = false,
        .backgroundStyle = {},
    };
}

// =============================================================================
// ConfirmDialog
// =============================================================================

ConfirmDialog::ConfirmDialog(ConfirmDialogConfig config):
    _config(std::move(config)), _confirmSelected(_config.defaultConfirm)
{
}

auto ConfirmDialog::processEvent(InputEvent const& event) -> DialogResult
{
    if (auto const* key = std::get_if<KeyEvent>(&event))
    {
        switch (key->key)
        {
            case KeyCode::Left: _confirmSelected = true; return DialogResult::Changed;

            case KeyCode::Right:
            case KeyCode::Tab: _confirmSelected = !_confirmSelected; return DialogResult::Changed;

            case KeyCode::Enter: return _confirmSelected ? DialogResult::Confirmed : DialogResult::Cancelled;

            case KeyCode::Escape: return DialogResult::Cancelled;

            default:
                // Handle y/n shortcuts
                if (isPrintable(key->key))
                {
                    if (key->codepoint == 'y' || key->codepoint == 'Y')
                        return DialogResult::Confirmed;
                    if (key->codepoint == 'n' || key->codepoint == 'N')
                        return DialogResult::Cancelled;
                }
                break;
        }
    }

    return DialogResult::None;
}

void ConfirmDialog::render(TerminalOutput& output)
{
    auto const termCols = output.columns();
    auto const termRows = output.rows();

    auto const dialogWidth = std::min(_config.width, termCols - 4);
    auto const dialogHeight = 6; // title + padding + message + padding + buttons + border

    auto const startRow = (termRows - dialogHeight) / 2;
    auto const startCol = (termCols - dialogWidth) / 2 + 1;

    // Dim background
    if (_config.dimBackground)
    {
        auto dimStyle = Style {};
        dimStyle.dim = true;

        for (auto row = 1; row <= termRows; ++row)
        {
            output.moveTo(row, 1);
            output.write(std::string(static_cast<std::size_t>(termCols), ' '), dimStyle);
        }
    }

    auto boxConfig = BoxConfig {
        .row = startRow,
        .col = startCol,
        .width = dialogWidth,
        .height = dialogHeight,
        .border = _config.border,
        .borderStyle = _config.borderStyle,
        .title = _config.title,
        .titleAlign = TitleAlign::Center,
        .titleStyle = _config.titleStyle,
        .paddingLeft = 2,
        .paddingRight = 2,
        .paddingTop = 1,
        .paddingBottom = 0,
        .fillBackground = true,
        .backgroundStyle = {},
    };

    auto box = Box(boxConfig);
    box.render(output);

    // Message
    auto const messageRow = box.contentStartRow();
    auto const messageCol = box.contentStartCol();
    output.moveTo(messageRow, messageCol);
    output.write(_config.message, _config.messageStyle);

    // Buttons
    auto const buttonRow = startRow + dialogHeight - 2;

    auto confirmStyle = Style {};
    auto cancelStyle = Style {};

    if (_confirmSelected)
    {
        confirmStyle.inverse = true;
        confirmStyle.bold = true;
    }
    else
    {
        cancelStyle.inverse = true;
        cancelStyle.bold = true;
    }

    auto const confirmBtn = " " + _config.confirmLabel + " ";
    auto const cancelBtn = " " + _config.cancelLabel + " ";
    auto const buttonsWidth = static_cast<int>(confirmBtn.size() + cancelBtn.size() + 4);
    auto const buttonsCol = startCol + (dialogWidth - buttonsWidth) / 2;

    output.moveTo(buttonRow, buttonsCol);
    output.write("[", _config.borderStyle);
    output.write(confirmBtn, confirmStyle);
    output.write("]", _config.borderStyle);
    output.writeRaw("  ");
    output.write("[", _config.borderStyle);
    output.write(cancelBtn, cancelStyle);
    output.write("]", _config.borderStyle);
}

auto ConfirmDialog::isConfirmSelected() const noexcept -> bool
{
    return _confirmSelected;
}

// =============================================================================
// InputDialog
// =============================================================================

InputDialog::InputDialog(InputDialogConfig config):
    _config(std::move(config)), _value(_config.initialValue), _cursor(_value.size())
{
}

auto InputDialog::processEvent(InputEvent const& event) -> DialogResult
{
    if (auto const* key = std::get_if<KeyEvent>(&event))
    {
        switch (key->key)
        {
            case KeyCode::Enter: return DialogResult::Confirmed;

            case KeyCode::Escape: return DialogResult::Cancelled;

            case KeyCode::Backspace:
                if (_cursor > 0)
                {
                    _value.erase(_cursor - 1, 1);
                    --_cursor;
                    return DialogResult::Changed;
                }
                break;

            case KeyCode::Delete:
                if (_cursor < _value.size())
                {
                    _value.erase(_cursor, 1);
                    return DialogResult::Changed;
                }
                break;

            case KeyCode::Left:
                if (_cursor > 0)
                {
                    --_cursor;
                    return DialogResult::Changed;
                }
                break;

            case KeyCode::Right:
                if (_cursor < _value.size())
                {
                    ++_cursor;
                    return DialogResult::Changed;
                }
                break;

            case KeyCode::Home: _cursor = 0; return DialogResult::Changed;

            case KeyCode::End: _cursor = _value.size(); return DialogResult::Changed;

            default:
                if (isPrintable(key->key))
                {
                    // Insert character at cursor
                    auto utf8 = std::string {};
                    if (key->codepoint < 0x80)
                    {
                        utf8 += static_cast<char>(key->codepoint);
                    }
                    else if (key->codepoint < 0x800)
                    {
                        utf8 += static_cast<char>(0xC0 | (key->codepoint >> 6));
                        utf8 += static_cast<char>(0x80 | (key->codepoint & 0x3F));
                    }
                    else if (key->codepoint < 0x10000)
                    {
                        utf8 += static_cast<char>(0xE0 | (key->codepoint >> 12));
                        utf8 += static_cast<char>(0x80 | ((key->codepoint >> 6) & 0x3F));
                        utf8 += static_cast<char>(0x80 | (key->codepoint & 0x3F));
                    }
                    else
                    {
                        utf8 += static_cast<char>(0xF0 | (key->codepoint >> 18));
                        utf8 += static_cast<char>(0x80 | ((key->codepoint >> 12) & 0x3F));
                        utf8 += static_cast<char>(0x80 | ((key->codepoint >> 6) & 0x3F));
                        utf8 += static_cast<char>(0x80 | (key->codepoint & 0x3F));
                    }

                    _value.insert(_cursor, utf8);
                    _cursor += utf8.size();
                    return DialogResult::Changed;
                }
                break;
        }
    }

    // Handle paste events
    if (auto const* paste = std::get_if<PasteEvent>(&event))
    {
        _value.insert(_cursor, paste->text);
        _cursor += paste->text.size();
        return DialogResult::Changed;
    }

    return DialogResult::None;
}

void InputDialog::render(TerminalOutput& output)
{
    auto const termCols = output.columns();
    auto const termRows = output.rows();

    auto const dialogWidth = std::min(_config.width, termCols - 4);
    auto const dialogHeight = 5; // border + prompt + input + border

    auto const startRow = (termRows - dialogHeight) / 2;
    auto const startCol = (termCols - dialogWidth) / 2 + 1;

    // Dim background
    if (_config.dimBackground)
    {
        auto dimStyle = Style {};
        dimStyle.dim = true;

        for (auto row = 1; row <= termRows; ++row)
        {
            output.moveTo(row, 1);
            output.write(std::string(static_cast<std::size_t>(termCols), ' '), dimStyle);
        }
    }

    auto boxConfig = BoxConfig {
        .row = startRow,
        .col = startCol,
        .width = dialogWidth,
        .height = dialogHeight,
        .border = _config.border,
        .borderStyle = _config.borderStyle,
        .title = _config.title,
        .titleAlign = TitleAlign::Center,
        .titleStyle = _config.titleStyle,
        .paddingLeft = 1,
        .paddingRight = 1,
        .paddingTop = 0,
        .paddingBottom = 0,
        .fillBackground = true,
        .backgroundStyle = {},
    };

    auto box = Box(boxConfig);
    box.render(output);

    // Prompt
    auto const promptRow = box.contentStartRow();
    auto const promptCol = box.contentStartCol();
    output.moveTo(promptRow, promptCol);
    output.write(_config.prompt, _config.titleStyle);

    // Input field
    auto const inputRow = promptRow + 1;
    auto const inputWidth = box.innerWidth();

    output.moveTo(inputRow, promptCol);

    if (_value.empty())
    {
        auto placeholderStyle = Style {};
        placeholderStyle.dim = true;
        output.write(_config.placeholder, placeholderStyle);
        auto const padLen = inputWidth - static_cast<int>(_config.placeholder.size());
        if (padLen > 0)
            output.writeRaw(std::string(static_cast<std::size_t>(padLen), ' '));
    }
    else
    {
        auto displayValue = _value;
        if (static_cast<int>(displayValue.size()) > inputWidth)
            displayValue = displayValue.substr(displayValue.size() - static_cast<std::size_t>(inputWidth));

        output.write(displayValue, _config.inputStyle);

        auto const padLen = inputWidth - static_cast<int>(displayValue.size());
        if (padLen > 0)
            output.writeRaw(std::string(static_cast<std::size_t>(padLen), ' '));
    }

    // Position cursor
    auto cursorCol = promptCol + static_cast<int>(_cursor);
    if (cursorCol > promptCol + inputWidth - 1)
        cursorCol = promptCol + inputWidth - 1;

    output.moveTo(inputRow, cursorCol);
    output.showCursor();
}

auto InputDialog::value() const noexcept -> std::string_view
{
    return _value;
}

void InputDialog::setValue(std::string_view value)
{
    _value = value;
    _cursor = _value.size();
}

} // namespace tui
