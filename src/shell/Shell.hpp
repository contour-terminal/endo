// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <shell/ProcessGroup.hpp>

#include <endo-language/codegen/IRGenerator.hpp>

#include <http/HttpClient.hpp>

#include <tui/SemanticBlockClient.hpp>

#include <CoreVM/CoreVM.hpp>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <print>
#include <set>
#include <string>
#include <vector>

#include <agent/AgentConfig.hpp>
#include <agent/context/ProjectContextLoader.hpp>
#include <platform/EnvironmentProvider.hpp>
#include <platform/Wakeup.hpp>

namespace endo::agent
{
class AgentSession;
class AgentWorker;
class ProviderFactory;
struct AgentRunOptions;
} // namespace endo::agent

#include <shell/completion/Completer.hpp>
#include <shell/completion/CompleterFunctionRegistry.hpp>
#include <shell/completion/ScriptedCompleter.hpp>
#include <shell/history/History.hpp>
#include <shell/history/PersistentHistory.hpp>
#include <shell/output/OutputDefinitionRegistry.hpp>
#include <shell/ui/Prompt.hpp>

#include "Job.hpp"
#include "TTY.hpp"
#include <agent/mcp/ServerManager.hpp>
#include <agent/tools/WebSearchTool.hpp>
#include <platform/Pipe.hpp>
#include <platform/Process.hpp>
#include <platform/SignalHandler.hpp>

namespace endo
{

std::string readLine(TTY& tty, std::string_view prompt);

/// Options for the read builtin command
struct ReadOptions
{
    std::string prompt;                               ///< -p PROMPT
    bool rawMode = false;                             ///< -r (no backslash escape)
    bool silent = false;                              ///< -s (no echo)
    std::optional<size_t> maxChars;                   ///< -n NCHARS
    std::optional<std::chrono::milliseconds> timeout; ///< -t SECONDS
    char delimiter = '\n';                            ///< -d DELIM
    std::vector<std::string> variableNames;           ///< VAR1 VAR2 ...
};

class Shell final: public SignalCallback
{
  public:
    Shell();
    ~Shell() override;

    Shell(TTY& tty, EnvironmentProvider& env);

    [[nodiscard]] EnvironmentProvider& environment() noexcept;
    [[nodiscard]] EnvironmentProvider const& environment() const noexcept;

    void setOptimize(bool optimize);

    /// Set check-only mode (compile without executing).
    void setCheckOnly(bool checkOnly) noexcept { _checkOnly = checkOnly; }

    /// Enable or disable unused-value detection for F# bindings.
    void setUnusedValueDetection(bool enabled) noexcept { _unusedValueDetection = enabled; }

    /// @brief Sets the trace file path for agent tool I/O tracing.
    /// @param path File path for JSONL trace output. Empty triggers auto-generated path.
    void setAgentTracePath(std::string path);

    /// Set interactive mode (controls prompts, job notifications, etc.)
    void setInteractive(bool interactive);

    /// Set positional parameters ($0, $1, $2, ...)
    void setPositionalParameters(std::vector<std::string> params);

    int run();
    /// @brief Executes a line of shell/F# code.
    /// @param lineBuffer The source code to execute.
    /// @param sourceName The source name for error messages (defaults to "stdin").
    int execute(std::string const& lineBuffer, std::string_view sourceName = "stdin");

    /// @brief Executes a line of shell/F# code with an external diagnostic report.
    /// @param lineBuffer The source code to execute.
    /// @param report The diagnostic report to use (errors will be reported here).
    /// @param sourceName The source name for error messages (defaults to "stdin").
    int execute(std::string const& lineBuffer,
                CoreVM::diagnostics::Report& report,
                std::string_view sourceName = "stdin");

    /// @brief Loads ~/.config/endo/init.endo and agent config if they exist.
    /// Call after construction for non-interactive modes that need shell config.
    void loadInitScript();

    /// @brief Runs the agent in headless/batch mode (no TUI).
    /// @param options Parsed command-line options for the headless run.
    /// @return Exit code (0 = success, non-zero = failure).
    int runAgentHeadless(agent::AgentRunOptions const& options);

    /// Updates LINES and COLUMNS environment variables from current TTY size.
    void updateTerminalSizeEnv();

    /// Called when SIGCHLD is received to reap child processes.
    void onSigchld() override;

    /// Called when SIGTSTP is received to suspend the shell.
    ///
    /// This is triggered when the parent shell sends SIGTSTP (e.g., via kill -TSTP)
    /// or when the user presses Ctrl+Z while the shell is a foreground process in
    /// another terminal.
    ///
    /// The shell will:
    /// 1. Restore terminal to cooked mode (disable raw mode and protocols)
    /// 2. Re-raise SIGTSTP with default handling to actually stop
    /// 3. After resume, restore terminal to raw mode and redraw
    void onSigtstp() override;

    /// Called when SIGCONT is received after being stopped.
    ///
    /// This is triggered when the shell is resumed after being stopped.
    /// The shell will restore terminal state and redraw the prompt.
    void onSigcont() override;

    /// Reports status of completed/stopped background jobs to the user.
    void reportJobStatus();

    Prompt prompt;
    std::vector<ProcessGroup> processGroups;
    JobTable jobTable;                    ///< Table of background jobs
    PersistentHistory history;            ///< Command history for completion (persisted to disk)
    std::unique_ptr<Completer> completer; ///< Completion system

    /// Agent configuration loaded from agent.yml (API keys) and overridden by init.endo builtins.
    agent::AgentConfig agentConfig;

    /// Web search configuration for the agent web_search tool.
    /// Configurable at runtime via set_web_search_* builtins.
    agent::WebSearchConfig webSearchConfig;

    /// MCP server configurations collected from init.endo builtins.
    /// Servers are spawned when entering agent mode.
    std::vector<agent::mcp::McpServerConfig> mcpServerConfigs;

  private:
    // --- Registration (builtins/Registration.cpp) ---
    void registerBuiltinFunctions();
    void registerEnvironmentBuiltins();
    void registerProcessBuiltins();
    void registerIOBuiltins();
    void registerCommandBuilderBuiltins();
    void registerExpansionBuiltins();
    void registerFlowControlBuiltins();
    void registerJobControlBuiltins();
    void registerUserCommandBuiltins();
    void registerOutputBuiltins();
    void registerLanguageBuiltins();
    void registerStructuredBuiltins();
    void registerPromptBuiltins();
    void registerAgentConfigBuiltins();
    void registerMcpBuiltins();
    void registerCompleterBuiltins();

    /// @brief Loads .endo completer scripts from user and system directories.
    void loadCompleters();

    /// @brief Executes a registered completer function and returns completion strings and errors.
    /// @param funcName The function name to invoke.
    /// @param args Tokens after the command, excluding the current word.
    /// @param prefix The current word being typed.
    /// @return Completions and any compilation/link errors.
    [[nodiscard]] CompleterExecutionResult executeCompleterFunction(std::string_view funcName,
                                                                    std::vector<std::string> const& args,
                                                                    std::string_view prefix);

    // --- Inline command implementations (builtins/InlineCommands.cpp) ---
    /// Executes the echo builtin, writing to outputFd. Returns exit code.
    [[nodiscard]] int executeInlineEcho(CoreVM::CoreStringArray const& args, NativeHandle outputFd);
    /// Executes the cat builtin, writing to outputFd. Returns exit code.
    [[nodiscard]] int executeInlineCat(CoreVM::CoreStringArray const& args,
                                       NativeHandle outputFd,
                                       NativeHandle stdinFd);
    /// Executes the sleep builtin, writing help to outputFd. Returns exit code.
    [[nodiscard]] int executeInlineSleep(CoreVM::CoreStringArray const& args, NativeHandle outputFd);
    /// Executes the rm builtin, writing verbose output to outputFd. Returns exit code.
    [[nodiscard]] int executeInlineRm(CoreVM::CoreStringArray const& args, NativeHandle outputFd);
    /// Executes the mkdir builtin, writing verbose output to outputFd. Returns exit code.
    [[nodiscard]] int executeInlineMkdir(CoreVM::CoreStringArray const& args, NativeHandle outputFd);
    /// Executes the cp builtin, writing verbose output to outputFd. Returns exit code.
    [[nodiscard]] int executeInlineCp(CoreVM::CoreStringArray const& args, NativeHandle outputFd);
    /// Executes the mv builtin, writing verbose output to outputFd. Returns exit code.
    [[nodiscard]] int executeInlineMv(CoreVM::CoreStringArray const& args, NativeHandle outputFd);
    /// Executes the find builtin, writing matching paths to outputFd. Returns exit code.
    [[nodiscard]] int executeInlineFind(CoreVM::CoreStringArray const& args, NativeHandle outputFd);
    /// Executes the grep builtin, writing matching lines to outputFd. Returns exit code.
    [[nodiscard]] int executeInlineGrep(CoreVM::CoreStringArray const& args,
                                        NativeHandle outputFd,
                                        NativeHandle stdinFd);
    /// Finalizes a pipeline builtin: closes pipe, tracks command, waits for downstream.
    void finalizePipelineBuiltin(bool lastInChain,
                                 CoreVM::CoreStringArray const& args,
                                 std::string_view programName,
                                 CoreVM::Params& context);

    // --- Process execution (builtins/ProcessExecution.cpp) ---
    void builtinCallProcess(CoreVM::Params& context);
    void builtinCallProcessShellPiped(CoreVM::Params& context);

    // --- Environment builtins (builtins/Environment.cpp) ---
    void builtinExit(CoreVM::Params& context);
    void builtinChDir(CoreVM::Params& context);
    void builtinChDirHome(CoreVM::Params& context);
    void builtinSet(CoreVM::Params& context);
    void builtinUnset(CoreVM::Params& context);
    void builtinGetVar(CoreVM::Params& context);
    void builtinGetExitStatus(CoreVM::Params& context);
    void builtinSetExitStatus(CoreVM::Params& context);
    void builtinGetProcessId(CoreVM::Params& context);
    void builtinGetBackgroundId(CoreVM::Params& context);
    void builtinGetPositional(CoreVM::Params& context);
    void builtinSetAndExport(CoreVM::Params& context);
    void builtinExport(CoreVM::Params& context);

    // --- Command builder (builtins/CommandBuilder.cpp) ---
    void builtinCmdStart(CoreVM::Params& context);
    void builtinCmdArg(CoreVM::Params& context);
    void builtinCmdExec(CoreVM::Params& context);
    void builtinCmdExecPiped(CoreVM::Params& context);
    std::vector<std::string>& cmdBuilderArgs();
    void cleanupProcSubst();
    void applyRedirects(SpawnConfig& config);
    [[nodiscard]] std::expected<std::filesystem::path, ShellError> resolveProgram(
        std::string const& program) const;

    /// Result of running a command in the foreground with job control.
    struct ForegroundResult
    {
        int exitCode = 0;     ///< Exit code if process terminated
        bool stopped = false; ///< True if process was stopped (Ctrl+Z)
        ProcessId pid = 0;    ///< Process ID of the child
        ProcessId pgid = 0;   ///< Process group ID
    };

    [[nodiscard]] std::expected<ForegroundResult, ShellError> runForeground(SpawnConfig& config,
                                                                            std::string const& command);

    // --- Redirect builtins (builtins/Redirects.cpp) ---
    void builtinOpenRead(CoreVM::Params& context);
    void builtinOpenWrite(CoreVM::Params& context);
    void builtinRedirectStart(CoreVM::Params& context);
    void builtinRedirectInput(CoreVM::Params& context);
    void builtinRedirectOutput(CoreVM::Params& context);
    void builtinRedirectFdDup(CoreVM::Params& context);
    void builtinRedirectHeredoc(CoreVM::Params& context);
    void builtinRedirectHerestring(CoreVM::Params& context);
    void builtinRedirectEnd(CoreVM::Params& context);

    // --- Substitution builtins (builtins/Substitution.cpp) ---
    void builtinSubstStart(CoreVM::Params& context);
    void builtinSubstEnd(CoreVM::Params& context);
    void builtinProcSubstFork(CoreVM::Params& context);
    void builtinProcSubstExit(CoreVM::Params& context);
    void builtinProcSubstGetPath(CoreVM::Params& context);
    void builtinProcSubstCleanup(CoreVM::Params& context);

    // --- Expansion builtins (builtins/Expansion.cpp) ---
    void builtinExpandTilde(CoreVM::Params& context);
    void builtinExpandTildeUser(CoreVM::Params& context);
    void builtinExpandGlob(CoreVM::Params& context);
    void builtinArithToString(CoreVM::Params& context);
    void builtinArithGetVar(CoreVM::Params& context);
    void builtinArithPow(CoreVM::Params& context);
    void builtinExpandParamLength(CoreVM::Params& context);
    void builtinExpandParamDefault(CoreVM::Params& context);
    void builtinExpandParamAlternate(CoreVM::Params& context);
    void builtinExpandParamAssign(CoreVM::Params& context);
    void builtinExpandParamError(CoreVM::Params& context);
    void builtinExpandParamRemovePrefix(CoreVM::Params& context);
    void builtinExpandParamRemoveSuffix(CoreVM::Params& context);
    void builtinExpandParamReplace(CoreVM::Params& context);

    // --- Flow control builtins (builtins/FlowControl.cpp) ---
    void builtinForInit(CoreVM::Params& context);
    void builtinForAddItem(CoreVM::Params& context);
    void builtinForHasMore(CoreVM::Params& context);
    void builtinForNext(CoreVM::Params& context);
    void builtinForCleanup(CoreVM::Params& context);
    void builtinCaseMatch(CoreVM::Params& context);
    void builtinFunctionRegister(CoreVM::Params& context);
    void builtinFunctionCall(CoreVM::Params& context);

    // --- Job control builtins (builtins/JobControl.cpp) ---
    void builtinJobs(CoreVM::Params& context);
    void builtinFg(CoreVM::Params& context);
    void builtinBg(CoreVM::Params& context);
    void builtinWait(CoreVM::Params& context);
    void builtinCmdExecPipedBackground(CoreVM::Params& context);

    // --- Read command builtins (builtins/ReadCommand.cpp) ---
    void builtinReadDefault(CoreVM::Params& context);
    void builtinRead(CoreVM::Params& context);
    [[nodiscard]] std::string readInputLine(NativeHandle inputFd, ReadOptions const& options);
    [[nodiscard]] std::vector<std::string> splitByIFS(std::string_view input) const;

    // --- User commands (builtins/UserCommands.cpp) ---
    void builtinBind(CoreVM::Params& context);
    void builtinWhich(CoreVM::Params& context);

    // --- Shared helpers ---

    /// @brief Renders markdown help text to a file descriptor.
    ///
    /// When outputFd is a TTY, uses tui::MarkdownRenderer for styled output;
    /// otherwise falls back to raw text.
    /// @param outputFd File descriptor to write to.
    /// @param markdownContent The markdown text to render.
    /// @return Always 0.
    [[nodiscard]] static int renderMarkdownHelp(NativeHandle outputFd, std::string_view markdownContent);

    // --- Output builtins (builtins/Output.cpp) ---
    void builtinPrint(CoreVM::Params& context);
    void builtinPrintln(CoreVM::Params& context);
    void builtinDisplayResult(CoreVM::Params& context);
    void builtinMarkdownRender(CoreVM::Params& context);
    void builtinFetch(CoreVM::Params& context);
    void builtinFetchWithHeaders(CoreVM::Params& context);

    // --- Core shell methods ---
    void trace(CoreVM::Instruction instr, size_t ip, size_t sp);

    // Shell integration (OSC 133) and CWD propagation (OSC 7)
    void emitPromptStart();
    void emitPromptEnd();
    void emitCommandStart();
    void emitCommandFinished(int exitCode);
    void emitCurrentWorkingDirectory();

    template <typename... Args>
    void error(std::format_string<Args...> const& message, Args&&... args)
    {
        std::println(std::cerr, "{}", std::format(message, std::forward<Args>(args)...));
    }

    // --- Agent mode ---
    void runAgentMode(std::optional<std::string> initialMessage = std::nullopt);

    /// @brief Offers error recovery after a failed command.
    /// @param exitCode The exit code of the failed command.
    /// @param command The command text that failed.
    void offerErrorRecovery(int exitCode, std::string const& command);

    std::unique_ptr<tui::SemanticBlockClient> _semanticBlockClient;
    agent::ErrorRecoveryAction _sessionErrorRecoveryOverride =
        agent::ErrorRecoveryAction::Ignore; ///< Session-level override (set by user choice).
    bool _hasSessionOverride = false;       ///< Whether the user made a session-level choice.

    std::unique_ptr<http::HttpClient> _agentHttpClient;
    std::unique_ptr<agent::ProviderFactory> _agentProviderFactory;
    std::unique_ptr<agent::AgentSession> _agentSession;
    platform::Wakeup _agentWakeup; ///< Wakeup primitive for agent event loop integration.
    std::optional<agent::ProjectContext>
        _cachedProjectContext;                      ///< Cached project context for agent mode re-entry.
    std::filesystem::path _cachedProjectContextCwd; ///< CWD associated with cached project context.
    std::optional<std::string> _agentTracePath; ///< Trace file path for agent tool I/O (nullopt = disabled).
    std::string _activeSessionName; ///< Name of the active agent session (persists across re-entries).
    std::chrono::system_clock::time_point _sessionCreatedAt; ///< Creation time of the active session.

    CoreVM::Runtime _runtime;
    EnvironmentProvider& _env;
    TTY& _tty;
    FSharpPersistentState _fsharpState;            ///< F# function definitions persisted across REPL prompts
    OutputDefinitionRegistry _outputDefinitions;   ///< Output definition registry for structured pipelines
    CompleterFunctionRegistry _completerFunctions; ///< Scripted completer function registry

    ProcessManager& _processManager;

    std::unique_ptr<CoreVM::Program> _currentProgram;
    CoreVM::Runner::Globals _globals;

    bool _optimize = false;
    bool _checkOnly = false;

    struct PipelineBuilder
    {
        struct IODescriptors
        {
            NativeHandle reader;
            NativeHandle writer;
        };

        NativeHandle defaultStdinFd = InvalidHandle;
        NativeHandle defaultStdoutFd = InvalidHandle;
        std::unique_ptr<Pipe> currentPipe = nullptr;
        NativeHandle lastReleasedReaderFd = InvalidHandle;

        auto requestShellPipe(bool lastInChain) -> IODescriptors;

        /// Close the current pipe's writer (for builtin commands that write to pipe)
        void closeCurrentPipeWriter();

        /// Close pipe file descriptors retained by the parent after child process spawn.
        void closePipeFdsInParent();
    };

    PipelineBuilder _currentPipelineBuilder;

    std::vector<ProcessId> _currentProcessGroupPids;
    std::vector<std::string> _pipelineCommands; ///< Commands in current pipeline for job table display
    std::optional<ProcessId> _leftPid;
    std::optional<ProcessId> _rightPid;

    int _exitCode = -1;
    std::chrono::milliseconds _lastCommandDuration { 0 }; ///< Duration of the last command
    bool _interactive = true;                             ///< Whether running in interactive mode
    bool _unusedValueDetection = false;                   ///< Detect unused F# bindings (script mode only)
    bool _lsIcons = true;                                 ///< Show Nerd Font icons in ls output
    bool _lsDirectorySlash = true;                        ///< Append trailing '/' to directory names
    ProcessId _shellPid = 0;
    ProcessId _shellPgid = 0; ///< Shell's process group ID
    int _signalFd = -1;       ///< signalfd for Linux, -1 otherwise
    int _shellLevel = 0;      ///< Shell nesting depth (0 = outermost)
    std::optional<ProcessId> _lastBackgroundPid;
    std::vector<std::string> _positionalParameters;
    std::vector<std::vector<std::string>> _cmdBuilderStack;

    struct RedirectState
    {
        enum class Type
        {
            InputFile,
            OutputFile,
            FdDup,
            HereDoc,
            HereString,
        };

        struct Entry
        {
            Type type;
            int sourceFd = -1;
            int targetFd = -1;
            std::string path;
            std::string content;
            bool append = false;
            NativeHandle openedFd = InvalidHandle;
        };

        std::vector<Entry> entries;

        void clear();

        /// Get the effective stdout fd considering output redirects.
        /// Opens the redirect file if needed.
        [[nodiscard]] NativeHandle getEffectiveStdoutFd(NativeHandle defaultFd, ProcessManager& pm);

        /// Get the effective stdin fd considering input redirects.
        /// Opens the redirect file if needed.
        [[nodiscard]] NativeHandle getEffectiveStdinFd(NativeHandle defaultFd, ProcessManager& pm);

        void addInputFile(int targetFd, std::string path);
        void addOutputFile(int sourceFd, std::string path, bool append);
        void addFdDup(int sourceFd, int targetFd);
        void addHereDoc(int targetFd, std::string content);
        void addHereString(int targetFd, std::string content);
    };

    RedirectState _redirectState;

    struct SubstitutionCapture
    {
        std::unique_ptr<Pipe> pipe;
        NativeHandle savedStdout = InvalidHandle;
        std::string output;

        void clear();
    };

    std::optional<SubstitutionCapture> _substitutionCapture;

    std::vector<std::unique_ptr<Pipe>> _processSubstitutionPipes;
    std::string _procSubstFdPath;
    std::vector<ProcessId> _procSubstChildPids;
    std::vector<NativeHandle> _procSubstExposedFds;

    struct ForLoopState
    {
        std::string variable;
        std::vector<std::string> items;
        size_t index = 0;
    };

    std::vector<ForLoopState> _forLoopStack;
    std::set<std::string> _registeredFunctions;

    CoreVM::Runner* _runner = nullptr;
    bool _quit = false;
};

} // namespace endo
