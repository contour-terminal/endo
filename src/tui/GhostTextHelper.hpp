// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tui/InputField.hpp>

#include <optional>
#include <string>

namespace tui
{

/// Updates ghost text on an input field based on a suggest function.
///
/// Only shows ghost text when the cursor is at the end of the input.
/// Uses a cache to avoid repeated expensive completer calls for the same text.
///
/// @param inputField       The input field to update ghost text on.
/// @param suggestCacheText Cached input text from the last suggest call (updated in-place).
/// @param suggestCacheResult Cached suggestion result (updated in-place).
/// @param suggest          Callable: (string_view text, size_t cursor) -> optional<string>.
template <typename SuggestFn>
void updateGhostText(InputField& inputField,
                     std::string& suggestCacheText,
                     std::optional<std::string>& suggestCacheResult,
                     SuggestFn const& suggest)
{
    auto const text = inputField.text();
    auto const cursor = inputField.cursor();

    // Only show ghost text when cursor is at end of input.
    if (cursor != text.size())
    {
        inputField.clearGhostText();
        return;
    }

    // Check suggest cache — skip expensive completer call if text unchanged.
    if (text == suggestCacheText)
    {
        if (suggestCacheResult)
            inputField.setGhostText(*suggestCacheResult);
        else
            inputField.clearGhostText();
        return;
    }

    // Cache miss — call completer and store result.
    auto suggestion = suggest(text, cursor);
    suggestCacheText = std::string(text);
    suggestCacheResult = suggestion;

    if (suggestion)
        inputField.setGhostText(*suggestion);
    else
        inputField.clearGhostText();
}

} // namespace tui
