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

#include <shell/AgentContext.hpp>

#include <tui/InputEvent.hpp>
#include <tui/QuestionComponent.hpp>
#include <tui/Terminal.hpp>

#include <cstdint>
#include <filesystem>
#include <future>
#include <optional>
#include <set>
#include <string>
#include <string_view>
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

namespace agent
{
    class AgentWorker;
    class LlmProvider;
    class SessionManager;
    class ToolRegistry;
    class AgentHistoryProvider;
    class FilePathCompleter;
    class SlashCommandRegistry;
    class PermissionManager;
    class ConversationHistoryStore;
    struct SessionMetadata;

    namespace mcp
    {
        class ServerManager;
    }
} // namespace agent

/// Result of handling one input event: whether the agent-mode loop continues.
enum class LoopControl : std::uint8_t
{
    Continue, ///< Keep running the loop.
    Exit,     ///< Leave agent mode (the user aborted).
};

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
    /// @param worker The agent worker (inbound/outbound message queues).
    /// @param provider The active LLM provider (reference, so model switches are visible).
    /// @param sessionManager The persisted-session manager.
    /// @param toolRegistry The tool registry.
    /// @param mcpServerManager The MCP server manager.
    /// @param historyProvider The input history provider (not owned).
    /// @param filePathProvider The @-file completion provider (not owned).
    /// @param contextFuture The pending background agent-context build.
    /// @param cwd The working directory the context was built for.
    /// @param permissionManager The tool-permission manager.
    /// @param historyStore The on-disk conversation history store.
    AgentModeSession(Shell& shell,
                     tui::TerminalOutput& out,
                     tui::Terminal& terminal,
                     tui::Screen& screen,
                     agent::AgentInputComponent& inputComponent,
                     agent::ToolStatusComponent& toolStatusComponent,
                     agent::AgentWorker& worker,
                     agent::LlmProvider*& provider,
                     agent::SessionManager const& sessionManager,
                     agent::ToolRegistry& toolRegistry,
                     agent::mcp::ServerManager& mcpServerManager,
                     agent::AgentHistoryProvider* historyProvider,
                     agent::FilePathCompleter* filePathProvider,
                     std::future<AgentContextResult>& contextFuture,
                     std::filesystem::path cwd,
                     agent::PermissionManager& permissionManager,
                     agent::ConversationHistoryStore const& historyStore) noexcept:
        _shell(shell),
        _out(out),
        _terminal(terminal),
        _screen(screen),
        _inputComponent(inputComponent),
        _toolStatusComponent(toolStatusComponent),
        _worker(worker),
        _provider(provider),
        _sessionManager(sessionManager),
        _toolRegistry(toolRegistry),
        _mcpServerManager(mcpServerManager),
        _historyProviderPtr(historyProvider),
        _filePathProviderPtr(filePathProvider),
        _contextFuture(contextFuture),
        _cwd(std::move(cwd)),
        _permissionManager(permissionManager),
        _historyStore(historyStore)
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

    /// Re-lays out and redraws the agent input line to the screen, sizing it to
    /// the terminal width and the component's current preferred height. Used after
    /// a prompt closes or a response completes, to return to the input line.
    void redrawInputComponent();

    /// Clears the streaming prompt, restoring the cursor to the content end.
    void clearStreamingPrompt();

    /// Renders the streaming prompt below the current content position.
    void renderStreamingPrompt();

    /// Processes a drained batch of agent worker messages (section 1 of the loop):
    /// streaming tokens/tool status, plan progress, ask-user / permission requests,
    /// completion, and shutdown — updating streaming state and the inline prompts.
    /// @param messages The messages drained from the worker's outbound queue.
    /// @param modelInfo The active model info (for cost / context-size display).
    void drainAgentMessages(std::vector<agent::FromAgentMessage>& messages,
                            agent::ModelInfo const& modelInfo);

    /// Ensures the system prompt and project context are applied, blocking on the
    /// background context build the first time it is needed.
    void ensureSystemPromptReady();

    /// Processes one terminal input event (section 4 of the loop): routes to the
    /// active inline prompt, or to the agent input component (submit, abort, mode
    /// cycling, slash commands, etc.).
    /// @param event The input event to handle (never a resize).
    /// @param needsRedraw Set to true if the caller should redraw after handling.
    /// @param slashRegistry The slash-command registry (for `/command` lookup).
    /// @return LoopControl::Exit to leave agent mode, otherwise LoopControl::Continue.
    [[nodiscard]] LoopControl handleInputEvent(tui::InputEvent const& event,
                                               bool& needsRedraw,
                                               agent::SlashCommandRegistry const& slashRegistry);

    /// Switches the active LLM provider/model, restarting the worker.
    /// @param targetProvider The provider name (claude, openai, openai_compat, gemini).
    /// @param targetModel The model name to activate.
    /// @return True on success, false if the provider name is unknown.
    bool switchToModel(std::string_view targetProvider, std::string_view targetModel);

    /// Persists the conversation history to disk and the active named session.
    void saveHistory();

    /// Registers the agent-mode slash commands (`/reset`, `/clear`, `/sessions`,
    /// `/model`, etc.) into @p registry.
    /// @param registry The slash-command registry to populate.
    void registerSlashCommands(agent::SlashCommandRegistry& registry);

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
    /// Builds the metadata for the active conversation, used when persisting to a
    /// named session. The created-at timestamp falls back to "now" only when the
    /// session has none yet.
    /// @param name The session name to record in the metadata.
    /// @return The populated session metadata.
    [[nodiscard]] agent::SessionMetadata buildSessionMetadata(std::string name) const;

    /// Echoes an answered/cancelled ask-user question to the scrollback as a
    /// bordered question + option list. Shared by the confirmed and cancelled
    /// input paths so the rendering lives in one place.
    /// @param config The question configuration (text and options).
    /// @param highlighted Indices of options to draw selected (▶, bold); empty for none.
    /// @param otherAnswer A custom "Other..." answer to append as a selected row, if any.
    /// @param notice A trailing dim notice (e.g. "(cancelled)") to append, if any.
    void echoQuestionToScrollback(tui::QuestionConfig const& config,
                                  std::set<std::size_t> const& highlighted,
                                  std::optional<std::string_view> otherAnswer,
                                  std::optional<std::string_view> notice);

    Shell& _shell;                                        ///< Owning shell (befriended for agent members).
    tui::TerminalOutput& _out;                            ///< Terminal output.
    tui::Terminal& _terminal;                             ///< Terminal.
    tui::Screen& _screen;                                 ///< TUI screen.
    agent::AgentInputComponent& _inputComponent;          ///< Agent input line component.
    agent::ToolStatusComponent& _toolStatusComponent;     ///< Tool-status component.
    agent::AgentWorker& _worker;                          ///< Agent worker message queues.
    agent::LlmProvider*& _provider;                       ///< Active provider (ref: model switches visible).
    agent::SessionManager const& _sessionManager;         ///< Persisted-session manager.
    agent::ToolRegistry& _toolRegistry;                   ///< Tool registry.
    agent::mcp::ServerManager& _mcpServerManager;         ///< MCP server manager.
    agent::AgentHistoryProvider* _historyProviderPtr;     ///< Input history provider (not owned).
    agent::FilePathCompleter* _filePathProviderPtr;       ///< @-file completion provider (not owned).
    std::future<AgentContextResult>& _contextFuture;      ///< Pending background context build.
    std::filesystem::path _cwd;                           ///< Working directory the context was built for.
    agent::PermissionManager& _permissionManager;         ///< Tool-permission manager.
    agent::ConversationHistoryStore const& _historyStore; ///< On-disk conversation history store.
};

} // namespace endo
