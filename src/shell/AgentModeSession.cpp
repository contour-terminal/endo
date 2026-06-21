// SPDX-License-Identifier: Apache-2.0
#include <shell/AgentModeSession.hpp>
#include <shell/Shell.hpp>

#include <tui/Buffer.hpp>
#include <tui/Canvas.hpp>
#include <tui/Screen.hpp>
#include <tui/Theme.hpp>

#include <agent/AgentConfig.hpp>
#include <agent/providers/ProviderFactory.hpp>
#include <agent/providers/ProviderModels.hpp>
#include <agent/session/AgentSession.hpp>
#include <agent/tools/DiffRenderer.hpp>
#include <agent/tracing/TraceTerminalRenderer.hpp>

namespace endo
{

void InlinePrompt::clear(tui::TerminalOutput& output)
{
    if (!visible)
        return;
    output.hideCursor();
    output.restoreCursor();
    output.clearToEndOfDisplay();
    output.flush();
    visible = false;
}

void InlinePrompt::render(tui::TerminalOutput& output, tui::Terminal const& terminal)
{
    if (!active || !component)
        return;
    auto const& theme = tui::currentTheme();
    auto const prefSize = component->preferredSize();
    auto const width = terminal.columns();
    auto const height = prefSize.height;

    for (auto i = 0; i < height; ++i)
        output.linefeed();
    output.moveUp(height);
    output.saveCursor();
    output.linefeed();

    auto buffer = tui::Buffer(height, width);
    auto canvas = tui::Canvas(buffer, tui::Rect { .x = 0, .y = 0, .width = width, .height = height }, theme);
    component->setArea(tui::Rect { .x = 0, .y = 0, .width = width, .height = height });
    component->setScreenBounds(tui::Rect { .x = 0, .y = 0, .width = width, .height = height });
    component->render(canvas);
    buffer.writeTo(output);

    if (component->cursorShape() == tui::CursorShape::SteadyBar)
        output.showCursor();
    else
        output.hideCursor();
    output.flush();
    visible = true;
}

void InlinePrompt::reset()
{
    active = false;
    component.reset();
    visible = false;
    requestId = 0;
}

auto InlinePrompt::isActive() const -> bool
{
    return active && component.has_value();
}

bool AgentModeSession::anyPromptActive() const noexcept
{
    return askUserPrompt.active || permissionPrompt.active || sessionPickerPrompt.active
           || planApprovalPrompt.active;
}

void AgentModeSession::teardownStreaming()
{
    streaming = false;
    streamCancelled = false;
    streamingPromptVisible = false;
    currentRenderer.reset();
    activeRenderer = nullptr;
    _inputComponent.setThinkingActive(false);
}

void AgentModeSession::renderComponentDirect()
{
    auto const& theme = tui::currentTheme();
    auto const prefSize = _inputComponent.preferredSize();
    auto const width = _terminal.columns();
    auto const height = prefSize.height;

    auto buffer = tui::Buffer(height, width);
    auto canvas = tui::Canvas(buffer, tui::Rect { .x = 0, .y = 0, .width = width, .height = height }, theme);
    _inputComponent.setArea(tui::Rect { .x = 0, .y = 0, .width = width, .height = height });
    _inputComponent.setScreenBounds(tui::Rect { .x = 0, .y = 0, .width = width, .height = height });
    _inputComponent.render(canvas);
    buffer.writeTo(_out);
}

void AgentModeSession::renderToolStatusDirect()
{
    if (!_toolStatusComponent.hasEntries())
        return;
    auto const& theme = tui::currentTheme();
    auto const prefSize = _toolStatusComponent.preferredSize();
    auto const width = _terminal.columns();
    auto const height = prefSize.height;
    if (height <= 0)
        return;

    auto buffer = tui::Buffer(height, width);
    auto canvas = tui::Canvas(buffer, tui::Rect { .x = 0, .y = 0, .width = width, .height = height }, theme);
    _toolStatusComponent.setArea(tui::Rect { .x = 0, .y = 0, .width = width, .height = height });
    _toolStatusComponent.setScreenBounds(tui::Rect { .x = 0, .y = 0, .width = width, .height = height });
    _toolStatusComponent.render(canvas);
    buffer.writeTo(_out);
}

void AgentModeSession::clearStreamingPrompt()
{
    if (!streamingPromptVisible)
        return;
    _out.hideCursor();
    _out.restoreCursor();
    _out.clearToEndOfDisplay();
    _out.flush();
    streamingPromptVisible = false;
}

void AgentModeSession::renderStreamingPrompt()
{
    if (!streaming || anyPromptActive())
        return;
    // Pre-scroll: emit linefeeds matching the prompt height.
    // This forces any terminal scrolling BEFORE saveCursor, keeping the saved position valid.
    auto const promptHeight = _inputComponent.preferredSize().height;
    for (auto i = 0; i < promptHeight; ++i)
        _out.linefeed();
    _out.moveUp(promptHeight);
    _out.saveCursor();
    _out.linefeed();
    renderComponentDirect();
    _out.flush();
    streamingPromptVisible = true;
}

void AgentModeSession::drainAgentMessages(std::vector<agent::FromAgentMessage>& agentMessages,
                                          agent::ModelInfo const& modelInfo,
                                          std::function<void()> const& saveHistory)
{
    // Aliases map the moved loop body's bare collaborator names onto the session's
    // borrowed references, so the body below is otherwise verbatim.
    auto& out = _out;
    auto& terminal = _terminal;
    auto& screen = _screen;
    auto& inputComponent = _inputComponent;
    auto& toolStatusComponent = _toolStatusComponent;
    auto const& theme = tui::currentTheme();

    for (auto& agentMsg: agentMessages)
    {
        std::visit(
            [&](auto& m) {
                using T = std::decay_t<decltype(m)>;
                if constexpr (std::is_same_v<T, agent::ThinkingStartMessage>)
                {
                    clearStreamingPrompt();
                    if (currentRenderer)
                        currentRenderer->end();
                    streaming = true;
                    streamCancelled = false;
                    currentRenderer.emplace(out);
                    activeRenderer = &*currentRenderer;
                    currentRenderer->begin();
                    inputComponent.setThinkingActive(true);
                    inputComponent.setActivityLabel("Thinking...");
                }
                else if constexpr (std::is_same_v<T, agent::TokenMessage>)
                {
                    clearStreamingPrompt();
                    if (currentRenderer)
                        currentRenderer->feedToken(m.token);
                }
                else if constexpr (std::is_same_v<T, agent::ToolStatusMessage>)
                {
                    clearStreamingPrompt();
                    inputComponent.setActivityLabel("Running " + m.call.name + "...");
                    // Skip ask_user — the QuestionComponent renders the question text.
                    if (_shell.agentConfig.logToolUses && m.call.name != "ask_user")
                    {
                        toolStatusComponent.toolStarted(m.call);
                        renderToolStatusDirect();

                        // Render inline diff preview for edit_file.
                        if (m.call.name == "edit_file" && m.call.arguments.contains("old_string")
                            && m.call.arguments.contains("new_string"))
                        {
                            auto const oldStr = m.call.arguments["old_string"].template get<std::string>();
                            auto const newStr = m.call.arguments["new_string"].template get<std::string>();
                            auto const filePath = m.call.arguments.value("path", std::string { "file" });

                            auto diffLines = agent::generateUnifiedDiff(oldStr, newStr);
                            auto const changedLines =
                                static_cast<int>(std::ranges::count_if(diffLines, [](auto const& l) {
                                    return l.type == agent::DiffLineType::Addition
                                           || l.type == agent::DiffLineType::Deletion;
                                }));
                            auto const truncated = changedLines > agent::LargeEditThreshold;
                            auto const language = tui::detectLanguageFromPath(filePath);
                            agent::renderDiff(out, filePath, diffLines, language, truncated);
                        }

                        if (activeRenderer && activeRenderer->isThinking())
                            activeRenderer->renderSpinner();
                        out.flush();
                    }
                }
                else if constexpr (std::is_same_v<T, agent::ToolResultMessage>)
                {
                    toolStatusComponent.toolCompleted(m);
                    if (_shell.agentConfig.logToolUses)
                    {
                        renderToolStatusDirect();
                        out.flush();
                    }
                }
                else if constexpr (std::is_same_v<T, agent::CompletionMessage>)
                {
                    clearStreamingPrompt();
                    if (currentRenderer)
                        currentRenderer->end();

                    // Render error/cancel text while streaming state is still valid.
                    auto const wasCancelled = streamCancelled;
                    if (wasCancelled)
                    {
                        auto const infoStyle = tui::Style { .fg = theme.agentColors.statusText };
                        out.writeText("\n(Operation cancelled by user)\n", infoStyle);
                        out.flush();
                    }
                    else if (!m.success)
                    {
                        auto const errorStyle = tui::Style { .fg = theme.agentColors.errorText };
                        out.writeText("\nError: " + m.errorMessage + "\n", errorStyle);
                        out.flush();
                    }

                    // Display token usage for this turn.
                    if (m.success && m.turnUsage.has_value())
                    {
                        auto const& tu = *m.turnUsage;
                        auto const dimStyle = tui::Style { .fg = theme.agentColors.statusText };
                        auto const cost =
                            agent::estimateCost(tu, modelInfo.providerName, modelInfo.modelName);
                        auto usageLine = std::format("\n  {} in / {} out",
                                                     agent::formatTokenCount(tu.inputTokens),
                                                     agent::formatTokenCount(tu.outputTokens));
                        if (tu.cacheReadTokens > 0)
                            usageLine +=
                                std::format(" ({} cached)", agent::formatTokenCount(tu.cacheReadTokens));
                        if (tu.cacheCreationTokens > 0)
                            usageLine += std::format(" ({} cache-write)",
                                                     agent::formatTokenCount(tu.cacheCreationTokens));
                        if (cost > 0.0)
                            usageLine += std::format(" ~${:.4f}", cost);
                        usageLine += "\n";
                        out.writeText(usageLine, dimStyle);
                        out.flush();
                    }

                    // Clean up any active ask-user or permission prompt.
                    if (askUserPrompt.active)
                    {
                        askUserPrompt.clear(out);
                        askUserPrompt.reset();
                    }
                    if (permissionPrompt.active)
                    {
                        permissionPrompt.clear(out);
                        permissionPrompt.reset();
                    }

                    teardownStreaming();
                    toolStatusComponent.clear();
                    inputComponent.setThinkingActive(false);
                    saveHistory();

                    if (pendingPlan.has_value())
                    {
                        // Show plan approval prompt instead of returning to input.
                        auto const usedTokens = _shell._agentSession->history().estimatedTokenCount();
                        auto const contextSize = modelInfo.contextSize;
                        auto const usagePct =
                            contextSize > 0 ? (usedTokens * 100 / contextSize) : size_t { 0 };
                        planApprovalPrompt.component.emplace(tui::QuestionConfig {
                            .questionText = std::format("Execute this plan? (context: {}% used)", usagePct),
                            .options = { "Yes, execute",
                                         "Yes, compact context first",
                                         "No, discard",
                                         "Revise" },
                            .multiSelect = false,
                            .allowOther = false,
                        });
                        planApprovalPrompt.active = true;
                        planApprovalPrompt.render(out, terminal);
                    }
                    else
                    {
                        // Re-render input component for next query.
                        screen.releaseCursor();
                        auto const newPrefSize = inputComponent.preferredSize();
                        inputComponent.setArea(tui::Rect {
                            .x = 0, .y = 0, .width = terminal.columns(), .height = newPrefSize.height });
                        screen.draw();
                    }
                }
                else if constexpr (std::is_same_v<T, agent::AskUserRequest>)
                {
                    clearStreamingPrompt();
                    askUserPrompt.component.emplace(tui::QuestionConfig {
                        .questionText = m.question.text,
                        .options = m.question.options,
                        .multiSelect = m.question.multiSelect,
                        .allowOther = m.question.allowOther,
                    });
                    askUserPrompt.requestId = m.requestId;
                    askUserPrompt.active = true;
                    askUserPrompt.render(out, terminal);
                }
                else if constexpr (std::is_same_v<T, agent::PermissionRequest>)
                {
                    clearStreamingPrompt();
                    auto questionText =
                        std::format("Allow {} ({})?", m.prompt.toolName, m.prompt.description);
                    if (!m.prompt.commandPreview.empty())
                        questionText += std::format("\n{}", m.prompt.commandPreview);

                    auto options = std::vector<std::string> { "Yes", "Yes, always for this tool", "No" };
                    permissionPrompt.component.emplace(tui::QuestionConfig {
                        .questionText = std::move(questionText),
                        .options = std::move(options),
                        .multiSelect = false,
                        .allowOther = false,
                    });
                    permissionPrompt.requestId = m.requestId;
                    permissionPrompt.active = true;
                    permissionPrompt.render(out, terminal);
                }
                else if constexpr (std::is_same_v<T, agent::PlanGeneratedMessage>)
                {
                    clearStreamingPrompt();
                    if (currentRenderer)
                        currentRenderer->renderPlan(m.plan);
                    pendingPlan = std::move(m.plan);
                }
                else if constexpr (std::is_same_v<T, agent::PlanStepStartMessage>)
                {
                    clearStreamingPrompt();
                    if (currentRenderer)
                        currentRenderer->end();
                    streaming = true;
                    streamCancelled = false;
                    currentRenderer.emplace(out);
                    activeRenderer = &*currentRenderer;
                    currentRenderer->begin();
                    inputComponent.setThinkingActive(true);
                    inputComponent.setActivityLabel(
                        std::format("Step {}/{}: {}...", m.stepIndex + 1, m.totalSteps, m.description));
                }
                else if constexpr (std::is_same_v<T, agent::PlanStepCompleteMessage>)
                {
                    clearStreamingPrompt();
                    if (currentRenderer)
                        currentRenderer->end();
                    currentRenderer.reset();
                    activeRenderer = nullptr;
                    auto const barStyle = tui::Style { .fg = theme.agentColors.leftBar };
                    if (m.status == agent::PlanStepStatus::Completed)
                    {
                        auto const okStyle = tui::Style { .fg = theme.agentColors.statusText };
                        out.writeText("\u2502 ", barStyle);
                        out.writeText(std::format("[\xe2\x9c\x93] Step {} completed\n", m.stepIndex + 1),
                                      okStyle);
                    }
                    else
                    {
                        auto const errStyle = tui::Style { .fg = theme.agentColors.errorText };
                        out.writeText("\u2502 ", barStyle);
                        out.writeText(std::format("[\xe2\x9c\x97] Step {} failed", m.stepIndex + 1),
                                      errStyle);
                        if (!m.errorMessage.empty())
                            out.writeText(": " + m.errorMessage, errStyle);
                        out.linefeed();
                    }
                    out.flush();
                }
                else if constexpr (std::is_same_v<T, agent::PlanCompleteMessage>)
                {
                    clearStreamingPrompt();
                    if (currentRenderer)
                        currentRenderer->end();

                    // Show final plan progress summary.
                    currentRenderer.emplace(out);
                    auto const lastStep = m.plan.steps.empty() ? size_t { 0 } : m.plan.steps.size() - 1;
                    currentRenderer->renderPlanProgress(m.plan, lastStep);
                    currentRenderer->end();

                    teardownStreaming();
                    inputComponent.setThinkingActive(false);
                    saveHistory();

                    screen.releaseCursor();
                    auto const newPrefSize = inputComponent.preferredSize();
                    inputComponent.setArea(tui::Rect {
                        .x = 0, .y = 0, .width = terminal.columns(), .height = newPrefSize.height });
                    screen.draw();
                }
                else if constexpr (std::is_same_v<T, agent::TraceEventMessage>)
                {
                    if (_shell.agentConfig.trace.terminal)
                    {
                        clearStreamingPrompt();
                        agent::renderTraceEvent(out, m.event);
                        if (activeRenderer && activeRenderer->isThinking())
                            activeRenderer->renderSpinner();
                    }
                }
                else if constexpr (std::is_same_v<T, agent::AgentShutdownComplete>)
                {
                    // Worker thread exited. Clean up if needed.
                }
            },
            agentMsg);
    }

    // Re-render the streaming prompt after each message batch that produced content.
    // Suppress while ask-user is active — only one inline prompt at a time.
    if (streaming && !streamingPromptVisible && !anyPromptActive())
        renderStreamingPrompt();
}
} // namespace endo
