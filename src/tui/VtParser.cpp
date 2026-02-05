// SPDX-License-Identifier: Apache-2.0
#include <charconv>
#include <cstdint>
#include <ranges>
#include <string_view>
#include <vector>

#include <tui/VtParser.hpp>

namespace tui
{

namespace
{

    /// @brief Parses semicolon-separated integer parameters from a CSI parameter string.
    /// @param buf The parameter string (e.g. "0;10;20").
    /// @return Vector of parsed integer values.
    auto parseCsiParams(std::string_view buf) -> std::vector<int>
    {
        auto result = std::vector<int> {};
        for (auto const part: buf | std::views::split(';'))
        {
            auto sv = std::string_view(part.begin(), part.end());
            auto value = 0;
            if (auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), value); ec == std::errc {})
                result.push_back(value);
            else
                result.push_back(0);
        }
        return result;
    }

    /// @brief Decodes CSI modifier parameter to Modifier bitmask.
    ///
    /// CSI modifiers use the convention: encoded = 1 + (shift) + 2*(alt) + 4*(ctrl) + 8*(super).
    /// @param param The raw modifier parameter from CSI sequence.
    /// @return The decoded Modifier bitmask.
    constexpr auto decodeModifiers(int param) -> Modifier
    {
        if (param <= 1)
            return Modifier::None;
        auto const bits = param - 1;
        auto mods = Modifier::None;
        if (bits & 1)
            mods |= Modifier::Shift;
        if (bits & 2)
            mods |= Modifier::Alt;
        if (bits & 4)
            mods |= Modifier::Ctrl;
        if (bits & 8)
            mods |= Modifier::Super;
        return mods;
    }

    /// @brief Maps a CSI final byte with optional params to a KeyCode for cursor/function keys.
    /// @param finalByte The CSI final byte.
    /// @param params The parsed parameter list.
    /// @return The corresponding KeyEvent, or nullopt if not recognized.
    auto mapCsiKey(char finalByte, std::vector<int> const& params) -> std::optional<KeyEvent>
    {
        auto const modifier = (params.size() >= 2) ? decodeModifiers(params[1]) : Modifier::None;

        switch (finalByte)
        {
            case 'A': return KeyEvent { .key = KeyCode::Up, .modifiers = modifier };
            case 'B': return KeyEvent { .key = KeyCode::Down, .modifiers = modifier };
            case 'C': return KeyEvent { .key = KeyCode::Right, .modifiers = modifier };
            case 'D': return KeyEvent { .key = KeyCode::Left, .modifiers = modifier };
            case 'H': return KeyEvent { .key = KeyCode::Home, .modifiers = modifier };
            case 'F': return KeyEvent { .key = KeyCode::End, .modifiers = modifier };
            case '~': {
                if (params.empty())
                    return std::nullopt;
                switch (params[0])
                {
                    case 1: return KeyEvent { .key = KeyCode::Home, .modifiers = modifier };
                    case 2: return KeyEvent { .key = KeyCode::Insert, .modifiers = modifier };
                    case 3: return KeyEvent { .key = KeyCode::Delete, .modifiers = modifier };
                    case 4: return KeyEvent { .key = KeyCode::End, .modifiers = modifier };
                    case 5: return KeyEvent { .key = KeyCode::PageUp, .modifiers = modifier };
                    case 6: return KeyEvent { .key = KeyCode::PageDown, .modifiers = modifier };
                    case 11: return KeyEvent { .key = KeyCode::F1, .modifiers = modifier };
                    case 12: return KeyEvent { .key = KeyCode::F2, .modifiers = modifier };
                    case 13: return KeyEvent { .key = KeyCode::F3, .modifiers = modifier };
                    case 14: return KeyEvent { .key = KeyCode::F4, .modifiers = modifier };
                    case 15: return KeyEvent { .key = KeyCode::F5, .modifiers = modifier };
                    case 17: return KeyEvent { .key = KeyCode::F6, .modifiers = modifier };
                    case 18: return KeyEvent { .key = KeyCode::F7, .modifiers = modifier };
                    case 19: return KeyEvent { .key = KeyCode::F8, .modifiers = modifier };
                    case 20: return KeyEvent { .key = KeyCode::F9, .modifiers = modifier };
                    case 21: return KeyEvent { .key = KeyCode::F10, .modifiers = modifier };
                    case 23: return KeyEvent { .key = KeyCode::F11, .modifiers = modifier };
                    case 24: return KeyEvent { .key = KeyCode::F12, .modifiers = modifier };
                    default: return std::nullopt;
                }
            }
            default: return std::nullopt;
        }
    }

} // namespace

auto VtParser::feed(std::string_view data) -> std::vector<InputEvent>
{
    auto events = std::vector<InputEvent> {};
    for (auto const ch: data)
    {
        auto const byte = static_cast<std::uint8_t>(ch);
        switch (_state)
        {
            case State::Ground: processGround(byte, events); break;
            case State::Escape: processEscape(byte, events); break;
            case State::CsiEntry:
            case State::CsiParam: processCsi(byte, events); break;
            case State::Ss3: processSs3(byte, events); break;
            case State::PasteBody: processPaste(byte, events); break;
            case State::Utf8Sequence: processUtf8(byte, events); break;
        }
    }
    return events;
}

auto VtParser::timeout() -> std::vector<InputEvent>
{
    auto events = std::vector<InputEvent> {};
    if (_state == State::Escape)
    {
        // Bare ESC — resolve as Escape key
        events.emplace_back(KeyEvent { .key = KeyCode::Escape });
        _state = State::Ground;
    }
    return events;
}

void VtParser::processGround(std::uint8_t byte, std::vector<InputEvent>& events)
{
    if (byte == 0x1B)
    {
        _state = State::Escape;
        return;
    }

    // Control characters
    if (byte < 0x20)
    {
        switch (byte)
        {
            case '\r':
            case '\n': events.emplace_back(KeyEvent { .key = KeyCode::Enter }); break;
            case '\t': events.emplace_back(KeyEvent { .key = KeyCode::Tab }); break;
            case 0x7F:
            case 0x08: events.emplace_back(KeyEvent { .key = KeyCode::Backspace }); break;
            default: {
                // Ctrl+letter: byte = letter - 'a' + 1
                auto const cp = static_cast<char32_t>(byte + 'a' - 1);
                events.emplace_back(KeyEvent {
                    .key = keyCodeFromCodepoint(cp), .modifiers = Modifier::Ctrl, .codepoint = cp });
                break;
            }
        }
        return;
    }

    if (byte == 0x7F)
    {
        events.emplace_back(KeyEvent { .key = KeyCode::Backspace });
        return;
    }

    // UTF-8 multi-byte lead byte
    if ((byte & 0x80) != 0)
    {
        _utf8Buf.clear();
        _utf8Buf += static_cast<char>(byte);
        if ((byte & 0xE0) == 0xC0)
            _utf8Remaining = 1;
        else if ((byte & 0xF0) == 0xE0)
            _utf8Remaining = 2;
        else if ((byte & 0xF8) == 0xF0)
            _utf8Remaining = 3;
        else
        {
            // Invalid lead byte, ignore
            return;
        }
        _state = State::Utf8Sequence;
        return;
    }

    // ASCII printable
    emitCodepoint(static_cast<char32_t>(byte), events);
}

void VtParser::processEscape(std::uint8_t byte, std::vector<InputEvent>& events)
{
    if (byte == '[')
    {
        _paramBuf.clear();
        _state = State::CsiEntry;
        return;
    }

    if (byte == 'O')
    {
        _state = State::Ss3;
        return;
    }

    // Alt+Enter: ESC followed by CR or LF
    if (byte == '\r' || byte == '\n')
    {
        events.emplace_back(KeyEvent { .key = KeyCode::Enter, .modifiers = Modifier::Alt });
        _state = State::Ground;
        return;
    }

    // Alt+character: ESC followed by printable
    if (byte >= 0x20 && byte < 0x7F)
    {
        auto const cp = static_cast<char32_t>(byte);
        events.emplace_back(
            KeyEvent { .key = keyCodeFromCodepoint(cp), .modifiers = Modifier::Alt, .codepoint = cp });
        _state = State::Ground;
        return;
    }

    // ESC ESC or ESC + control: emit ESC then reprocess
    events.emplace_back(KeyEvent { .key = KeyCode::Escape });
    _state = State::Ground;
    processGround(byte, events);
}

void VtParser::processCsi(std::uint8_t byte, std::vector<InputEvent>& events)
{
    // Parameter bytes: digits, semicolons, and private markers
    if ((byte >= '0' && byte <= '9') || byte == ';' || byte == '<' || byte == '>' || byte == '?')
    {
        _paramBuf += static_cast<char>(byte);
        _state = State::CsiParam;
        return;
    }

    // Final byte in range [0x40, 0x7E]
    if (byte >= 0x40 && byte <= 0x7E)
    {
        dispatchCsi(static_cast<char>(byte), events);
        // Only reset to Ground if dispatchCsi didn't change state (e.g. to PasteBody)
        if (_state == State::CsiEntry || _state == State::CsiParam)
            _state = State::Ground;
        return;
    }

    // Intermediate bytes (0x20-0x2F) — accumulate
    if (byte >= 0x20 && byte <= 0x2F)
    {
        _paramBuf += static_cast<char>(byte);
        return;
    }

    // Unexpected byte — abort sequence
    _state = State::Ground;
}

void VtParser::processSs3(std::uint8_t byte, std::vector<InputEvent>& events)
{
    _state = State::Ground;
    switch (static_cast<char>(byte))
    {
        case 'A': events.emplace_back(KeyEvent { .key = KeyCode::Up }); break;
        case 'B': events.emplace_back(KeyEvent { .key = KeyCode::Down }); break;
        case 'C': events.emplace_back(KeyEvent { .key = KeyCode::Right }); break;
        case 'D': events.emplace_back(KeyEvent { .key = KeyCode::Left }); break;
        case 'H': events.emplace_back(KeyEvent { .key = KeyCode::Home }); break;
        case 'F': events.emplace_back(KeyEvent { .key = KeyCode::End }); break;
        case 'P': events.emplace_back(KeyEvent { .key = KeyCode::F1 }); break;
        case 'Q': events.emplace_back(KeyEvent { .key = KeyCode::F2 }); break;
        case 'R': events.emplace_back(KeyEvent { .key = KeyCode::F3 }); break;
        case 'S': events.emplace_back(KeyEvent { .key = KeyCode::F4 }); break;
        default: break;
    }
}

void VtParser::processPaste(std::uint8_t byte, std::vector<InputEvent>& events)
{
    _pasteBuf += static_cast<char>(byte);

    // Check for bracketed paste end: ESC[201~
    static constexpr auto PasteEnd = std::string_view { "\033[201~" };
    if (_pasteBuf.size() >= PasteEnd.size() && _pasteBuf.ends_with(PasteEnd))
    {
        // Remove the end sequence from the paste buffer
        _pasteBuf.resize(_pasteBuf.size() - PasteEnd.size());
        events.emplace_back(PasteEvent { .text = std::move(_pasteBuf) });
        _pasteBuf.clear();
        _state = State::Ground;
    }
}

void VtParser::processUtf8(std::uint8_t byte, std::vector<InputEvent>& events)
{
    if ((byte & 0xC0) != 0x80)
    {
        // Invalid continuation byte — discard sequence, reprocess
        _utf8Buf.clear();
        _state = State::Ground;
        processGround(byte, events);
        return;
    }

    _utf8Buf += static_cast<char>(byte);
    --_utf8Remaining;

    if (_utf8Remaining == 0)
    {
        emitUtf8(events);
        _utf8Buf.clear();
        _state = State::Ground;
    }
}

void VtParser::dispatchCsi(char finalByte, std::vector<InputEvent>& events)
{
    // Check for SGR mouse: ESC[<button;x;yM or ESC[<button;x;ym
    if (!_paramBuf.empty() && _paramBuf[0] == '<' && (finalByte == 'M' || finalByte == 'm'))
    {
        auto const params = parseCsiParams(_paramBuf.substr(1));
        if (params.size() >= 3)
        {
            auto const rawButton = params[0];
            auto const x = params[1];
            auto const y = params[2];
            auto const isRelease = (finalByte == 'm');

            auto mods = Modifier::None;
            if (rawButton & 4)
                mods |= Modifier::Shift;
            if (rawButton & 8)
                mods |= Modifier::Alt;
            if (rawButton & 16)
                mods |= Modifier::Ctrl;

            auto const buttonBits = rawButton & 0x43; // button encoding bits

            if (buttonBits == 64)
            {
                events.emplace_back(
                    MouseEvent { .type = MouseEvent::Type::ScrollUp, .x = x, .y = y, .modifiers = mods });
            }
            else if (buttonBits == 65)
            {
                events.emplace_back(
                    MouseEvent { .type = MouseEvent::Type::ScrollDown, .x = x, .y = y, .modifiers = mods });
            }
            else if (rawButton & 32)
            {
                events.emplace_back(MouseEvent { .type = MouseEvent::Type::Move,
                                                 .button = buttonBits & 3,
                                                 .x = x,
                                                 .y = y,
                                                 .modifiers = mods });
            }
            else if (isRelease)
            {
                events.emplace_back(MouseEvent { .type = MouseEvent::Type::Release,
                                                 .button = buttonBits & 3,
                                                 .x = x,
                                                 .y = y,
                                                 .modifiers = mods });
            }
            else
            {
                events.emplace_back(MouseEvent { .type = MouseEvent::Type::Press,
                                                 .button = buttonBits & 3,
                                                 .x = x,
                                                 .y = y,
                                                 .modifiers = mods });
            }
        }
        return;
    }

    // Check for bracketed paste start: ESC[200~
    if (finalByte == '~' && _paramBuf == "200")
    {
        _pasteBuf.clear();
        _state = State::PasteBody;
        return;
    }

    // Check for CSIu (Kitty keyboard protocol): ESC[keycode;modifiers u
    if (finalByte == 'u')
    {
        auto const params = parseCsiParams(_paramBuf);
        auto const keycode = params.empty() ? 0 : params[0];
        auto const modifier = (params.size() >= 2) ? decodeModifiers(params[1]) : Modifier::None;

        switch (keycode)
        {
            case 13: events.emplace_back(KeyEvent { .key = KeyCode::Enter, .modifiers = modifier }); return;
            case 9: events.emplace_back(KeyEvent { .key = KeyCode::Tab, .modifiers = modifier }); return;
            case 127:
                events.emplace_back(KeyEvent { .key = KeyCode::Backspace, .modifiers = modifier });
                return;
            case 27: events.emplace_back(KeyEvent { .key = KeyCode::Escape, .modifiers = modifier }); return;
            default: break;
        }

        // Printable codepoint
        if (keycode >= 32 && keycode < 0x10000)
        {
            auto const cp = static_cast<char32_t>(keycode);
            events.emplace_back(
                KeyEvent { .key = keyCodeFromCodepoint(cp), .modifiers = modifier, .codepoint = cp });
        }
        return;
    }

    // Standard CSI sequences (cursor keys, function keys, etc.)
    // Skip sequences with private markers (>, ?, but not <)
    if (!_paramBuf.empty() && (_paramBuf[0] == '>' || _paramBuf[0] == '?'))
        return;

    auto const params = parseCsiParams(_paramBuf);
    if (auto key = mapCsiKey(finalByte, params))
    {
        events.emplace_back(*key);
    }
}

void VtParser::emitCodepoint(char32_t cp, std::vector<InputEvent>& events)
{
    events.emplace_back(KeyEvent { .key = keyCodeFromCodepoint(cp), .codepoint = cp });
}

void VtParser::emitUtf8(std::vector<InputEvent>& events)
{
    // Decode UTF-8 sequence to a single codepoint
    auto const* const bytes = reinterpret_cast<std::uint8_t const*>(_utf8Buf.data());
    auto const len = _utf8Buf.size();
    auto cp = char32_t { 0 };

    if (len == 2)
        cp = static_cast<char32_t>(((bytes[0] & 0x1F) << 6) | (bytes[1] & 0x3F));
    else if (len == 3)
        cp = static_cast<char32_t>(((bytes[0] & 0x0F) << 12) | ((bytes[1] & 0x3F) << 6) | (bytes[2] & 0x3F));
    else if (len == 4)
        cp = static_cast<char32_t>(((bytes[0] & 0x07) << 18) | ((bytes[1] & 0x3F) << 12)
                                   | ((bytes[2] & 0x3F) << 6) | (bytes[3] & 0x3F));

    if (cp != 0)
        events.emplace_back(KeyEvent { .key = keyCodeFromCodepoint(cp), .codepoint = cp });
}

} // namespace tui
