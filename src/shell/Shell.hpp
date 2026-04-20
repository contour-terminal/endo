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
#include <span>
#include <string>
#include <vector>

#include <platform/EnvironmentProvider.hpp>
#include <platform/FileSystem.hpp>
#include <platform/Wakeup.hpp>

#if defined(ENDO_ENABLE_AGENT) && ENDO_ENABLE_AGENT
    #include <agent/AgentConfig.hpp>
    #include <agent/context/ProjectContextLoader.hpp>

namespace endo::agent
{
class AgentSession;
class AgentWorker;
class ProviderFactory;
struct AgentRunOptions;
} // namespace endo::agent
#endif

#include <shell/DirectoryConfig.hpp>
#include <shell/completion/Completer.hpp>
#include <shell/completion/CompleterFunctionRegistry.hpp>
#include <shell/completion/ScriptedCompleter.hpp>
#include <shell/history/History.hpp>
#include <shell/history/PersistentHistory.hpp>
#include <shell/output/OutputDefinitionRegistry.hpp>
#include <shell/ui/Prompt.hpp>

#include "Job.hpp"
#include "TTY.hpp"
#if defined(ENDO_ENABLE_AGENT) && ENDO_ENABLE_AGENT
    #include <agent/mcp/ServerManager.hpp>
    #include <agent/tools/WebSearchTool.hpp>
#endif
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
    Shell(TTY& tty, EnvironmentProvider& env, FileSystem& fs);

    [[nodiscard]] EnvironmentProvider& environment() noexcept;
    [[nodiscard]] EnvironmentProvider const& environment() const noexcept;

    [[nodiscard]] FileSystem& fs() noexcept { return _fs; }

    [[nodiscard]] FileSystem const& fs() const noexcept { return _fs; }

    void setOptimize(bool optimize);

    /// Set check-only mode (compile without executing).
    void setCheckOnly(bool checkOnly) noexcept { _checkOnly = checkOnly; }

    /// Enable or disable unused-value detection for F# bindings.
    void setUnusedValueDetection(bool enabled) noexcept { _unusedValueDetection = enabled; }

#if defined(ENDO_ENABLE_AGENT) && ENDO_ENABLE_AGENT
    /// @brief Sets the trace file path for agent tool I/O tracing.
    /// @param path File path for JSONL trace output. Empty triggers auto-generated path.
    void setAgentTracePath(std::string path);
#endif

    /// Adds an additional module search path (for --module-path CLI option).
    void addModuleSearchPath(std::filesystem::path path);

    /// Sets the source file path for relative module resolution.
    void setSourceFile(std::filesystem::path path);

    /// Disables loading of init.endo profile on startup.
    void setNoProfile(bool noProfile) noexcept { _noProfile = noProfile; }

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

    /// @brief Executes a config script with interactive/unused-value flags suppressed.
    /// @param content The script source code.
    /// @param sourceName Source name for diagnostics.
    /// @return Exit code from execution.
    int executeConfigScript(std::string const& content, std::string_view sourceName);

    /// @brief Returns a mutable reference to the F# persistent state.
    [[nodiscard]] FSharpPersistentState& fsharpState() noexcept { return _fsharpState; }

    /// @brief Returns the completer function registry (for testing/diagnostics).
    [[nodiscard]] CompleterFunctionRegistry const& completerFunctions() const noexcept
    {
        return _completerFunctions;
    }

    /// @brief Called when the working directory changes, to load/unload directory configs.
    void onDirectoryChanged();

    /// @brief Invokes a user-defined F# callback `() -> string` by name and returns
    /// the produced string. Used by the prompt render path for dynamic fields like
    /// `shell_prompt_indicator` when they hold a function value.
    ///
    /// Runs synchronously on the calling (main) thread. Intended to be called from
    /// prompt-render code between REPL inputs, when the shell's REPL Runner is idle.
    /// On any error (unknown function, compilation failure, exception), returns
    /// std::nullopt and leaves the shell state unchanged.
    ///
    /// @param functionName The user-facing name (without the `fsharp.` compiler prefix).
    [[nodiscard]] std::optional<std::string> invokePromptCallback(std::string const& functionName);

#if defined(ENDO_ENABLE_AGENT) && ENDO_ENABLE_AGENT
    /// @brief Runs the agent in headless/batch mode (no TUI).
    /// @param options Parsed command-line options for the headless run.
    /// @return Exit code (0 = success, non-zero = failure).
    int runAgentHeadless(agent::AgentRunOptions const& options);
#endif

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
    JobTable jobTable; ///< Table of background jobs

  private:
    FileSystem& _fs; ///< Filesystem interface (declared before history for init order)

  public:
    PersistentHistory history { _fs };    ///< Command history for completion (persisted to disk)
    std::unique_ptr<Completer> completer; ///< Completion system

#if defined(ENDO_ENABLE_AGENT) && ENDO_ENABLE_AGENT
    /// Agent configuration loaded from agent.yml (API keys) and overridden by init.endo builtins.
    agent::AgentConfig agentConfig;

    /// Web search configuration for the agent web_search tool.
    /// Configurable at runtime via set_web_search_* builtins.
    agent::WebSearchConfig webSearchConfig;

    /// MCP server configurations collected from init.endo builtins.
    /// Servers are spawned when entering agent mode.
    std::vector<agent::mcp::McpServerConfig> mcpServerConfigs;
#endif

  private:
    /// @brief Lazily initializes interactive-mode subsystems (history, completer, directory config).
    /// Called once before entering the REPL loop. Safe to call multiple times (guarded by _interactiveReady).
    void ensureInteractiveReady();

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
    void registerDirectoryConfigBuiltins(); // builtins/DirectoryConfigBuiltins.cpp

    // --- Data-driven inline builtin dispatch (builtins/InlineCommandDescriptors.cpp) ---
  public:
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
    /// @brief Returns the sorted descriptor table of all inline builtins.
    [[nodiscard]] static std::span<struct InlineCommandDescriptor const> inlineCommandDescriptors();

    /// @brief Looks up an inline builtin by name (binary search on sorted table).
    /// @return Pointer to the descriptor, or nullptr if not found.
    [[nodiscard]] static InlineCommandDescriptor const* findInlineBuiltin(std::string_view name);

  private:
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
    /// Executes the timeout builtin, running a command with a time limit. Returns exit code.
    [[nodiscard]] int executeInlineTimeout(CoreVM::CoreStringArray const& args, NativeHandle outputFd);
    /// Executes the kill builtin, sending signals to processes or jobs. Returns exit code.
    [[nodiscard]] int executeInlineKill(CoreVM::CoreStringArray const& args, NativeHandle outputFd);
    /// Executes the pkill builtin, sending signals to processes matched by name or command line. Returns exit
    /// code.
    [[nodiscard]] int executeInlinePkill(CoreVM::CoreStringArray const& args, NativeHandle outputFd);
    /// Executes the whoami builtin. Returns exit code.
    [[nodiscard]] int executeInlineWhoami(CoreVM::CoreStringArray const& args, NativeHandle outputFd);
    /// Executes the hostname builtin. Returns exit code.
    [[nodiscard]] int executeInlineHostname(CoreVM::CoreStringArray const& args, NativeHandle outputFd);
    /// Executes the date builtin. Returns exit code.
    [[nodiscard]] int executeInlineDate(CoreVM::CoreStringArray const& args, NativeHandle outputFd);
    /// Executes the cal builtin (calendar view with optional color). Returns exit code.
    [[nodiscard]] int executeInlineCal(CoreVM::CoreStringArray const& args, NativeHandle outputFd);
    /// Executes the uname builtin. Returns exit code.
    [[nodiscard]] int executeInlineUname(CoreVM::CoreStringArray const& args, NativeHandle outputFd);
    /// Executes the nproc builtin. Returns exit code.
    [[nodiscard]] int executeInlineNproc(CoreVM::CoreStringArray const& args, NativeHandle outputFd);
    /// Executes the pwd builtin, printing the current working directory. Returns exit code.
    [[nodiscard]] int executeInlinePwd(CoreVM::CoreStringArray const& args, NativeHandle outputFd);
    /// Executes the basename builtin. Returns exit code.
    [[nodiscard]] int executeInlineBasename(CoreVM::CoreStringArray const& args, NativeHandle outputFd);
    /// Executes the dirname builtin. Returns exit code.
    [[nodiscard]] int executeInlineDirname(CoreVM::CoreStringArray const& args, NativeHandle outputFd);
    /// Executes the realpath builtin. Returns exit code.
    [[nodiscard]] int executeInlineRealpath(CoreVM::CoreStringArray const& args, NativeHandle outputFd);
    /// Executes the touch builtin. Returns exit code.
    [[nodiscard]] int executeInlineTouch(CoreVM::CoreStringArray const& args, NativeHandle outputFd);
    /// Executes the ln builtin. Returns exit code.
    [[nodiscard]] int executeInlineLn(CoreVM::CoreStringArray const& args, NativeHandle outputFd);
    /// Executes the mktemp builtin. Returns exit code.
    [[nodiscard]] int executeInlineMktemp(CoreVM::CoreStringArray const& args, NativeHandle outputFd);
    /// Executes the head builtin. Returns exit code.
    [[nodiscard]] int executeInlineHead(CoreVM::CoreStringArray const& args,
                                        NativeHandle outputFd,
                                        NativeHandle stdinFd);
    /// Executes the tail builtin. Returns exit code.
    [[nodiscard]] int executeInlineTail(CoreVM::CoreStringArray const& args,
                                        NativeHandle outputFd,
                                        NativeHandle stdinFd);
    /// Executes the wc builtin. Returns exit code.
    [[nodiscard]] int executeInlineWc(CoreVM::CoreStringArray const& args,
                                      NativeHandle outputFd,
                                      NativeHandle stdinFd);
    /// Executes the sort builtin. Returns exit code.
    [[nodiscard]] int executeInlineSort(CoreVM::CoreStringArray const& args,
                                        NativeHandle outputFd,
                                        NativeHandle stdinFd);
    /// Executes the uniq builtin. Returns exit code.
    [[nodiscard]] int executeInlineUniq(CoreVM::CoreStringArray const& args,
                                        NativeHandle outputFd,
                                        NativeHandle stdinFd);
    /// Executes the cut builtin. Returns exit code.
    [[nodiscard]] int executeInlineCut(CoreVM::CoreStringArray const& args,
                                       NativeHandle outputFd,
                                       NativeHandle stdinFd);
    /// Executes the tr builtin. Returns exit code.
    [[nodiscard]] int executeInlineTr(CoreVM::CoreStringArray const& args,
                                      NativeHandle outputFd,
                                      NativeHandle stdinFd);
    /// Executes the tee builtin. Returns exit code.
    [[nodiscard]] int executeInlineTee(CoreVM::CoreStringArray const& args,
                                       NativeHandle outputFd,
                                       NativeHandle stdinFd);
    /// Executes the history builtin. Returns exit code.
    [[nodiscard]] int executeInlineHistory(CoreVM::CoreStringArray const& args, NativeHandle outputFd);
    /// Executes the source builtin, running a script in the current shell context. Returns exit code.
    [[nodiscard]] int executeInlineSource(CoreVM::CoreStringArray const& args, NativeHandle outputFd);
    /// Executes the source-env builtin, sourcing a script and importing its environment. Returns exit code.
    [[nodiscard]] int executeInlineSourceEnv(CoreVM::CoreStringArray const& args, NativeHandle outputFd);
    /// Finalizes a pipeline builtin: closes pipe, tracks command, waits for downstream.
    void finalizePipelineBuiltin(bool lastInChain,
                                 CoreVM::CoreStringArray const& args,
                                 std::string_view programName,
                                 CoreVM::Params& context);

    // --- Process execution (builtins/ProcessExecution.cpp) ---

    /// @brief Tries to execute an inline builtin command synchronously.
    /// @param program The command name (e.g. "echo", "sleep", "grep").
    /// @param args The full argument list including program name at index 0.
    /// @param outputFd File descriptor for stdout output.
    /// @param inputFd File descriptor for stdin input.
    /// @return Exit code if the command was an inline builtin, std::nullopt otherwise.
    [[nodiscard]] std::optional<int> tryExecuteInlineBuiltin(std::string_view program,
                                                             CoreVM::CoreStringArray const& args,
                                                             NativeHandle outputFd,
                                                             NativeHandle inputFd);

    /// Executes an .endo script file in the current shell context (like source).
    /// @return Exit code of the script.
    [[nodiscard]] int executeEndoScript(std::filesystem::path const& scriptPath);

    /// Executes an .endo script file with positional parameters.
    ///
    /// Sets $0 to the script path and $1.. to the given arguments.
    /// Saves and restores the caller's positional parameters.
    /// @return Exit code of the script.
    [[nodiscard]] int executeEndoScript(std::filesystem::path const& scriptPath,
                                        std::span<std::string const> args);

    void builtinCallProcess(CoreVM::Params& context);
    void builtinCallProcessShellPiped(CoreVM::Params& context);

    /// F# builtin: run_script "path/to/file.endo"
    void builtinRunScript(CoreVM::Params& context);

    // --- Environment builtins (builtins/Environment.cpp) ---
    void builtinExit(CoreVM::Params& context);
    void builtinChDir(CoreVM::Params& context);
    void builtinChDirHome(CoreVM::Params& context);
    void applyDirectoryChange(std::filesystem::path const& path, CoreVM::Params& context);
    void builtinSet(CoreVM::Params& context);
    void builtinUnset(CoreVM::Params& context);
    void builtinGetVar(CoreVM::Params& context);
    // NOLINTNEXTLINE(readability-make-member-function-const) -- NativeCallback::bind requires non-const
    void builtinGetExitStatus(CoreVM::Params& context);
    void builtinSetExitStatus(CoreVM::Params& context);
    // NOLINTNEXTLINE(readability-make-member-function-const) -- NativeCallback::bind requires non-const
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
    [[nodiscard]] static std::string readInputLine(NativeHandle inputFd, ReadOptions const& options);
    [[nodiscard]] std::vector<std::string> splitByIFS(std::string_view input) const;

    // --- User commands (builtins/UserCommands.cpp) ---
    void builtinBind(CoreVM::Params& context);
    void builtinWhich(CoreVM::Params& context);

    // --- Directory config builtins (builtins/DirectoryConfigBuiltins.cpp) ---
    [[nodiscard]] int executeInlineDirConfig(CoreVM::CoreStringArray const& args, NativeHandle outputFd);

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
    // NOLINTNEXTLINE(readability-make-member-function-const)
    void builtinFetch(CoreVM::Params& context);
    // NOLINTNEXTLINE(readability-make-member-function-const)
    void builtinFetchWithHeaders(CoreVM::Params& context);

    // --- Core shell methods ---
    void trace(CoreVM::Instruction instr, size_t ip, size_t sp);

    // Shell integration (OSC 133) and CWD propagation (OSC 7)
    void emitPromptStart();
    void emitPromptEnd();
    void emitCommandStart();
    void emitCommandFinished(int exitCode);
    void emitCurrentWorkingDirectory();

    /// @brief Sets the terminal window title via OSC 2.
    /// Control characters (C0, DEL, C1) are stripped to prevent escape injection.
    void emitWindowTitle(std::string_view title);

    template <typename... Args>
    void error(std::format_string<Args...> const& message, Args&&... args)
    {
        auto text = std::format(message, std::forward<Args>(args)...);
        text += '\n';
        _tty.writeToStderr(text);
    }

    std::unique_ptr<tui::SemanticBlockClient> _semanticBlockClient;

#if defined(ENDO_ENABLE_AGENT) && ENDO_ENABLE_AGENT
    // --- Agent mode ---
    void runAgentMode(std::optional<std::string> initialMessage = std::nullopt);

    /// @brief Offers error recovery after a failed command.
    /// @param exitCode The exit code of the failed command.
    /// @param command The command text that failed.
    void offerErrorRecovery(int exitCode, std::string const& command);

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
#endif

    CoreVM::Runtime _runtime;
    CoreVM::diagnostics::BufferedReport _moduleReport; ///< Diagnostics report for module loading
    EnvironmentProvider& _env;
    TTY& _tty;
    FSharpPersistentState _fsharpState;            ///< F# function definitions persisted across REPL prompts
    OutputDefinitionRegistry _outputDefinitions;   ///< Output definition registry for structured pipelines
    CompleterFunctionRegistry _completerFunctions; ///< Scripted completer function registry
    std::vector<CollectedCompletion> _collectedCompletions;    ///< Buffer for __collect_completions bridge
    std::unique_ptr<DirectoryConfigManager> _dirConfigManager; ///< Per-directory config manager

    ProcessManager& _processManager;

    std::unique_ptr<CoreVM::Program> _currentProgram;
    CoreVM::Runner::Globals _globals;

    bool _optimize = false;
    bool _checkOnly = false;
    bool _noProfile = false;

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
        void closeCurrentPipeWriter() const;

        /// Close pipe file descriptors retained by the parent after child process spawn.
        void closePipeFdsInParent();
    };

    PipelineBuilder _currentPipelineBuilder;

    std::vector<ProcessId> _currentProcessGroupPids;
    std::vector<std::string> _pipelineCommands; ///< Commands in current pipeline for job table display
    std::optional<ProcessId> _leftPid;
    std::optional<ProcessId> _rightPid;

    int _exitCode = -1;
    /// Scratch buffer populated by the `__prompt_capture_string` builtin when
    /// `invokePromptCallback` runs a user-defined prompt function. Read once and
    /// cleared by `invokePromptCallback`; not thread-safe.
    std::string _promptCallbackResult;
    std::chrono::milliseconds _lastCommandDuration { 0 }; ///< Duration of the last command
    bool _interactive = true;                             ///< Whether running in interactive mode
    unsigned _configScriptDepth = 0;    ///< Nesting depth of config script / command substitution execution
    bool _unusedValueDetection = false; ///< Detect unused F# bindings (script mode only)
    bool _lsIcons = true;               ///< Show Nerd Font icons in ls output
    bool _lsDirectorySlash = true;      ///< Append trailing '/' to directory names
    ProcessId _shellPid = 0;
    ProcessId _shellPgid = 0; ///< Shell's process group ID
    int _signalFd = -1;       ///< signalfd for Linux, -1 otherwise
    int _shellLevel = 0;      ///< Shell nesting depth (0 = outermost)
    std::optional<ProcessId> _lastBackgroundPid;
    std::vector<std::string> _positionalParameters;
    bool _interactiveReady =
        false; ///< Whether interactive subsystems (history, completer, dirconfig) are initialized
    std::vector<std::vector<std::string>> _cmdBuilderStack;

    struct RedirectState
    {
        enum class Type // NOLINT(performance-enum-size)
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
