// SPDX-License-Identifier: Apache-2.0
#include <shell/AgentModeSession.hpp>
#include <shell/Shell.hpp>

#include <tui/Buffer.hpp>
#include <tui/Canvas.hpp>
#include <tui/Screen.hpp>
#include <tui/Theme.hpp>

#include <agent/AgentConfig.hpp>
#include <agent/commands/AgentHistoryProvider.hpp>
#include <agent/commands/FilePathCompleter.hpp>
#include <agent/commands/SlashCommand.hpp>
#include <agent/commands/SlashCommandRegistry.hpp>
#include <agent/context/FileReferenceExpander.hpp>
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

LoopControl AgentModeSession::handleInputEvent(
    tui::InputEvent const& event,
    bool& needsRedraw,
    agent::SlashCommandRegistry const& slashRegistry,
    std::function<bool(std::string_view, std::string_view)> const& switchToModel,
    std::function<void()> const& saveHistory)
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
} // namespace endo
