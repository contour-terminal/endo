// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tui/Component.hpp>
#include <tui/InputField.hpp>
#include <tui/List.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace tui
{

/// @brief Result of processing an input event in QuestionComponent.
enum class QuestionAction : std::uint8_t
{
    None,      ///< No action taken.
    Changed,   ///< Content changed, re-render needed.
    Confirmed, ///< User confirmed their answer (Enter pressed).
    Cancelled, ///< User cancelled (Escape pressed).
};

/// @brief Configuration for creating a QuestionComponent.
struct QuestionConfig
{
    std::string questionText;         ///< The question to display.
    std::vector<std::string> options; ///< Choices (empty for free-text-only mode).
    bool multiSelect = false;         ///< Single-select vs multi-select.
    bool allowOther = true;           ///< Show "Other..." for free-text fallback.
};

/// @brief Inline question component for asking the user a question.
///
/// Supports three modes:
/// 1. **Free-text only** (empty options): Question text + InputField.
/// 2. **Single-select** (options, multiSelect=false): Question text + List with cursor.
///    If allowOther, last item is "Other..." which transitions to InputField.
/// 3. **Multi-select** (options, multiSelect=true): Question text + List with checkboxes.
///    If allowOther, toggling "Other..." shows/hides an InputField below the list.
///
/// Renders with left-bar chrome (╭─/│/╰─) matching AgentInputComponent style.
class QuestionComponent: public Component
{
  public:
    /// @brief Constructs a question component with the given configuration.
    /// @param config The question configuration.
    explicit QuestionComponent(QuestionConfig config);
    ~QuestionComponent() override = default;

    // --- Component Interface ---

    /// @brief Renders the question component to the canvas.
    void render(Canvas& canvas) override;

    /// @brief Handles input events via Component interface.
    [[nodiscard]] EventResult onEvent(InputEvent const& event) override;

    /// @brief QuestionComponent can receive keyboard focus.
    [[nodiscard]] bool focusable() const override { return true; }

    /// @brief Returns cursor shape based on current mode.
    [[nodiscard]] CursorShape cursorShape() const override;

    /// @brief Returns preferred size based on content.
    [[nodiscard]] Size preferredSize() const override;

    // --- Question API ---

    /// @brief Processes an input event and returns the resulting action.
    /// @param event The input event to process.
    /// @return The action resulting from the event.
    [[nodiscard]] auto processInput(InputEvent const& event) -> QuestionAction;

    /// @brief Returns the user's answer as a string.
    ///
    /// - Free-text: the typed text.
    /// - Single-select (normal): the selected item label.
    /// - Single-select (Other active): the typed text.
    /// - Multi-select: comma-joined labels of checked items (including typed text for Other).
    [[nodiscard]] auto answer() const -> std::string;

    /// @brief Returns the question configuration.
    [[nodiscard]] auto config() const noexcept -> QuestionConfig const& { return _config; }

    /// @brief Returns the selected option index (single-select mode).
    [[nodiscard]] auto selectedIndex() const -> std::size_t { return _list.selectedIndex(); }

    /// @brief Returns the indices of checked options (multi-select mode).
    [[nodiscard]] auto checkedIndices() const -> std::vector<std::size_t> { return _list.checkedIndices(); }

    /// @brief Returns whether the "Other..." free-text input is active.
    [[nodiscard]] bool isOtherActive() const noexcept { return _otherActive; }

  private:
    QuestionConfig _config;
    List _list;
    InputField _inputField;
    bool _otherActive = false;  ///< Whether the "Other..." free-text input is shown.
    bool _inputFocused = false; ///< Whether the InputField has focus (vs the List).

    static constexpr int LeftBarWidth = 2; ///< Width of left bar chrome (╭─, ╰─, │).
    static constexpr int BarPadding = 1;   ///< Padding after the bar.
    static constexpr int HeaderHeight = 1; ///< Height of the header line.

    /// @brief Returns true if operating in free-text-only mode.
    [[nodiscard]] bool isFreeTextOnly() const noexcept { return _config.options.empty(); }

    /// @brief Returns the index of the "Other..." item, or -1 if not present.
    [[nodiscard]] int otherItemIndex() const noexcept;

    /// @brief Returns the number of content rows (list items + optional input field).
    [[nodiscard]] int contentHeight() const noexcept;
};

} // namespace tui
