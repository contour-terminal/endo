// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tui/Component.hpp>
#include <tui/InputField.hpp>

#include <string>

namespace endo::agent
{

/// Styled input component for agent mode queries.
///
/// Renders a rounded-chrome header line with agent mode label, provider, and model info,
/// followed by the input area with a configurable prompt indicator. Uses the agent color
/// palette (purple accent) to visually distinguish from the shell prompt.
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

    /// @brief Sets the prompt indicator displayed before user input.
    /// @param indicator The indicator string (e.g., "❯").
    void setPromptIndicator(std::string indicator);

    /// @brief Sets the provider name displayed in the header line.
    /// @param name The provider name (e.g., "claude", "openai").
    void setProviderName(std::string name) { _providerName = std::move(name); }

    /// @brief Sets the model name displayed in the header line.
    /// @param name The model identifier (e.g., "claude-sonnet-4-5-20250929").
    void setModelName(std::string name) { _modelName = std::move(name); }

    /// @brief Sets the git branch name displayed in the header line.
    /// @param branch The branch name (e.g., "main", "feature/xyz").
    void setGitBranch(std::string branch) { _gitBranch = std::move(branch); }

    /// @brief Returns the current input text.
    [[nodiscard]] auto text() const noexcept -> std::string_view { return _inputField.text(); }

    /// @brief Clears the input field.
    void clear() { _inputField.clear(); }

    /// @brief Returns the InputField for direct access.
    [[nodiscard]] auto inputField() noexcept -> tui::InputField& { return _inputField; }

    [[nodiscard]] auto inputField() const noexcept -> tui::InputField const& { return _inputField; }

  private:
    tui::InputField _inputField;
    std::string _providerName; ///< Active provider name for header display.
    std::string _modelName;    ///< Active model name for header display.
    std::string _gitBranch;    ///< Current git branch for header display.

    static constexpr int LeftBarWidth = 2; ///< Width of the left bar chrome (╭─, ╰─, │).
    static constexpr int BarPadding = 1;   ///< Padding after the bar.
    static constexpr int HeaderHeight = 1; ///< Height of the header line (shows agent/provider/model).
};

} // namespace endo::agent
