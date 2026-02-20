// SPDX-License-Identifier: Apache-2.0
#include "KeyBindings.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <sstream>
#include <utility>

namespace tui
{

namespace
{
    // Case-insensitive string comparison
    bool equalsIgnoreCase(std::string_view a, std::string_view b)
    {
        if (a.size() != b.size())
            return false;
        for (size_t i = 0; i < a.size(); ++i)
        {
            if (std::tolower(static_cast<unsigned char>(a[i]))
                != std::tolower(static_cast<unsigned char>(b[i])))
                return false;
        }
        return true;
    }

    // Special key name mappings
    struct KeyNameMapping
    {
        std::string_view name;
        KeyCode code;
    };

    constexpr std::array<KeyNameMapping, 26> keyNameMappings = { {
        { "enter", KeyCode::Enter },   { "return", KeyCode::Enter },
        { "tab", KeyCode::Tab },       { "backspace", KeyCode::Backspace },
        { "delete", KeyCode::Delete }, { "del", KeyCode::Delete },
        { "escape", KeyCode::Escape }, { "esc", KeyCode::Escape },
        { "up", KeyCode::Up },         { "down", KeyCode::Down },
        { "left", KeyCode::Left },     { "right", KeyCode::Right },
        { "home", KeyCode::Home },     { "end", KeyCode::End },
        { "pageup", KeyCode::PageUp }, { "pagedown", KeyCode::PageDown },
        { "insert", KeyCode::Insert }, { "f1", KeyCode::F1 },
        { "f2", KeyCode::F2 },         { "f3", KeyCode::F3 },
        { "f4", KeyCode::F4 },         { "f5", KeyCode::F5 },
        { "f6", KeyCode::F6 },         { "f7", KeyCode::F7 },
        { "f8", KeyCode::F8 },         { "f9", KeyCode::F9 },
    } };

    // More function keys
    constexpr std::array<KeyNameMapping, 3> moreKeyNameMappings = { {
        { "f10", KeyCode::F10 },
        { "f11", KeyCode::F11 },
        { "f12", KeyCode::F12 },
    } };

    std::optional<KeyCode> parseSpecialKey(std::string_view name)
    {
        for (auto const& mapping: keyNameMappings)
        {
            if (equalsIgnoreCase(name, mapping.name))
                return mapping.code;
        }
        for (auto const& mapping: moreKeyNameMappings)
        {
            if (equalsIgnoreCase(name, mapping.name))
                return mapping.code;
        }
        return std::nullopt;
    }

    std::string_view keyCodeToString(KeyCode code)
    {
        for (auto const& mapping: keyNameMappings)
        {
            if (mapping.code == code)
                return mapping.name;
        }
        for (auto const& mapping: moreKeyNameMappings)
        {
            if (mapping.code == code)
                return mapping.name;
        }
        return "";
    }

    // Action name mappings
    struct ActionNameMapping
    {
        std::string_view name;
        EditAction action;
    };

    constexpr std::array<ActionNameMapping, 36> actionNameMappings = { {
        // Movement
        { "move-forward-char", EditAction::MoveForwardChar },
        { "move-backward-char", EditAction::MoveBackwardChar },
        { "move-forward-word", EditAction::MoveForwardWord },
        { "move-backward-word", EditAction::MoveBackwardWord },
        { "move-to-line-start", EditAction::MoveToLineStart },
        { "move-to-line-end", EditAction::MoveToLineEnd },
        { "move-to-buffer-start", EditAction::MoveToBufferStart },
        { "move-to-buffer-end", EditAction::MoveToBufferEnd },
        { "move-up", EditAction::MoveUp },
        { "move-down", EditAction::MoveDown },
        { "smart-move-to-line-start", EditAction::SmartMoveToLineStart },
        { "smart-move-to-line-end", EditAction::SmartMoveToLineEnd },
        // Editing
        { "delete-char-backward", EditAction::DeleteCharBackward },
        { "delete-char-forward", EditAction::DeleteCharForward },
        { "delete-word", EditAction::DeleteWord },
        { "delete-word-backward", EditAction::DeleteWordBackward },
        { "kill-to-end", EditAction::KillToEnd },
        { "kill-to-start", EditAction::KillToStart },
        { "transpose", EditAction::Transpose },
        // Undo/Redo
        { "undo", EditAction::Undo },
        { "redo", EditAction::Redo },
        // Kill Ring
        { "yank", EditAction::Yank },
        { "yank-pop", EditAction::YankPop },
        // Selection
        { "select-all", EditAction::SelectAll },
        // Clipboard
        { "cut", EditAction::Cut },
        { "copy", EditAction::Copy },
        { "paste", EditAction::Paste },
        // Control
        { "submit", EditAction::Submit },
        { "abort", EditAction::Abort },
        { "insert-newline", EditAction::InsertNewline },
        { "agent-mode", EditAction::AgentMode },
        { "cycle-agent-mode", EditAction::CycleAgentMode },
        { "cycle-thinking-mode", EditAction::CycleThinkingMode },
        { "cycle-model", EditAction::CycleModel },
        // History
        { "history-prev", EditAction::HistoryPrev },
        { "history-next", EditAction::HistoryNext },
    } };
} // namespace

std::optional<KeyChord> KeyChord::parse(std::string_view str)
{
    if (str.empty())
        return std::nullopt;

    KeyChord result;

    // Split by '+' and process each part
    size_t start = 0;
    std::string_view lastPart;

    while (start < str.size())
    {
        size_t end = str.find('+', start);
        if (end == std::string_view::npos)
            end = str.size();

        auto part = str.substr(start, end - start);
        if (part.empty())
            return std::nullopt;

        // Check if this is a modifier
        if (equalsIgnoreCase(part, "ctrl") || equalsIgnoreCase(part, "control"))
        {
            result.modifiers |= Modifier::Ctrl;
        }
        else if (equalsIgnoreCase(part, "alt") || equalsIgnoreCase(part, "meta"))
        {
            result.modifiers |= Modifier::Alt;
        }
        else if (equalsIgnoreCase(part, "shift"))
        {
            result.modifiers |= Modifier::Shift;
        }
        else if (equalsIgnoreCase(part, "super") || equalsIgnoreCase(part, "win")
                 || equalsIgnoreCase(part, "cmd"))
        {
            result.modifiers |= Modifier::Super;
        }
        else
        {
            // Not a modifier - this should be the key
            lastPart = part;
        }

        start = end + 1;
    }

    // Process the key part
    if (lastPart.empty())
        return std::nullopt;

    // Check for special keys
    if (auto specialKey = parseSpecialKey(lastPart))
    {
        result.key = *specialKey;
        result.codepoint = 0;
    }
    // Check for single letter key
    else if (lastPart.size() == 1 && std::isalpha(static_cast<unsigned char>(lastPart[0])))
    {
        result.codepoint = static_cast<char32_t>(std::tolower(static_cast<unsigned char>(lastPart[0])));
        result.key = {};
    }
    else
    {
        // Unknown key
        return std::nullopt;
    }

    return result;
}

std::string KeyChord::toString() const
{
    std::ostringstream oss;

    // Add modifiers
    if (hasModifier(modifiers, Modifier::Ctrl))
        oss << "ctrl+";
    if (hasModifier(modifiers, Modifier::Alt))
        oss << "alt+";
    if (hasModifier(modifiers, Modifier::Shift))
        oss << "shift+";
    if (hasModifier(modifiers, Modifier::Super))
        oss << "super+";

    // Add key
    if (codepoint != 0)
    {
        oss << static_cast<char>(codepoint);
    }
    else
    {
        auto keyName = keyCodeToString(key);
        if (!keyName.empty())
            oss << keyName;
        else
            oss << "unknown";
    }

    return oss.str();
}

bool KeyChord::matches(KeyEvent const& event) const noexcept
{
    // Check modifiers match
    if (modifiers != withoutLockKeys(event.modifiers))
        return false;

    // If we have a codepoint, match against event's codepoint
    if (codepoint != 0)
        return event.codepoint == codepoint;

    // Otherwise, match against the key code
    return key == event.key;
}

std::optional<EditAction> parseEditAction(std::string_view str)
{
    for (auto const& mapping: actionNameMappings)
    {
        if (equalsIgnoreCase(str, mapping.name))
            return mapping.action;
    }
    return std::nullopt;
}

std::string_view editActionToString(EditAction action) noexcept
{
    for (auto const& mapping: actionNameMappings)
    {
        if (mapping.action == action)
            return mapping.name;
    }
    return "none";
}

void KeyBindings::bind(KeyChord chord, EditAction action)
{
    // Check if chord is already bound, replace if so
    for (auto& [existingChord, existingAction]: _bindings)
    {
        if (existingChord == chord)
        {
            existingAction = action;
            return;
        }
    }
    _bindings.emplace_back(chord, action);
}

void KeyBindings::bindMultiple(std::initializer_list<KeyChord> chords, EditAction action)
{
    for (auto const& chord: chords)
        bind(chord, action);
}

void KeyBindings::unbind(KeyChord chord)
{
    std::erase_if(_bindings, [&](auto const& pair) { return pair.first == chord; });
}

std::optional<EditAction> KeyBindings::lookup(KeyEvent const& event) const noexcept
{
    for (auto const& [chord, action]: _bindings)
    {
        if (chord.matches(event))
            return action;
    }
    return std::nullopt;
}

std::span<std::pair<KeyChord, EditAction> const> KeyBindings::bindings() const noexcept
{
    return _bindings;
}

KeyBindings KeyBindings::defaults()
{
    using M = Modifier;
    using A = EditAction;
    using K = KeyChord;

    KeyBindings bindings;

    // === Undo/Redo ===
    bindings.bind(K::fromChar('z', M::Ctrl), A::Undo);
    bindings.bind(K::fromChar('y', M::Ctrl), A::Redo);
    bindings.bind(K::fromChar('z', M::Ctrl | M::Shift), A::Redo);

    // === Clipboard ===
    bindings.bind(K::fromChar('c', M::Ctrl), A::Copy);
    bindings.bind(K::fromChar('x', M::Ctrl), A::Cut);
    bindings.bind(K::fromChar('v', M::Ctrl), A::Paste);

    // === Selection ===
    bindings.bind(K::fromChar('a', M::Ctrl | M::Shift), A::SelectAll);

    // === Movement (letter keys) ===
    bindings.bind(K::fromChar('a', M::Ctrl), A::SmartMoveToLineStart);
    bindings.bind(K::fromChar('f', M::Ctrl), A::MoveForwardChar);
    bindings.bind(K::fromChar('b', M::Ctrl), A::MoveBackwardChar);
    bindings.bind(K::fromChar('f', M::Alt), A::MoveForwardWord);
    bindings.bind(K::fromChar('b', M::Alt), A::MoveBackwardWord);
    bindings.bind(K::fromChar('e', M::Ctrl), A::SmartMoveToLineEnd);
    bindings.bind(K::fromChar('p', M::Ctrl), A::MoveUp);
    bindings.bind(K::fromChar('n', M::Ctrl), A::MoveDown);

    // === Movement (special keys) ===
    bindings.bind(K::fromKey(KeyCode::Left), A::MoveBackwardChar);
    bindings.bind(K::fromKey(KeyCode::Right), A::MoveForwardChar);
    bindings.bind(K::fromKey(KeyCode::Left, M::Ctrl), A::MoveBackwardWord);
    bindings.bind(K::fromKey(KeyCode::Right, M::Ctrl), A::MoveForwardWord);
    bindings.bind(K::fromKey(KeyCode::Up), A::MoveUp);
    bindings.bind(K::fromKey(KeyCode::Down), A::MoveDown);
    bindings.bind(K::fromKey(KeyCode::Home), A::MoveToLineStart);
    bindings.bind(K::fromKey(KeyCode::End), A::MoveToLineEnd);
    bindings.bind(K::fromKey(KeyCode::Home, M::Ctrl), A::MoveToBufferStart);
    bindings.bind(K::fromKey(KeyCode::End, M::Ctrl), A::MoveToBufferEnd);

    // === Editing ===
    bindings.bind(K::fromKey(KeyCode::Backspace), A::DeleteCharBackward);
    bindings.bind(K::fromKey(KeyCode::Delete), A::DeleteCharForward);
    bindings.bind(K::fromKey(KeyCode::Backspace, M::Ctrl), A::DeleteWordBackward);
    bindings.bind(K::fromKey(KeyCode::Backspace, M::Alt), A::DeleteWordBackward);
    bindings.bind(K::fromChar('w', M::Ctrl), A::DeleteWordBackward);
    bindings.bind(K::fromChar('d', M::Alt), A::DeleteWord);
    bindings.bind(K::fromChar('k', M::Ctrl), A::KillToEnd);
    bindings.bind(K::fromChar('u', M::Ctrl), A::KillToStart);
    // Note: Transpose has no default binding, users can reconfigure
    bindings.bind(K::fromChar('t', M::Ctrl), A::AgentMode);
    bindings.bind(K::fromKey(KeyCode::Tab, M::Shift), A::CycleAgentMode);
    bindings.bind(K::fromChar('/', M::Ctrl), A::CycleThinkingMode);
    bindings.bind(K::fromChar('.', M::Ctrl), A::CycleModel);

    // === Kill Ring ===
    // Note: Yank has no default binding (was Ctrl+Y, now Redo)
    // Users can configure: bindings.bind(K::fromChar('y', M::Ctrl), A::Yank);
    bindings.bind(K::fromChar('y', M::Alt), A::YankPop);

    // === Control ===
    bindings.bind(K::fromKey(KeyCode::Enter), A::Submit);
    bindings.bind(K::fromKey(KeyCode::Enter, M::Shift), A::InsertNewline);
    bindings.bind(K::fromKey(KeyCode::Enter, M::Alt), A::InsertNewline);
    // Note: Ctrl+D is handled specially in InputField (EOF if empty, DeleteCharForward otherwise)
    // We bind it to DeleteCharForward here; InputField checks for empty buffer first
    bindings.bind(K::fromChar('d', M::Ctrl), A::DeleteCharForward);

    // === History ===
    // Note: History navigation is contextual (only when cursor at buffer boundaries)
    // These are handled specially in InputField, not through bindings
    // bindings.bind(..., HistoryPrev);
    // bindings.bind(..., HistoryNext);

    return bindings;
}

} // namespace tui
