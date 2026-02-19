// SPDX-License-Identifier: Apache-2.0
#include <tui/TerminalOutput.hpp>
#include <tui/Theme.hpp>

#include <format>

#include <agent/AgentResponseRenderer.hpp>
#include <agent/Plan.hpp>

namespace endo::agent
{

AgentResponseRenderer::AgentResponseRenderer(tui::TerminalOutput& output):
    _output(output), _markdownRenderer(output), _spinner(tui::SpinnerType::Dots)
{
    _markdownRenderer.setFullWidthMode(true);
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
        _output.carriageReturn();
        _output.clearToEndOfLine();

        // Start markdown streaming
        _markdownRenderer.beginStream();
    }

    // Count newlines in the token to track response height.
    auto const previousLineCount = _lineCount;
    for (auto const ch: token)
    {
        if (ch == '\n')
            ++_lineCount;
    }

    _markdownRenderer.feedToken(token);
    _output.flush();

    if (_lineCount != previousLineCount && _lineCallback)
        _lineCallback(_lineCount);
}

void AgentResponseRenderer::setLineCallback(LineCallback cb)
{
    _lineCallback = std::move(cb);
}

void AgentResponseRenderer::end()
{
    if (_thinking)
    {
        // No tokens were received — clear spinner
        _output.carriageReturn();
        _output.clearToEndOfLine();
        _thinking = false;
    }
    else if (!_firstToken)
    {
        // End the markdown stream
        _markdownRenderer.endStream();
    }

    _output.linefeed();
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

    _output.carriageReturn();
    _output.clearToEndOfLine();
    _output.writeText("\u2502 ", barStyle);
    _spinner.renderWithLabel(_output, "Thinking...", spinnerStyle, labelStyle);
    _output.flush();
}

void AgentResponseRenderer::renderPlan(Plan const& plan)
{
    auto const& theme = tui::currentTheme();
    auto const barStyle = tui::Style { .fg = theme.agentColors.leftBar };
    auto const labelStyle = tui::Style { .fg = theme.agentColors.statusText };
    auto const defaultStyle = tui::Style {};

    // Count unique files across all steps
    auto fileCount = size_t { 0 };
    for (auto const& step: plan.steps)
        fileCount += step.filesTouched.size();

    // Header
    _output.writeText(
        std::format("\u256D\u2500 plan \u2502 {} steps \u2502 ~{} files\n", plan.steps.size(), fileCount),
        barStyle);

    // Summary
    _output.writeText("\u2502  ", barStyle);
    _output.writeText("Summary: ", labelStyle);
    _output.writeText(plan.summary + "\n", defaultStyle);

    // Steps
    for (auto const& step: plan.steps)
    {
        _output.writeText("\u2502  ", barStyle);

        auto const* const checkbox = [&]() -> char const* {
            switch (step.status)
            {
                case PlanStepStatus::Completed: return "[\xe2\x9c\x93]";
                case PlanStepStatus::InProgress: return "[\xe2\x80\xa6]";
                case PlanStepStatus::Failed: return "[\xe2\x9c\x97]";
                case PlanStepStatus::Skipped: return "[-]";
                default: return "[ ]";
            }
        }();
        _output.writeText(std::format("{}. {} {}\n", step.index + 1, checkbox, step.description),
                          defaultStyle);

        if (!step.filesTouched.empty())
        {
            _output.writeText("\u2502     ", barStyle);
            auto files = std::string { "Files: " };
            for (auto i = size_t { 0 }; i < step.filesTouched.size(); ++i)
            {
                if (i > 0)
                    files += ", ";
                files += step.filesTouched[i];
            }
            _output.writeText(files + "\n", labelStyle);
        }
    }

    // Risk assessment
    if (!plan.riskAssessment.empty())
    {
        _output.writeText("\u2502  ", barStyle);
        _output.writeText("Risk: ", labelStyle);
        _output.writeText(plan.riskAssessment + "\n", defaultStyle);
    }

    // Alternatives
    if (!plan.alternatives.empty())
    {
        _output.writeText("\u2502  ", barStyle);
        _output.writeText("Alternatives: ", labelStyle);
        _output.writeText(plan.alternatives[0], defaultStyle);
        for (auto i = size_t { 1 }; i < plan.alternatives.size(); ++i)
            _output.writeText(std::format(", {}", plan.alternatives[i]), defaultStyle);
        _output.writeRaw("\n");
    }

    // Action bar
    _output.writeText("\u2502  ", barStyle);
    _output.writeText("[y]es  [n]o  [r]evise\n", labelStyle);
    _output.writeText("\u2570\u2500\n", barStyle);
    _output.flush();
}

void AgentResponseRenderer::renderPlanProgress(Plan const& plan, size_t currentStep)
{
    auto const& theme = tui::currentTheme();
    auto const barStyle = tui::Style { .fg = theme.agentColors.leftBar };
    auto const labelStyle = tui::Style { .fg = theme.agentColors.statusText };
    auto const defaultStyle = tui::Style {};
    auto const spinnerStyle = tui::Style { .fg = theme.agentColors.spinnerColor };

    _output.writeText(
        std::format("\u256D\u2500 progress \u2502 step {}/{}\n", currentStep + 1, plan.steps.size()),
        barStyle);

    for (auto const& step: plan.steps)
    {
        _output.writeText("\u2502  ", barStyle);

        auto const& style = step.index == currentStep ? spinnerStyle : defaultStyle;
        auto const* const indicator = [&]() -> char const* {
            switch (step.status)
            {
                case PlanStepStatus::Completed: return "\xe2\x9c\x93";
                case PlanStepStatus::InProgress: return "\xe2\x80\xa6";
                case PlanStepStatus::Failed: return "\xe2\x9c\x97";
                case PlanStepStatus::Skipped: return "-";
                default: return " ";
            }
        }();
        _output.writeText(std::format("[{}] {}. {}\n", indicator, step.index + 1, step.description), style);
    }

    _output.writeText("\u2570\u2500\n", barStyle);
    _output.flush();
}

} // namespace endo::agent
