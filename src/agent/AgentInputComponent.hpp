// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tui/Component.hpp>
#include <tui/InputField.hpp>

#include <string>

namespace endo::agent
{

/// Styled input component for agent mode queries.
///
/// Renders a purple left bar to distinguish from the shell prompt,
/// and wraps a tui::InputField for text editing. Supports Submit (Enter)
/// and Abort (Escape) actions.
class AgentInputComponent: public tui::Component
{
  public:
    /// @brief Result of processing input.
    enum class Action
    {
        None,    ///< No action needed.
        Changed, ///< Content changed, re-render needed.
        Submit,  ///< User pressed Enter on non-empty input.
        Abort,   ///< User pressed Escape to exit agent mode.
    };

    AgentInputComponent();
    ~AgentInputComponent() override = default;

    // --- Component Interface ---

    void render(tui::Canvas& canvas) override;
    [[nodiscard]] tui::EventResult onEvent(tui::InputEvent const& event) override;

    [[nodiscard]] bool focusable() const override { return true; }

    [[nodiscard]] tui::CursorShape cursorShape() const override { return tui::CursorShape::SteadyBar; }

    [[nodiscard]] tui::Size preferredSize() const override;

    // --- Agent Input API ---

    /// @brief Processes an input event and returns the action.
    /// @param event The input event to process.
    /// @return The resulting action.
    [[nodiscard]] Action processInput(tui::InputEvent const& event);

    /// @brief Returns the current input text.
    [[nodiscard]] auto text() const noexcept -> std::string_view { return _inputField.text(); }

    /// @brief Clears the input field.
    void clear() { _inputField.clear(); }

    /// @brief Returns the InputField for direct access.
    [[nodiscard]] auto inputField() noexcept -> tui::InputField& { return _inputField; }

    [[nodiscard]] auto inputField() const noexcept -> tui::InputField const& { return _inputField; }

  private:
    tui::InputField _inputField;

    static constexpr int LeftBarWidth = 1; ///< Width of the purple left bar.
    static constexpr int BarPadding = 1;   ///< Padding after the bar.
    static constexpr int PromptWidth = 2;  ///< Width of "# " prompt prefix.
};

} // namespace endo::agent
