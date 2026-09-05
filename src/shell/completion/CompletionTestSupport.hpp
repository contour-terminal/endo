// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file CompletionTestSupport.hpp
/// @brief Shared helpers for the completion provider tests.
///
/// CompletionContext is a six-field aggregate built positionally; every test TU that
/// hand-rolled its own builder had to be found and updated whenever a field moved. These
/// live here so there is one of each.

#include <endo-language/ide/CompletionContext.hpp>

#include <tui/completer/CompletionItem.hpp>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace endo::test
{

/// @brief Builds an Argument-position completion context.
/// @param fullInput The whole command line up to the cursor.
/// @param prefix    The word being completed.
/// @param command   The command name the line starts with.
/// @return The context a provider would receive.
[[nodiscard]] inline CompletionContext makeArgumentContext(std::string fullInput,
                                                           std::string const& prefix,
                                                           std::string command)
{
    auto const cursor = fullInput.size();
    return CompletionContext {
        .type = CompletionContextType::Argument,
        .prefix = prefix,
        .prefixStart = cursor - prefix.size(),
        .cursorPosition = cursor,
        .command = std::move(command),
        .fullInput = std::move(fullInput),
    };
}

/// @brief Builds an Option-position completion context.
/// @param fullInput The whole command line up to the cursor.
/// @param prefix    The flag being completed (e.g. "-").
/// @param command   The command name the line starts with.
/// @return The context a provider would receive.
[[nodiscard]] inline CompletionContext makeOptionContext(std::string fullInput,
                                                         std::string const& prefix,
                                                         std::string command)
{
    auto const cursor = fullInput.size();
    return CompletionContext {
        .type = CompletionContextType::Option,
        .prefix = prefix,
        .prefixStart = cursor - prefix.size(),
        .cursorPosition = cursor,
        .command = std::move(command),
        .fullInput = std::move(fullInput),
    };
}

/// @brief Tests whether @p items contains a completion with exactly @p text.
[[nodiscard]] inline bool hasCompletion(std::vector<tui::CompletionItem> const& items, std::string_view text)
{
    return std::ranges::any_of(items, [&](auto const& item) { return item.text == text; });
}

} // namespace endo::test
