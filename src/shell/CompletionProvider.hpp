// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace endo
{

/// @brief A single completion suggestion.
struct CompletionItem
{
    std::string text;        ///< Full completion text (the actual value to insert).
    std::string displayText; ///< Text to display in menu (may be abbreviated).
    std::string description; ///< Help text / synopsis for the completion menu.
    int score = 0;           ///< Ranking score (higher = better match).

    /// @brief Returns the suffix to append after the prefix.
    /// @param prefixLen Length of the prefix already typed.
    [[nodiscard]] std::string suffix(size_t prefixLen) const
    {
        return prefixLen < text.size() ? text.substr(prefixLen) : "";
    }

    auto operator<=>(CompletionItem const&) const = default;
};

/// @brief Type of completion context.
enum class CompletionContextType
{
    Command,       ///< First token position - complete executables/builtins.
    Argument,      ///< General argument position.
    FilePath,      ///< Path argument (starts with /, ./, ~).
    Variable,      ///< After $ (variable expansion).
    VariableBrace, ///< Inside ${...} (brace variable expansion).
    Redirect,      ///< After < or > (file target).
    Option,        ///< After - or -- (command option).
    Unknown        ///< Unable to determine context.
};

/// @brief Context information for completion.
struct CompletionContext
{
    CompletionContextType type = CompletionContextType::Unknown;
    std::string prefix;                 ///< Word being completed (may be empty).
    size_t prefixStart = 0;             ///< Byte offset of prefix in input.
    size_t cursorPosition = 0;          ///< Cursor byte offset in input.
    std::optional<std::string> command; ///< Current command (for option context).
    std::string fullInput;              ///< Complete input line.
};

/// @brief Abstract base for completion providers.
///
/// Completion providers generate suggestions for a specific type of context.
/// Multiple providers can be registered with the Completer orchestrator.
class CompletionProvider
{
  public:
    virtual ~CompletionProvider() = default;

    /// @brief Generates completions for the given context.
    /// @param context The completion context.
    /// @return List of completion items (may be empty).
    [[nodiscard]] virtual std::vector<CompletionItem> complete(CompletionContext const& context) = 0;

    /// @brief Checks if this provider can handle the given context type.
    /// @param type The context type.
    /// @return true if this provider handles the context type.
    [[nodiscard]] virtual bool canHandle(CompletionContextType type) const = 0;

    /// @brief Returns the priority of this provider (higher = checked first).
    /// @return Priority value (default: 0).
    [[nodiscard]] virtual int priority() const { return 0; }
};

} // namespace endo
