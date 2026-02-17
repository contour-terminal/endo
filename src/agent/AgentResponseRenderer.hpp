// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tui/MarkdownRenderer.hpp>
#include <tui/Spinner.hpp>

#include <string_view>

namespace tui
{
class TerminalOutput;
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

  private:
    tui::TerminalOutput& _output;
    tui::MarkdownRenderer _markdownRenderer;
    tui::Spinner _spinner;
    bool _thinking = false;
    bool _firstToken = true;
};

} // namespace endo::agent
