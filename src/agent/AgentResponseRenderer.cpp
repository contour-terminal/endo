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
    _output.write(
        std::format("\u256D\u2500 plan \u2502 {} steps \u2502 ~{} files\n", plan.steps.size(), fileCount),
        barStyle);

    // Summary
    _output.write("\u2502  ", barStyle);
    _output.write("Summary: ", labelStyle);
    _output.write(plan.summary + "\n", defaultStyle);

    // Steps
    for (auto const& step: plan.steps)
    {
        _output.write("\u2502  ", barStyle);

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
        _output.write(std::format("{}. {} {}\n", step.index + 1, checkbox, step.description), defaultStyle);

        if (!step.filesTouched.empty())
        {
            _output.write("\u2502     ", barStyle);
            auto files = std::string { "Files: " };
            for (auto i = size_t { 0 }; i < step.filesTouched.size(); ++i)
            {
                if (i > 0)
                    files += ", ";
                files += step.filesTouched[i];
            }
            _output.write(files + "\n", labelStyle);
        }
    }

    // Risk assessment
    if (!plan.riskAssessment.empty())
    {
        _output.write("\u2502  ", barStyle);
        _output.write("Risk: ", labelStyle);
        _output.write(plan.riskAssessment + "\n", defaultStyle);
    }

    // Alternatives
    if (!plan.alternatives.empty())
    {
        _output.write("\u2502  ", barStyle);
        _output.write("Alternatives: ", labelStyle);
        _output.write(plan.alternatives[0], defaultStyle);
        for (auto i = size_t { 1 }; i < plan.alternatives.size(); ++i)
            _output.write(std::format(", {}", plan.alternatives[i]), defaultStyle);
        _output.writeRaw("\n");
    }

    // Action bar
    _output.write("\u2502  ", barStyle);
    _output.write("[y]es  [n]o  [r]evise\n", labelStyle);
    _output.write("\u2570\u2500\n", barStyle);
    _output.flush();
}

void AgentResponseRenderer::renderPlanProgress(Plan const& plan, size_t currentStep)
{
    auto const& theme = tui::currentTheme();
    auto const barStyle = tui::Style { .fg = theme.agentColors.leftBar };
    auto const labelStyle = tui::Style { .fg = theme.agentColors.statusText };
    auto const defaultStyle = tui::Style {};
    auto const spinnerStyle = tui::Style { .fg = theme.agentColors.spinnerColor };

    _output.write(
        std::format("\u256D\u2500 progress \u2502 step {}/{}\n", currentStep + 1, plan.steps.size()),
        barStyle);

    for (auto const& step: plan.steps)
    {
        _output.write("\u2502  ", barStyle);

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
        _output.write(std::format("[{}] {}. {}\n", indicator, step.index + 1, step.description), style);
    }

    _output.write("\u2570\u2500\n", barStyle);
    _output.flush();
}

} // namespace endo::agent
