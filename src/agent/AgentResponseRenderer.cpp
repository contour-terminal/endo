// SPDX-License-Identifier: Apache-2.0
#include <tui/TerminalOutput.hpp>
#include <tui/Theme.hpp>

#include <agent/AgentResponseRenderer.hpp>

namespace endo::agent
{

AgentResponseRenderer::AgentResponseRenderer(tui::TerminalOutput& output):
    _output(output), _markdownRenderer(output), _spinner(tui::SpinnerType::Dots)
{
}

void AgentResponseRenderer::begin()
{
    _thinking = true;
    _firstToken = true;
    renderSpinner();
    _output.flush();
}

void AgentResponseRenderer::feedToken(std::string_view token)
{
    if (_firstToken)
    {
        _firstToken = false;
        _thinking = false;

        // Clear the spinner line
        _output.writeRaw("\r");
        _output.clearToEndOfLine();

        // Start markdown streaming
        _markdownRenderer.beginStream();
    }

    _markdownRenderer.feedToken(token);
    _output.flush();
}

void AgentResponseRenderer::end()
{
    if (_thinking)
    {
        // No tokens were received — clear spinner
        _output.writeRaw("\r");
        _output.clearToEndOfLine();
        _thinking = false;
    }
    else if (!_firstToken)
    {
        // End the markdown stream
        _markdownRenderer.endStream();
    }

    _output.writeRaw("\n");
    _output.flush();
}

auto AgentResponseRenderer::tickSpinner() -> bool
{
    return _spinner.tick();
}

void AgentResponseRenderer::renderSpinner()
{
    auto const& theme = tui::currentTheme();
    auto const barStyle = tui::Style { .fg = theme.agentColors.leftBar };
    auto const spinnerStyle = tui::Style { .fg = theme.agentColors.spinnerColor };
    auto const labelStyle = tui::Style { .fg = theme.agentColors.statusText };

    _output.writeRaw("\r");
    _output.clearToEndOfLine();
    _output.write("\u2502 ", barStyle);
    _spinner.renderWithLabel(_output, "Thinking...", spinnerStyle, labelStyle);
    _output.flush();
}

} // namespace endo::agent
