// SPDX-License-Identifier: Apache-2.0
#include <shell/AgentModeSession.hpp>
#include <shell/Shell.hpp>

#include <tui/Buffer.hpp>
#include <tui/Canvas.hpp>
#include <tui/ImageLoader.hpp>
#include <tui/Screen.hpp>
#include <tui/Theme.hpp>

#include <charconv>

#include <agent/AgentConfig.hpp>
#include <agent/PermissionManager.hpp>
#include <agent/commands/AgentHistoryProvider.hpp>
#include <agent/commands/FilePathCompleter.hpp>
#include <agent/commands/SlashCommand.hpp>
#include <agent/commands/SlashCommandRegistry.hpp>
#include <agent/commands/SlashCommands.hpp>
#include <agent/context/FileReferenceExpander.hpp>
#include <agent/conversation/ConversationHistoryStore.hpp>
#include <agent/conversation/SessionManager.hpp>
#include <agent/providers/ProviderFactory.hpp>
#include <agent/providers/ProviderModels.hpp>
#include <agent/session/AgentSession.hpp>
#include <agent/session/AgentWorker.hpp>
#include <agent/tools/DiffRenderer.hpp>
#include <agent/tools/ExploreTool.hpp>
#include <agent/tools/ToolRegistry.hpp>
#include <agent/tracing/TraceTerminalRenderer.hpp>

namespace endo
{

namespace
{
    /// Renders @p component into a fresh off-screen buffer and writes it directly
    /// to @p out. This bypasses the Screen's managed double-buffer so the content
    /// lands inline in the terminal scrollback; the component is laid out at full
    /// @p width and its own preferred height.
    /// @param out The terminal output to write the rendered buffer to.
    /// @param component The component to lay out and render.
    /// @param width The width, in columns, to render the component at.
    void renderComponentInline(tui::TerminalOutput& out, tui::Component& component, int width)
    {
        auto const& theme = tui::currentTheme();
        auto const height = component.preferredSize().height;
        auto const rect = tui::Rect { .x = 0, .y = 0, .width = width, .height = height };
        auto buffer = tui::Buffer(height, width);
        auto canvas = tui::Canvas(buffer, rect, theme);
        component.setArea(rect);
        component.setScreenBounds(rect);
        component.render(canvas);
        buffer.writeTo(out);
    }

    /// Trims leading and trailing ASCII spaces from @p text.
    /// @param text The string view to trim.
    /// @return The trimmed view (spaces only; tabs and newlines are preserved).
    [[nodiscard]] std::string_view trimSpaces(std::string_view text)
    {
        while (!text.empty() && text.front() == ' ')
            text.remove_prefix(1);
        while (!text.empty() && text.back() == ' ')
            text.remove_suffix(1);
        return text;
    }
} // namespace

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
    auto const width = terminal.columns();
    auto const height = component->preferredSize().height;

    // Pre-scroll by the prompt height so saveCursor records a valid position.
    for (auto i = 0; i < height; ++i)
        output.linefeed();
    output.moveUp(height);
    output.saveCursor();
    output.linefeed();

    renderComponentInline(output, *component, width);

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
    renderComponentInline(_out, _inputComponent, _terminal.columns());
}

void AgentModeSession::renderToolStatusDirect()
{
    if (!_toolStatusComponent.hasEntries())
        return;
    if (_toolStatusComponent.preferredSize().height <= 0)
        return;
    renderComponentInline(_out, _toolStatusComponent, _terminal.columns());
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
                                          agent::ModelInfo const& modelInfo)
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

void AgentModeSession::ensureSystemPromptReady()
{
    if (systemPromptReady)
        return;
    auto result = _contextFuture.get();
    _shell._agentSession->setSystemPrompt(std::move(result.systemPrompt));
    if (auto* explore = dynamic_cast<agent::ExploreTool*>(_toolRegistry.findTool("explore")))
        explore->setSystemPrompt(std::move(result.exploreSystemPrompt));
    if (!result.gitBranch.empty())
        _inputComponent.setGitBranch(std::move(result.gitBranch));
    if (!result.projectPath.empty())
        _inputComponent.setProjectPath(std::move(result.projectPath));
    _filePathProviderPtr->setFilePaths(result.projectContext.filePaths);
    _shell._cachedProjectContext = std::move(result.projectContext);
    _shell._cachedProjectContextCwd = _cwd;
    systemPromptReady = true;
}

LoopControl AgentModeSession::handleInputEvent(tui::InputEvent const& event,
                                               bool& needsRedraw,
                                               agent::SlashCommandRegistry const& slashRegistry)
{
    // Aliases map the moved dispatch body's bare collaborator names onto the
    // session's borrowed references, so the body below is otherwise verbatim.
    auto& out = _out;
    auto& terminal = _terminal;
    auto& screen = _screen;
    auto& inputComponent = _inputComponent;
    auto& worker = _worker;
    auto*& provider = _provider;
    auto const& sessionManager = _sessionManager;
    auto& mcpServerManager = _mcpServerManager;
    auto* const historyProviderPtr = _historyProviderPtr;
    auto const& theme = tui::currentTheme();

    // During ask-user, route input to the question component.
    if (askUserPrompt.active && askUserPrompt.component)
    {
        // Drive the question through its modal step(); a returned result
        // means confirmed/cancelled, std::nullopt means continue (redraw
        // only when the step changed visible state).
        auto const stepResult = askUserPrompt.component->step(event);
        if (stepResult && stepResult->confirmed)
        {
            {
                auto const& answerText = stepResult->answer;
                auto const qConfig = askUserPrompt.component->config();
                auto const selectedIdx = stepResult->selectedIndex;
                auto const& checkedIdx = stepResult->checkedIndices;
                auto const otherActive = stepResult->otherActive;
                worker.inbound().push(
                    agent::UserAnswerMessage { .requestId = askUserPrompt.requestId,
                                               .answer = agent::UserAnswer { .answer = answerText } });
                askUserPrompt.clear(out);
                // Echo question + options with selection to scrollback
                {
                    auto const barStyle = tui::Style { .fg = theme.agentColors.leftBar };
                    auto const questionStyle = tui::Style { .fg = theme.colors.text };
                    auto const normalStyle = tui::Style { .fg = theme.agentColors.statusText, .dim = true };
                    auto const selectedStyle = tui::Style { .fg = theme.agentColors.leftBar, .bold = true };
                    // Question text
                    out.writeText("\u2502 ", barStyle);
                    out.writeText(qConfig.questionText, questionStyle);
                    out.linefeed();
                    // Options
                    if (qConfig.multiSelect)
                    {
                        auto const checkedSet = std::set<std::size_t>(checkedIdx.begin(), checkedIdx.end());
                        for (auto i = std::size_t { 0 }; i < qConfig.options.size(); ++i)
                        {
                            auto const checked = checkedSet.contains(i);
                            out.writeText("\u2502 ", barStyle);
                            if (checked)
                            {
                                out.writeText(" \xe2\x96\xb6 " + qConfig.options[i], selectedStyle);
                            }
                            else
                            {
                                out.writeText("   " + qConfig.options[i], normalStyle);
                            }
                            out.linefeed();
                        }
                    }
                    else
                    {
                        for (auto i = std::size_t { 0 }; i < qConfig.options.size(); ++i)
                        {
                            out.writeText("\u2502 ", barStyle);
                            if (!otherActive && i == selectedIdx)
                            {
                                out.writeText(" \xe2\x96\xb6 " + qConfig.options[i], selectedStyle);
                            }
                            else
                            {
                                out.writeText("   " + qConfig.options[i], normalStyle);
                            }
                            out.linefeed();
                        }
                    }
                    // Custom "Other..." text
                    if (otherActive)
                    {
                        out.writeText("\u2502 ", barStyle);
                        out.writeText(" \xe2\x96\xb6 " + answerText, selectedStyle);
                        out.linefeed();
                    }
                    out.writeText("\u2502", barStyle);
                    out.linefeed();
                    out.flush();
                }
                askUserPrompt.reset();
            }
        }
        else if (stepResult)
        {
            {
                auto const qConfig = askUserPrompt.component->config();
                worker.inbound().push(
                    agent::UserAnswerMessage { .requestId = askUserPrompt.requestId,
                                               .answer = agent::UserAnswer { .cancelled = true } });
                askUserPrompt.clear(out);
                // Echo question + options with cancellation to scrollback
                {
                    auto const barStyle = tui::Style { .fg = theme.agentColors.leftBar };
                    auto const questionStyle = tui::Style { .fg = theme.colors.text };
                    auto const normalStyle = tui::Style { .fg = theme.agentColors.statusText, .dim = true };
                    auto const dimStyle = tui::Style { .fg = theme.agentColors.statusText, .dim = true };
                    // Question text
                    out.writeText("\u2502 ", barStyle);
                    out.writeText(qConfig.questionText, questionStyle);
                    out.linefeed();
                    // Options (all unselected)
                    for (auto const& opt: qConfig.options)
                    {
                        out.writeText("\u2502 ", barStyle);
                        out.writeText("   " + opt, normalStyle);
                        out.linefeed();
                    }
                    // Cancellation notice
                    out.writeText("\u2502 ", barStyle);
                    out.writeText(" (cancelled)", dimStyle);
                    out.linefeed();
                    out.writeText("\u2502", barStyle);
                    out.linefeed();
                    out.flush();
                }
                askUserPrompt.reset();
            }
        }
        else if (askUserPrompt.component->stepChangedState())
        {
            auto guard = out.syncGuard();
            askUserPrompt.clear(out);
            askUserPrompt.render(out, terminal);
        }
        return LoopControl::Continue;
    }

    // During permission prompt, route input to the permission component.
    if (permissionPrompt.active && permissionPrompt.component)
    {
        auto const stepResult = permissionPrompt.component->step(event);
        if (stepResult && stepResult->confirmed)
        {
            {
                auto const selectedIdx = stepResult->selectedIndex;
                permissionPrompt.clear(out);

                auto decision = agent::PermissionDecision::Denied;
                if (selectedIdx == 0) // "Yes"
                    decision = agent::PermissionDecision::Approved;
                else if (selectedIdx == 1) // "Yes, always for this tool"
                    decision = agent::PermissionDecision::Approved;
                else // "No"
                    decision = agent::PermissionDecision::Denied;

                worker.inbound().push(agent::PermissionResponseMessage {
                    .requestId = permissionPrompt.requestId,
                    .decision = decision,
                });

                // Echo the permission decision to scrollback.
                {
                    auto const barStyle = tui::Style { .fg = theme.agentColors.leftBar };
                    auto const dimStyle = tui::Style { .fg = theme.agentColors.statusText };
                    out.writeText("\u2502 ", barStyle);
                    if (decision == agent::PermissionDecision::Approved)
                        out.writeText("Approved", dimStyle);
                    else
                        out.writeText("Denied", dimStyle);
                    out.linefeed();
                    out.flush();
                }

                permissionPrompt.reset();
            }
        }
        else if (stepResult)
        {
            permissionPrompt.clear(out);
            worker.inbound().push(agent::PermissionResponseMessage {
                .requestId = permissionPrompt.requestId,
                .decision = agent::PermissionDecision::Cancelled,
            });

            auto const barStyle = tui::Style { .fg = theme.agentColors.leftBar };
            auto const dimStyle = tui::Style { .fg = theme.agentColors.statusText };
            out.writeText("\u2502 ", barStyle);
            out.writeText("(cancelled)", dimStyle);
            out.linefeed();
            out.flush();

            permissionPrompt.reset();
        }
        else if (permissionPrompt.component->stepChangedState())
        {
            auto guard = out.syncGuard();
            permissionPrompt.clear(out);
            permissionPrompt.render(out, terminal);
        }
        return LoopControl::Continue;
    }

    // During session picker, route input to the session picker component.
    if (sessionPickerPrompt.active && sessionPickerPrompt.component)
    {
        auto const stepResult = sessionPickerPrompt.component->step(event);
        if (stepResult && stepResult->confirmed)
        {
            {
                auto const selectedIdx = stepResult->selectedIndex;
                sessionPickerPrompt.clear(out);
                if (selectedIdx < sessionPickerNames.size())
                {
                    auto const& name = sessionPickerNames[selectedIdx];
                    auto loaded = sessionManager.loadSession(name);
                    if (loaded.has_value())
                    {
                        auto& [meta, messages] = *loaded;
                        (*_shell._agentSession).reset();
                        historyProviderPtr->setEntries({});
                        for (auto const& msg: messages)
                        {
                            if (msg.role == agent::Role::User)
                            {
                                auto const text =
                                    agent::FileReferenceExpander::stripExpansions(msg.textContent());
                                if (!text.empty())
                                    historyProviderPtr->addEntry(text);
                            }
                        }
                        _shell._agentSession->loadPersistedMessages(std::move(messages));
                        _shell._activeSessionName = name;
                        _shell._sessionCreatedAt = meta.createdAt;
                        sessionManager.setLastActiveSession(name);
                        out.writeText("Session '" + name + "' loaded.\n");
                    }
                    else
                    {
                        auto const errorStyle = tui::Style { .fg = theme.agentColors.errorText };
                        out.writeText("Failed to load session: " + loaded.error().message + "\n", errorStyle);
                    }
                }
                sessionPickerPrompt.reset();
                sessionPickerNames.clear();
                out.flush();
            }
        }
        else if (stepResult)
        {
            sessionPickerPrompt.clear(out);
            sessionPickerPrompt.reset();
            sessionPickerNames.clear();
        }
        else if (sessionPickerPrompt.component->stepChangedState())
        {
            auto guard = out.syncGuard();
            sessionPickerPrompt.clear(out);
            sessionPickerPrompt.render(out, terminal);
        }
        return LoopControl::Continue;
    }

    // During plan approval, route input to the approval component.
    if (planApprovalPrompt.isActive())
    {
        auto const stepResult = planApprovalPrompt.component->step(event);
        if (stepResult && stepResult->confirmed)
        {
            {
                auto const selectedIdx = stepResult->selectedIndex;
                planApprovalPrompt.clear(out);
                planApprovalPrompt.reset();
                if (selectedIdx == 0) // "Yes, execute"
                {
                    worker.inbound().push(agent::PlanApproveMessage { .plan = std::move(*pendingPlan) });
                    pendingPlan.reset();
                    streaming = true;
                    inputComponent.setThinkingActive(true);
                    inputComponent.setActivityLabel("Executing plan...");
                }
                else if (selectedIdx == 1) // "Yes, compact context first"
                {
                    worker.inbound().push(
                        agent::PlanApproveMessage { .plan = std::move(*pendingPlan), .compactFirst = true });
                    pendingPlan.reset();
                    streaming = true;
                    inputComponent.setThinkingActive(true);
                    inputComponent.setActivityLabel("Compacting context...");
                }
                else if (selectedIdx == 2) // "No, discard"
                {
                    pendingPlan.reset();
                    auto const dimStyle = tui::Style { .fg = theme.agentColors.statusText };
                    out.writeText("Plan discarded.\n", dimStyle);
                    out.flush();
                    screen.releaseCursor();
                    auto const newPrefSize = inputComponent.preferredSize();
                    inputComponent.setArea(tui::Rect {
                        .x = 0, .y = 0, .width = terminal.columns(), .height = newPrefSize.height });
                    screen.draw();
                }
                else // "Revise"
                {
                    pendingPlan.reset();
                    // Stay in plan mode for revision.
                    screen.releaseCursor();
                    auto const newPrefSize = inputComponent.preferredSize();
                    inputComponent.setArea(tui::Rect {
                        .x = 0, .y = 0, .width = terminal.columns(), .height = newPrefSize.height });
                    screen.draw();
                }
            }
        }
        else if (stepResult)
        {
            planApprovalPrompt.clear(out);
            planApprovalPrompt.reset();
            pendingPlan.reset();
            {
                screen.releaseCursor();
                auto const newPrefSize = inputComponent.preferredSize();
                inputComponent.setArea(
                    tui::Rect { .x = 0, .y = 0, .width = terminal.columns(), .height = newPrefSize.height });
                screen.draw();
            }
        }
        else if (planApprovalPrompt.component->stepChangedState())
        {
            auto guard = out.syncGuard();
            planApprovalPrompt.clear(out);
            planApprovalPrompt.render(out, terminal);
        }
        return LoopControl::Continue;
    }

    // During streaming, only handle Escape (cancel) and Ctrl+L (clear).
    if (streaming)
    {
        auto const action = inputComponent.processInput(event);
        switch (action)
        {
            case agent::AgentInputComponent::Action::Abort:
                streamCancelled = true;
                worker.inbound().push(agent::CancelMessage {});
                break;
            case agent::AgentInputComponent::Action::ClearScreen:
                out.clearScreen();
                out.flush();
                streamingPromptVisible = false;
                renderStreamingPrompt();
                break;
            case agent::AgentInputComponent::Action::Changed: {
                auto guard = out.syncGuard();
                clearStreamingPrompt();
                renderStreamingPrompt();
                break;
            }
            default: break;
        }
        return LoopControl::Continue;
    }

    // Not streaming — handle full input.
    auto const action = inputComponent.processInput(event);
    switch (action)
    {
        case agent::AgentInputComponent::Action::Submit: {
            // Poll MCP servers for tool list changes before each LLM turn.
            mcpServerManager.processNotifications();

            auto sentToWorker = false;

            // Ensure system prompt is ready.
            ensureSystemPromptReady();

            auto const query = std::string(inputComponent.text());

            // Extract attached images before clearing.
            auto attachedImages = std::vector<agent::ImageBlock>(inputComponent.attachedImages().begin(),
                                                                 inputComponent.attachedImages().end());

            // Expand @-file references for agent context injection.
            auto const expandFileRefs = [&](std::string_view text) {
                return agent::FileReferenceExpander::expand(text, std::filesystem::current_path())
                    .expandedMessage;
            };

            if (!query.starts_with("/"))
            {
                inputComponent.inputField().addHistory(query);
                historyProviderPtr->addEntry(query);
            }

            // Move cursor past the input component, preserving image previews in scrollback.
            auto const totalLines = inputComponent.inputField().lineCount();
            auto const cursorLine = inputComponent.inputField().cursorLine();
            auto const previewLines = inputComponent.imagePreviewHeight();
            inputComponent.clear();
            auto const linesToMoveDown = totalLines - cursorLine + previewLines;
            if (linesToMoveDown > 0)
                out.moveDown(linesToMoveDown);
            out.carriageReturn();
            out.clearLine(); // Clear info line (shortcut hints / spinner)
            out.linefeed();
            out.clearLine(); // Clear bottom padding (NBSP marker)
            out.flush();

            screen.releaseCursor();

            // Dispatch slash commands.
            if (query.starts_with("/"))
            {
                auto const spacePos = query.find(' ');
                auto const cmdName =
                    query.substr(1, spacePos == std::string::npos ? std::string::npos : spacePos - 1);
                auto const args = spacePos != std::string::npos ? query.substr(spacePos + 1) : "";

                if (auto const* cmd = slashRegistry.findCommand(cmdName))
                {
                    auto commandResult = cmd->execute(args);
                    if (auto const* d = std::get_if<agent::DirectOutput>(&commandResult))
                    {
                        out.writeText(d->text);
                        out.flush();
                    }
                    else if (auto const* m = std::get_if<agent::MarkdownOutput>(&commandResult))
                    {
                        auto mdRenderer = tui::MarkdownRenderer(out);
                        mdRenderer.setMaxWidth(terminal.columns());
                        mdRenderer.render(m->markdown);
                        out.flush();
                    }
                    else if (auto const* p = std::get_if<agent::PlanModeRequest>(&commandResult))
                    {
                        if (!_shell.agentConfig.planMode.enabled)
                        {
                            auto const errorStyle = tui::Style { .fg = theme.agentColors.errorText };
                            out.writeText("Plan mode is disabled in configuration.\n", errorStyle);
                            out.flush();
                        }
                        else if (p->query.empty())
                        {
                            if (!planModeActive)
                            {
                                planModeActive = true;
                                inputComponent.setPlanMode(true);
                            }
                            auto const infoStyle = tui::Style { .fg = theme.agentColors.statusText };
                            out.writeText("Plan mode active. Type your task to generate a plan.\n",
                                          infoStyle);
                            out.flush();
                        }
                        else
                        {
                            // Send plan query to worker.
                            worker.inbound().push(agent::UserPromptMessage { .text = expandFileRefs(p->query),
                                                                             .planMode = true });
                            sentToWorker = true;
                        }
                    }
                    else if (auto const* r = std::get_if<agent::PromptRewrite>(&commandResult))
                    {
                        // Send rewritten prompt to worker.
                        worker.inbound().push(agent::UserPromptMessage { .text = expandFileRefs(r->prompt) });
                        sentToWorker = true;
                    }
                    else if (auto const* sp = std::get_if<agent::SessionPickerRequest>(&commandResult))
                    {
                        // Show interactive session picker using QuestionComponent.
                        sessionPickerNames = sp->sessionNames;
                        sessionPickerPrompt.component.emplace(tui::QuestionConfig {
                            .questionText = sp->questionText,
                            .options = sp->options,
                            .multiSelect = false,
                            .allowOther = false,
                        });
                        sessionPickerPrompt.active = true;
                        sessionPickerPrompt.render(out, terminal);
                    }
                }
                else
                {
                    auto const errorStyle = tui::Style { .fg = theme.agentColors.errorText };
                    out.writeText("Unknown command: /" + cmdName + "\n", errorStyle);
                    out.flush();
                }
            }
            else if (planModeActive && _shell.agentConfig.planMode.enabled)
            {
                // Plan mode: send to worker with planMode flag.
                worker.inbound().push(agent::UserPromptMessage {
                    .text = expandFileRefs(query),
                    .planMode = true,
                    .images = std::move(attachedImages),
                });
                sentToWorker = true;
                saveHistory();
            }
            else
            {
                // Normal message: send to worker.
                worker.inbound().push(agent::UserPromptMessage {
                    .text = expandFileRefs(query),
                    .images = std::move(attachedImages),
                });
                sentToWorker = true;
            }

            // Re-render input component only for non-streaming commands.
            // Streaming responses re-render via CompletionMessage handler.
            if (!sentToWorker)
            {
                auto const newPrefSize = inputComponent.preferredSize();
                inputComponent.setArea(
                    tui::Rect { .x = 0, .y = 0, .width = terminal.columns(), .height = newPrefSize.height });
                screen.draw();
            }
            break;
        }
        case agent::AgentInputComponent::Action::Abort: {
            // Stop worker before exiting agent mode.
            worker.stop();
            _shell._agentSession->setPermissionManager(nullptr);
            _shell._agentSession->setToolRegistry(nullptr);
            _shell._agentSession->setToolStatusCallback(nullptr);
            _shell._agentSession->setTracer(nullptr);
            terminal.input().setWakeup(nullptr);
            screen.clearAndRelease();
            return LoopControl::Exit;
        }
        case agent::AgentInputComponent::Action::CycleMode: {
            planModeActive = !planModeActive;
            inputComponent.setPlanMode(planModeActive);
            needsRedraw = true;
            break;
        }
        case agent::AgentInputComponent::Action::CycleThinkingMode: {
            // Cycle thinking mode for the active provider.
            auto const& pName = _shell._agentProviderFactory->activeProviderName();
            auto* thinkingModePtr = static_cast<agent::ThinkingMode*>(nullptr);
            if (pName == "claude")
                thinkingModePtr = &_shell.agentConfig.claude.thinkingMode;
            else if (pName == "openai")
                thinkingModePtr = &_shell.agentConfig.openai.thinkingMode;
            else if (pName == "openai_compat")
                thinkingModePtr = &_shell.agentConfig.openaiCompat.thinkingMode;
            else if (pName == "gemini")
                thinkingModePtr = &_shell.agentConfig.gemini.thinkingMode;

            if (thinkingModePtr)
            {
                *thinkingModePtr = agent::nextThinkingMode(*thinkingModePtr);
                inputComponent.setThinkingMode(*thinkingModePtr);
                auto const currentModel = provider->modelInfo().modelName;
                switchToModel(pName, currentModel);
            }
            needsRedraw = true;
            break;
        }
        case agent::AgentInputComponent::Action::CycleModel: {
            // Cycle through hardcoded model list for the active provider.
            auto const& pName = _shell._agentProviderFactory->activeProviderName();
            auto const models = agent::modelsForProvider(pName);
            if (!models.empty())
            {
                auto const currentModel = provider->modelInfo().modelName;
                auto const nextModelName = agent::nextModel(models, currentModel);
                switchToModel(pName, nextModelName);
            }
            needsRedraw = true;
            break;
        }
        case agent::AgentInputComponent::Action::ClearScreen: {
            out.clearScreen();
            out.flush();
            screen.releaseCursor();
            needsRedraw = true;
            break;
        }
        case agent::AgentInputComponent::Action::CommandPalette:
            // Palette is shown internally by AgentInputComponent; just redraw.
            needsRedraw = true;
            break;
        case agent::AgentInputComponent::Action::NewPrompt:
            inputComponent.clear();
            needsRedraw = true;
            break;
        case agent::AgentInputComponent::Action::Changed: needsRedraw = true; break;
        case agent::AgentInputComponent::Action::None: break;
    }

    return LoopControl::Continue;
}

agent::SessionMetadata AgentModeSession::buildSessionMetadata(std::string name) const
{
    auto const now = std::chrono::system_clock::now();
    return agent::SessionMetadata {
        .name = std::move(name),
        .createdAt = _shell._sessionCreatedAt == std::chrono::system_clock::time_point {}
                         ? now
                         : _shell._sessionCreatedAt,
        .updatedAt = now,
        .provider = _shell._agentProviderFactory->activeProviderName(),
        .model = _provider->modelInfo().modelName,
        .turnCount = _shell._agentSession->turnCount(),
        .tokenUsage = _shell._agentSession->sessionUsage(),
    };
}

void AgentModeSession::saveHistory()
{
    auto const& sessionManager = _sessionManager;
    auto const& historyStore = _historyStore;
    (void) historyStore.save( // NOLINT(bugprone-unused-return-value)
        _shell._agentSession->history().messages());
    // Also save to named session if active.
    if (!_shell._activeSessionName.empty())
    {
        (void) sessionManager.saveSession( // NOLINT(bugprone-unused-return-value)
            _shell._activeSessionName,
            _shell._agentSession->history().messages(),
            buildSessionMetadata(_shell._activeSessionName));
        sessionManager.setLastActiveSession(_shell._activeSessionName);
    }
}

bool AgentModeSession::switchToModel(std::string_view targetProvider, std::string_view targetModel)
{
    auto& worker = _worker;
    auto& inputComponent = _inputComponent;
    auto*& provider = _provider;
    // Update the config for the target provider.
    auto const pName = std::string(targetProvider);
    std::string* modelPtr = nullptr;
    if (pName == "claude")
        modelPtr = &_shell.agentConfig.claude.model;
    else if (pName == "openai")
        modelPtr = &_shell.agentConfig.openai.model;
    else if (pName == "openai_compat")
        modelPtr = &_shell.agentConfig.openaiCompat.model;
    else if (pName == "gemini")
        modelPtr = &_shell.agentConfig.gemini.model;
    else
        return false;

    if (modelPtr)
        *modelPtr = std::string(targetModel);
    _shell.agentConfig.activeProvider = pName;

    // Stop worker before replacing factory to avoid use-after-free:
    // the worker thread holds a reference to the provider via AgentSession.
    worker.stop();
    _shell._agentProviderFactory =
        std::make_unique<agent::ProviderFactory>(*_shell._agentHttpClient, _shell.agentConfig);
    if (auto* newProvider = _shell._agentProviderFactory->activeProvider())
    {
        _shell._agentSession->setProvider(*newProvider);
        provider = newProvider;
    }
    worker.start();

    inputComponent.setProviderName(pName);
    inputComponent.setModelName(std::string(targetModel));
    return true;
}

void AgentModeSession::registerSlashCommands(agent::SlashCommandRegistry& registry)
{
    // Aliases map the moved handlers' bare collaborator names onto the session's
    // borrowed references / methods, so the bodies below are otherwise verbatim.
    auto*& provider = _provider;
    auto& inputComponent = _inputComponent;
    auto& worker = _worker;
    auto const& sessionManager = _sessionManager;
    auto const& historyStore = _historyStore;
    auto& permissionManager = _permissionManager;
    auto* const historyProviderPtr = _historyProviderPtr;
    auto& toolRegistry = _toolRegistry;

    registry.registerCommand(std::make_unique<agent::CallbackSlashCommand>(
        "reset", "Clear conversation history", [&](std::string_view) -> agent::SlashCommandResult {
            (*_shell._agentSession).reset();
            permissionManager.resetApprovals();
            (void) historyStore.remove(); // NOLINT(bugprone-unused-return-value)
            historyProviderPtr->setEntries({});
            _shell._activeSessionName.clear();
            _shell._sessionCreatedAt = {};
            sessionManager.clearLastActiveSession();
            return agent::DirectOutput { .text = "Conversation history cleared.\n" };
        }));

    // --- Session management slash commands ---

    // /clear and /new: auto-save current session, then start fresh.
    auto clearHandler = std::function<agent::SlashCommandResult(std::string_view)>(
        [&](std::string_view) -> agent::SlashCommandResult {
            auto savedName = std::string {};
            // Save current conversation if it has content.
            if (_shell._agentSession->turnCount() > 0)
            {
                if (_shell._activeSessionName.empty())
                {
                    // Auto-generate a name from the first user message.
                    for (auto const& msg: _shell._agentSession->history().messages())
                    {
                        if (msg.role == agent::Role::User)
                        {
                            savedName = sessionManager.generateSessionName(msg.textContent());
                            break;
                        }
                    }
                    if (savedName.empty())
                        savedName = sessionManager.generateSessionName("untitled");
                }
                else
                {
                    savedName = _shell._activeSessionName;
                }

                (void) sessionManager.saveSession( // NOLINT(bugprone-unused-return-value)
                    savedName,
                    _shell._agentSession->history().messages(),
                    buildSessionMetadata(savedName));
            }

            // Reset everything for a fresh conversation.
            (*_shell._agentSession).reset();
            permissionManager.resetApprovals();
            (void) historyStore.remove(); // NOLINT(bugprone-unused-return-value)
            historyProviderPtr->setEntries({});
            _shell._activeSessionName.clear();
            _shell._sessionCreatedAt = {};
            sessionManager.clearLastActiveSession();

            if (!savedName.empty())
                return agent::DirectOutput { .text = "Session saved as '" + savedName
                                                     + "'. Starting new conversation.\n" };
            return agent::DirectOutput { .text = "Starting new conversation.\n" };
        });
    registry.registerCommand(std::make_unique<agent::CallbackSlashCommand>(
        "clear", "Auto-save and start new conversation", clearHandler));
    registry.registerCommand(std::make_unique<agent::CallbackSlashCommand>(
        "new", "Auto-save and start new conversation", clearHandler));

    // /save (alias: /save-session): Save current session with a name.
    auto saveHandler = std::function<agent::SlashCommandResult(std::string_view)>(
        [&](std::string_view arguments) -> agent::SlashCommandResult {
            auto name = std::string(trimSpaces(arguments));

            // Auto-generate name from first user message if none given.
            if (name.empty())
            {
                for (auto const& msg: _shell._agentSession->history().messages())
                {
                    if (msg.role == agent::Role::User)
                    {
                        name = sessionManager.generateSessionName(msg.textContent());
                        break;
                    }
                }
                if (name.empty())
                    name = sessionManager.generateSessionName("untitled");
            }

            auto const metadata = buildSessionMetadata(name);

            auto result =
                sessionManager.saveSession(name, _shell._agentSession->history().messages(), metadata);
            if (!result.has_value())
                return agent::DirectOutput { .text =
                                                 "Failed to save session: " + result.error().message + "\n" };

            _shell._activeSessionName = name;
            if (_shell._sessionCreatedAt == std::chrono::system_clock::time_point {})
                _shell._sessionCreatedAt = metadata.createdAt;
            sessionManager.setLastActiveSession(name);
            return agent::DirectOutput { .text = "Session saved as '" + name + "'.\n" };
        });
    registry.registerCommand(std::make_unique<agent::CallbackSlashCommand>(
        "save", "Save current session with a name", saveHandler));
    registry.registerCommand(std::make_unique<agent::CallbackSlashCommand>(
        "save-session", "Save current session with a name", saveHandler));

    // /load (alias: /load-session): Load a saved session.
    auto loadHandler = std::function<agent::SlashCommandResult(std::string_view)>(
        [&](std::string_view arguments) -> agent::SlashCommandResult {
            auto name = std::string(trimSpaces(arguments));

            if (name.empty())
            {
                // No name given — show interactive session picker.
                auto sessionsResult = sessionManager.listSessions();
                if (!sessionsResult.has_value() || sessionsResult->empty())
                    return agent::DirectOutput { .text = "No saved sessions found.\n" };

                auto options = std::vector<std::string> {};
                auto names = std::vector<std::string> {};
                for (auto const& meta: *sessionsResult)
                {
                    auto const total = meta.tokenUsage.inputTokens + meta.tokenUsage.outputTokens;
                    auto label =
                        std::format("{} ({} turns, ~{}k tokens)", meta.name, meta.turnCount, total / 1000);
                    options.push_back(std::move(label));
                    names.push_back(meta.name);
                }
                return agent::SessionPickerRequest {
                    .questionText = "Select a session to load:",
                    .options = std::move(options),
                    .sessionNames = std::move(names),
                };
            }

            // Load by name.
            auto loaded = sessionManager.loadSession(name);
            if (!loaded.has_value())
                return agent::DirectOutput { .text =
                                                 "Failed to load session: " + loaded.error().message + "\n" };

            auto& [meta, messages] = *loaded;
            (*_shell._agentSession).reset();
            historyProviderPtr->setEntries({});
            for (auto const& msg: messages)
            {
                if (msg.role == agent::Role::User)
                {
                    auto const text = agent::FileReferenceExpander::stripExpansions(msg.textContent());
                    if (!text.empty())
                        historyProviderPtr->addEntry(text);
                }
            }
            _shell._agentSession->loadPersistedMessages(std::move(messages));
            _shell._activeSessionName = name;
            _shell._sessionCreatedAt = meta.createdAt;
            sessionManager.setLastActiveSession(name);
            return agent::DirectOutput { .text = "Session '" + name + "' loaded ("
                                                 + std::to_string(meta.turnCount) + " turns).\n" };
        });
    registry.registerCommand(
        std::make_unique<agent::CallbackSlashCommand>("load", "Load a saved session", loadHandler));
    registry.registerCommand(
        std::make_unique<agent::CallbackSlashCommand>("load-session", "Load a saved session", loadHandler));

    // /delete (alias: /delete-session): Delete a saved session.
    auto deleteHandler = std::function<agent::SlashCommandResult(std::string_view)>(
        [&](std::string_view arguments) -> agent::SlashCommandResult {
            auto name = std::string(trimSpaces(arguments));

            if (name.empty())
                return agent::DirectOutput { .text = "Usage: /delete <name>\n" };

            if (!sessionManager.sessionExists(name))
                return agent::DirectOutput { .text = "Session '" + name + "' not found.\n" };

            auto result = sessionManager.removeSession(name);
            if (!result.has_value())
                return agent::DirectOutput { .text = "Failed to delete session: " + result.error().message
                                                     + "\n" };

            if (_shell._activeSessionName == name)
            {
                _shell._activeSessionName.clear();
                _shell._sessionCreatedAt = {};
                sessionManager.clearLastActiveSession();
            }
            return agent::DirectOutput { .text = "Session '" + name + "' deleted.\n" };
        });
    registry.registerCommand(
        std::make_unique<agent::CallbackSlashCommand>("delete", "Delete a saved session", deleteHandler));
    registry.registerCommand(std::make_unique<agent::CallbackSlashCommand>(
        "delete-session", "Delete a saved session", deleteHandler));

    // /rename: Rename the current session.
    registry.registerCommand(std::make_unique<agent::CallbackSlashCommand>(
        "rename", "Rename the current session", [&](std::string_view arguments) -> agent::SlashCommandResult {
            auto newName = std::string(trimSpaces(arguments));

            if (newName.empty())
                return agent::DirectOutput { .text = "Usage: /rename <new-name>\n" };

            if (sessionManager.sessionExists(newName))
                return agent::DirectOutput { .text = "Session '" + newName + "' already exists.\n" };

            if (_shell._activeSessionName.empty())
            {
                // No session saved yet — just set the name for the next save.
                _shell._activeSessionName = newName;
                return agent::DirectOutput { .text = "Session will be saved as '" + newName + "'.\n" };
            }

            auto result = sessionManager.renameSession(_shell._activeSessionName, newName);
            if (!result.has_value())
                return agent::DirectOutput { .text = "Failed to rename session: " + result.error().message
                                                     + "\n" };

            _shell._activeSessionName = newName;
            return agent::DirectOutput { .text = "Session renamed to '" + newName + "'.\n" };
        }));

    // /list (alias: /sessions): List saved sessions.
    auto listHandler = std::function<agent::SlashCommandResult(std::string_view)>(
        [&](std::string_view) -> agent::SlashCommandResult {
            auto sessionsResult = sessionManager.listSessions();
            if (!sessionsResult.has_value())
                return agent::DirectOutput { .text = "Failed to list sessions: "
                                                     + sessionsResult.error().message + "\n" };

            if (sessionsResult->empty())
                return agent::DirectOutput { .text = "No saved sessions.\n" };

            auto md = std::string { "| Name | Turns | Tokens | Updated | Active |\n"
                                    "|:-----|------:|-------:|:--------|:-------|\n" };
            for (auto const& meta: *sessionsResult)
            {
                auto const total = meta.tokenUsage.inputTokens + meta.tokenUsage.outputTokens;
                const auto* const active = (meta.name == _shell._activeSessionName) ? "\xe2\x97\x8f" : "";
                auto const tt = std::chrono::system_clock::to_time_t(meta.updatedAt);
                auto tm = std::tm {};
#if defined(_WIN32)
                localtime_s(&tm, &tt);
#else
                localtime_r(&tt, &tm);
#endif
                auto timeBuf = std::array<char, 32> {};
                std::strftime(timeBuf.data(), timeBuf.size(), "%Y-%m-%d %H:%M", &tm);
                md += std::format("| {} | {} | ~{}k | {} | {} |\n",
                                  meta.name,
                                  meta.turnCount,
                                  total / 1000,
                                  timeBuf.data(),
                                  active);
            }
            return agent::MarkdownOutput { .markdown = std::move(md) };
        });
    registry.registerCommand(
        std::make_unique<agent::CallbackSlashCommand>("list", "List saved sessions", listHandler));
    registry.registerCommand(
        std::make_unique<agent::CallbackSlashCommand>("sessions", "List saved sessions", listHandler));

    registry.registerCommand(std::make_unique<agent::CallbackSlashCommand>(
        "tools",
        "List all active agent tools",
        [&toolRegistry](std::string_view) -> agent::SlashCommandResult {
            auto defs = toolRegistry.definitions();
            std::ranges::sort(defs, {}, &agent::ToolDefinition::name);
            auto md = std::string { "| Tool | Description |\n|:-----|:------------|\n" };
            for (auto const& def: defs)
                md += std::format("| {} | {} |\n", def.name, def.description);
            md += std::format("\n{} tools registered.\n", toolRegistry.size());
            return agent::MarkdownOutput { .markdown = std::move(md) };
        }));

    registry.registerCommand(std::make_unique<agent::CallbackSlashCommand>(
        "status",
        "Show session status (tokens, cost, provider)",
        [&](std::string_view) -> agent::SlashCommandResult {
            auto const& usage = _shell._agentSession->sessionUsage();
            auto const turns = _shell._agentSession->turnCount();
            auto const messageCount = _shell._agentSession->history().size();
            auto const contextTokens = _shell._agentSession->history().estimatedTokenCount();
            auto const& pName = _shell._agentProviderFactory->activeProviderName();
            auto const mInfo = provider->modelInfo();
            auto const cost = agent::estimateCost(usage, pName, mInfo.modelName);
            auto const contextPct =
                mInfo.contextSize > 0 ? static_cast<int>((contextTokens * 100) / mInfo.contextSize) : 0;

            auto md = std::string { "| Metric | Value |\n|:-------|:------|\n" };
            md += std::format("| Provider | {} |\n", pName);
            md += std::format("| Model | {} |\n", mInfo.modelName);
            md += std::format("| Turns | {} |\n", turns);
            md += std::format("| Messages | {} |\n", messageCount);
            md += std::format("| Input tokens | {} |\n", agent::formatTokenCount(usage.inputTokens));
            md += std::format("| Output tokens | {} |\n", agent::formatTokenCount(usage.outputTokens));
            if (usage.cacheReadTokens > 0)
                md += std::format("| Cache read | {} |\n", agent::formatTokenCount(usage.cacheReadTokens));
            if (usage.cacheCreationTokens > 0)
                md +=
                    std::format("| Cache write | {} |\n", agent::formatTokenCount(usage.cacheCreationTokens));
            md += std::format("| Context usage | ~{} / {} ({}%) |\n",
                              agent::formatTokenCount(static_cast<int64_t>(contextTokens)),
                              agent::formatTokenCount(static_cast<int64_t>(mInfo.contextSize)),
                              contextPct);
            if (cost > 0.0)
                md += std::format("| Est. cost | ${:.4f} |\n", cost);
            return agent::MarkdownOutput { .markdown = std::move(md) };
        }));

    registry.registerCommand(std::make_unique<agent::CallbackSlashCommand>(
        "model",
        "Switch model (/model <name> or /model to list)",
        [&](std::string_view args) -> agent::SlashCommandResult {
            auto const trimmedArgs = trimSpaces(args);

            if (trimmedArgs.empty())
            {
                // List all models grouped by provider, marking the active one.
                auto const& activePName = _shell._agentProviderFactory->activeProviderName();
                auto const activeModelInfo = provider->modelInfo();

                auto text = std::string {};
                for (auto const prov: agent::KnownProviders)
                {
                    auto const models = agent::modelsForProvider(prov);
                    if (models.empty())
                        continue;
                    text += std::format("{}:\n", prov);
                    for (auto const model: models)
                    {
                        auto const isActive = (prov == activePName && model == activeModelInfo.modelName);
                        text += std::format("  {}{}\n", model, isActive ? "  [active]" : "");
                    }
                }
                text += "\nType /model <name> to switch.\n";
                return agent::DirectOutput { .text = std::move(text) };
            }

            // Find the model by name.
            auto const& activePName = _shell._agentProviderFactory->activeProviderName();
            auto const match = agent::findModelByName(trimmedArgs, activePName);
            if (!match)
            {
                auto text = std::format("No model matching '{}' found.\n\nAvailable models:\n", trimmedArgs);
                for (auto const& m: agent::allKnownModels())
                    text += std::format("  {} ({})\n", m.modelName, m.providerName);
                return agent::DirectOutput { .text = std::move(text) };
            }

            // Check if the target provider is authenticated.
            auto const authenticated = _shell._agentProviderFactory->authenticatedProviders();
            auto const isAuth =
                std::ranges::find(authenticated, std::string(match->providerName)) != authenticated.end();
            if (!isAuth)
            {
                return agent::DirectOutput {
                    .text = std::format("Provider '{}' is not authenticated.\n"
                                        "Run `endo agent login` to configure it.\n",
                                        match->providerName),
                };
            }

            // Capture old model info, switch, capture new.
            auto const oldInfo = provider->modelInfo();
            if (!switchToModel(match->providerName, match->modelName))
                return agent::DirectOutput { .text = "Failed to switch model.\n" };
            auto const newInfo = provider->modelInfo();

            return agent::MarkdownOutput { .markdown = agent::formatCapabilityDiff(oldInfo, newInfo) };
        }));

    registry.registerCommand(std::make_unique<agent::CallbackSlashCommand>(
        "paste-image",
        "Paste image from system clipboard",
        [&](std::string_view /*args*/) -> agent::SlashCommandResult {
            if (inputComponent.imageCount() >= 5)
                return agent::DirectOutput { .text = "Maximum of 5 images already attached.\n" };

            auto clipboardImage = tui::readClipboardImage();
            if (!clipboardImage)
                return agent::DirectOutput {
                    .text = "No image found in clipboard. Ensure an image is copied and the clipboard tool "
                            "(wl-paste or xclip) is installed.\n"
                };

            auto const mediaType = clipboardImage->mediaType;
            auto const sizeKB = clipboardImage->data.size() / 1024;
            inputComponent.attachImage(std::move(clipboardImage->data), std::string(mediaType));
            return agent::DirectOutput {
                .text = std::format("Attached image from clipboard ({}, {} KB).\n", mediaType, sizeKB),
            };
        }));

    registry.registerCommand(std::make_unique<agent::CallbackSlashCommand>(
        "remove-image", "Remove attached images", [&](std::string_view args) -> agent::SlashCommandResult {
            if (inputComponent.imageCount() == 0)
                return agent::DirectOutput { .text = "No images attached.\n" };

            if (args.empty() || args == "all")
            {
                auto const count = inputComponent.imageCount();
                inputComponent.clearImages();
                return agent::DirectOutput {
                    .text = std::format("Removed {} attached image{}.\n", count, count == 1 ? "" : "s"),
                };
            }

            auto index = size_t { 0 };
            auto const [ptr, ec] = std::from_chars(args.data(), args.data() + args.size(), index);
            if (ec != std::errc {} || index >= inputComponent.imageCount())
                return agent::DirectOutput {
                    .text = std::format("Invalid image index. Use 0-{} or 'all'.\n",
                                        inputComponent.imageCount() - 1),
                };
            inputComponent.removeImage(index);
            return agent::DirectOutput { .text = std::format("Removed image {}.\n", index) };
        }));
}
} // namespace endo
