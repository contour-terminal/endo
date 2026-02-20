// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tui/MarkdownRenderer.hpp>
#include <tui/Spinner.hpp>

#include <cstddef>
#include <functional>
#include <string_view>

namespace tui
{
class TerminalOutput;
}

namespace endo::agent
{
struct Plan;
}

namespace endo::agent
{

/// Renders an agent response directly to the terminal.
///
/// This is NOT a Component — streaming content doesn't fit the Component model.
/// Instead, it writes directly to TerminalOutput for immediate feedback.
///
/// Usage flow:
/// 1. Call begin() to show the "Thinking..." spinner
/// 2. Call feedToken() for each streamed token (first token clears spinner)
/// 3. Call end() to finalize rendering
/// 4. Call tickSpinner() periodically during the thinking phase
class AgentResponseRenderer
{
  public:
    /// @brief Constructs a renderer targeting the given terminal output.
    /// @param output The terminal output to write to.
    explicit AgentResponseRenderer(tui::TerminalOutput& output);

    /// @brief Begins the response, showing a thinking spinner.
    void begin();

    /// @brief Feeds a streamed token for rendering.
    ///
    /// On the first token, clears the spinner and starts markdown streaming.
    /// @param token The text token to render.
    void feedToken(std::string_view token);

    /// @brief Ends the response, flushing any remaining content.
    void end();

    /// @brief Advances the spinner animation.
    /// @return true if the spinner frame changed (needs redraw).
    auto tickSpinner() -> bool;

    /// @brief Renders the current spinner state.
    void renderSpinner();

    /// @brief Returns whether the response is still in the thinking phase.
    [[nodiscard]] auto isThinking() const noexcept -> bool { return _thinking; }

    /// Callback invoked when the response line count changes.
    using LineCallback = std::function<void(int lineCount)>;

    /// @brief Sets a callback invoked whenever the response emits a new line.
    /// @param cb The callback receiving the current total line count.
    void setLineCallback(LineCallback cb);

    /// @brief Returns the current number of output lines emitted by the response.
    [[nodiscard]] auto lineCount() const noexcept -> int { return _lineCount; }

    /// @brief Renders a plan for user review with chrome and action hints.
    /// @param plan The plan to render.
    void renderPlan(Plan const& plan);

    /// @brief Renders plan execution progress with step status indicators.
    /// @param plan The plan being executed.
    /// @param currentStep The index of the step currently being executed.
    void renderPlanProgress(Plan const& plan, size_t currentStep);

  private:
    tui::TerminalOutput& _output;
    tui::MarkdownRenderer _markdownRenderer;
    tui::Spinner _spinner;
    bool _thinking = false;
    bool _firstToken = true;
    int _lineCount = 1;         ///< Number of output lines (starts at 1 for spinner/first line).
    LineCallback _lineCallback; ///< Optional callback for line count changes.
};

} // namespace endo::agent
