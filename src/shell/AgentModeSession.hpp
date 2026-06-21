// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file
/// `AgentModeSession` — owns the mutable per-run state of the interactive agent
/// mode loop and borrows the heavy collaborators that `Shell::runAgentModeFlow`
/// constructs. Extracting this state + the loop's helper lambdas/sections into a
/// dedicated object keeps `runAgentModeFlow` small enough to satisfy the
/// `readability-function-size` check, instead of disabling the check shell-wide.
///
/// Lifetime: a session is constructed on the stack inside `runAgentModeFlow`,
/// after every collaborator it borrows, and destroyed when that function returns.
/// It therefore holds references/pointers (not ownership) to those collaborators;
/// the function remains their owner.

#include <tui/QuestionComponent.hpp>
#include <tui/Terminal.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <agent/Plan.hpp>
#include <agent/Types.hpp>
#include <agent/session/AgentMessages.hpp>
#include <agent/ui/AgentInputComponent.hpp>
#include <agent/ui/AgentResponseRenderer.hpp>
#include <agent/ui/ToolStatusComponent.hpp>

namespace endo
{

class Shell;

/// An inline TUI prompt (question / permission / picker / approval) rendered
/// below the agent input line. Self-contained: its methods take the terminal
/// output / terminal as parameters, so it captures no outer state.
struct InlinePrompt
{
    bool active = false;                             ///< Whether this prompt is awaiting input.
    std::optional<tui::QuestionComponent> component; ///< The question component, when active.
    bool visible = false;                            ///< Whether it is currently drawn on screen.
    std::uint64_t requestId = 0;                     ///< The agent request this prompt answers.

    /// Clears the rendered prompt, restoring the cursor to the content position.
    /// @param output The terminal output to write the clear sequence to.
    void clear(tui::TerminalOutput& output);

    /// Renders the prompt below the current content position.
    /// @param output The terminal output.
    /// @param terminal The terminal (for the current column width).
    void render(tui::TerminalOutput& output, tui::Terminal const& terminal);

    /// Resets the prompt to the inactive state.
    void reset();

    /// @return True if active and a component is present.
    [[nodiscard]] auto isActive() const -> bool;
};

/// Owns the agent-mode loop's mutable state and helper behavior.
///
/// Constructed once per `runAgentModeFlow` invocation with references to the
/// collaborators that function owns. The function delegates the loop's helper
/// lambdas (now methods) and, in later stages, its body sections to this object.
class AgentModeSession
{
  public:
    /// @param shell The owning shell (for its agent members; befriended).
    /// @param out The terminal output.
    /// @param terminal The terminal.
    /// @param screen The TUI screen.
    /// @param inputComponent The agent input line component.
    /// @param toolStatusComponent The tool-status component.
    AgentModeSession(Shell& shell,
                     tui::TerminalOutput& out,
                     tui::Terminal& terminal,
                     tui::Screen& screen,
                     agent::AgentInputComponent& inputComponent,
                     agent::ToolStatusComponent& toolStatusComponent) noexcept:
        _shell(shell),
        _out(out),
        _terminal(terminal),
        _screen(screen),
        _inputComponent(inputComponent),
        _toolStatusComponent(toolStatusComponent)
    {
    }

    AgentModeSession(AgentModeSession const&) = delete;
    AgentModeSession& operator=(AgentModeSession const&) = delete;
    AgentModeSession(AgentModeSession&&) = delete;
    AgentModeSession& operator=(AgentModeSession&&) = delete;
    ~AgentModeSession() = default;

    /// @return True if any inline prompt is active.
    [[nodiscard]] bool anyPromptActive() const noexcept;

    /// Tears down streaming state after a response completes.
    void teardownStreaming();

    /// Renders the input component to an off-screen buffer and writes it out.
    void renderComponentDirect();

    /// Renders the tool-status component to an off-screen buffer and writes it out.
    void renderToolStatusDirect();

    /// Clears the streaming prompt, restoring the cursor to the content end.
    void clearStreamingPrompt();

    /// Renders the streaming prompt below the current content position.
    void renderStreamingPrompt();

    /// Processes a drained batch of agent worker messages (section 1 of the loop):
    /// streaming tokens/tool status, plan progress, ask-user / permission requests,
    /// completion, and shutdown — updating streaming state and the inline prompts.
    /// @param messages The messages drained from the worker's outbound queue.
    /// @param modelInfo The active model info (for cost / context-size display).
    /// @param saveHistory Callback persisting conversation history after relevant events.
    void drainAgentMessages(std::vector<agent::FromAgentMessage>& messages,
                            agent::ModelInfo const& modelInfo,
                            std::function<void()> const& saveHistory);

    /// @name Owned per-run loop state (public: accessed by runAgentModeFlow during the migration).
    /// @{
    bool streaming = false;              ///< A response is currently streaming.
    bool streamCancelled = false;        ///< The current stream was cancelled.
    bool streamingPromptVisible = false; ///< The streaming prompt is drawn.
    bool planModeActive = false;         ///< Plan mode is active.
    bool systemPromptReady = false;      ///< The system prompt has been built/applied.
    std::optional<agent::AgentResponseRenderer> currentRenderer; ///< Active response renderer.
    agent::AgentResponseRenderer* activeRenderer = nullptr;      ///< Non-owning view of the renderer.
    std::optional<agent::Plan> pendingPlan;                      ///< Plan awaiting user approval.
    std::vector<std::string> sessionPickerNames;                 ///< Names backing the session picker.
    InlinePrompt askUserPrompt;                                  ///< The ask-user question prompt.
    InlinePrompt permissionPrompt;                               ///< The permission prompt.
    InlinePrompt sessionPickerPrompt;                            ///< The session picker prompt.
    InlinePrompt planApprovalPrompt;                             ///< The plan-approval prompt.
                                                                 /// @}

  private:
    Shell& _shell;                                    ///< Owning shell (befriended for agent members).
    tui::TerminalOutput& _out;                        ///< Terminal output.
    tui::Terminal& _terminal;                         ///< Terminal.
    tui::Screen& _screen;                             ///< TUI screen.
    agent::AgentInputComponent& _inputComponent;      ///< Agent input line component.
    agent::ToolStatusComponent& _toolStatusComponent; ///< Tool-status component.
};

} // namespace endo
