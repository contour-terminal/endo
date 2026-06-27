// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tui/CommandRegistry.hpp>
#include <tui/Component.hpp>
#include <tui/InputEvent.hpp>
#include <tui/InputField.hpp>
#include <tui/ScrollableSelection.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace tui
{

/// @brief Result of processing an input event in CommandPalettePopup.
enum class CommandPaletteAction : std::uint8_t
{
    Changed,   ///< Filter or selection changed, re-render needed.
    Executed,  ///< User executed the selected command.
    Dismissed, ///< User dismissed the palette (Escape).
};

/// @brief A popup command palette with fuzzy search filtering.
///
/// Renders a bordered popup with a filter input field at the top and a
/// scrollable list of commands below. Commands are fuzzy-matched against
/// the filter text using FuzzyMatch::matchSmartCase(). Each item shows
/// category, label (with match highlighting), description, and keybinding hint.
///
/// Follows the CompletionPopup pattern: manages its own visibility state,
/// handles keyboard navigation, and renders using the Canvas API.
class CommandPalettePopup: public Component
{
  public:
    CommandPalettePopup() = default;
    ~CommandPalettePopup() override = default;

    // --- Component Interface ---

    /// @brief Renders the palette to the canvas.
    void render(Canvas& canvas) override;

    /// @brief Handles input events via Component interface.
    [[nodiscard]] EventResult onEvent(InputEvent const& event) override;

    /// @brief CommandPalettePopup can receive keyboard focus.
    [[nodiscard]] bool focusable() const override { return true; }

    /// @brief Returns preferred size based on content.
    [[nodiscard]] Size preferredSize() const override;

    // ========================================================================
    // Visibility and Items
    // ========================================================================

    /// @brief Shows the palette with commands from the given registry and context.
    /// @param registry The command registry to display.
    /// @param context The current mode context for filtering.
    void show(CommandRegistry const& registry, CommandContext context);

    /// @brief Shows a sub-menu with a custom set of commands and title.
    /// @param items The commands to display.
    /// @param title Title shown in the filter field prompt.
    void showSubMenu(std::vector<CommandEntry> items, std::string_view title);

    /// @brief Hides the palette and clears state.
    void hide();

    /// @brief Returns whether the palette is currently visible.
    [[nodiscard]] bool visible() const noexcept override;

    // ========================================================================
    // Event Handling
    // ========================================================================

    /// @brief Processes an input event and returns the resulting action.
    /// @param event The input event to process.
    /// @return The resulting action.
    [[nodiscard]] CommandPaletteAction processEvent(InputEvent const& event);

    /// @brief Returns the rendered height (including borders).
    [[nodiscard]] int renderedHeight() const noexcept;

    /// @brief Returns the rendered width (including borders).
    [[nodiscard]] int renderedWidth() const noexcept;

  private:
    /// @brief A filtered command item with fuzzy match positions for highlighting.
    struct FilteredItem
    {
        CommandEntry const* entry = nullptr; ///< Pointer to the original command entry.
        std::vector<size_t> matchPositions;  ///< Grapheme indices for label highlighting.
        int score = 0;                       ///< Fuzzy match score for sorting.
    };

    InputField _filterField;                            ///< Text input for filtering.
    std::vector<CommandEntry> _allItems;                ///< All available commands (snapshot).
    std::vector<FilteredItem> _filteredItems;           ///< Filtered and sorted commands.
    ScrollableSelection _selection { MaxVisibleItems }; ///< Selection and scroll state.
    bool _visible = false;                              ///< Whether the palette is shown.
    int _renderedHeight = 0;                            ///< Last rendered height.
    int _renderedWidth = 0;                             ///< Last rendered width.
    std::string _title;                                 ///< Optional title for sub-menus.

    static constexpr size_t MaxVisibleItems = 12; ///< Maximum number of visible items.
    static constexpr int MinPaletteWidth = 50;    ///< Minimum palette width.
    static constexpr int MaxPaletteWidth = 80;    ///< Maximum palette width.

    /// @brief Recomputes _filteredItems from _allItems and current filter text.
    void refilter();

    /// @brief Calculates the width needed for the palette.
    [[nodiscard]] int calculateWidth(int maxWidth) const;

    /// @brief Handles a key event for navigation and actions.
    [[nodiscard]] CommandPaletteAction handleKey(KeyEvent const& key);

    /// @brief Executes the currently selected command.
    void executeSelected();
};

} // namespace tui
