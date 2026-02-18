// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <shell/ProcessGroup.hpp>

#include <endo-language/IRGenerator.hpp>

#include <http/HttpClient.hpp>

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

#include <agent/ProjectContextLoader.hpp>
#include <platform/EnvironmentProvider.hpp>

namespace endo::agent
{
class AgentSession;
class ProviderFactory;
} // namespace endo::agent

#include "Completer.hpp"
#include "History.hpp"
#include "Job.hpp"
#include "OutputDefinitionRegistry.hpp"
#include "PersistentHistory.hpp"
#include "Prompt.hpp"
#include "TTY.hpp"
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

    /// Set interactive mode (controls prompts, job notifications, etc.)
    void setInteractive(bool interactive);

    /// Set positional parameters ($0, $1, $2, ...)
    void setPositionalParameters(std::vector<std::string> params);

    int run();
    int execute(std::string const& lineBuffer);

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

    // --- Output builtins (builtins/Output.cpp) ---
    void builtinPrint(CoreVM::Params& context);
    void builtinPrintln(CoreVM::Params& context);
    void builtinDisplayResult(CoreVM::Params& context);
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
    void runAgentMode();

    std::unique_ptr<http::HttpClient> _agentHttpClient;
    std::unique_ptr<agent::ProviderFactory> _agentProviderFactory;
    std::unique_ptr<agent::AgentSession> _agentSession;
    std::optional<agent::ProjectContext>
        _cachedProjectContext;                      ///< Cached project context for agent mode re-entry.
    std::filesystem::path _cachedProjectContextCwd; ///< CWD associated with cached project context.

    CoreVM::Runtime _runtime;
    EnvironmentProvider& _env;
    TTY& _tty;
    FSharpPersistentState _fsharpState;          ///< F# function definitions persisted across REPL prompts
    OutputDefinitionRegistry _outputDefinitions; ///< Output definition registry for structured pipelines

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

        auto requestShellPipe(bool lastInChain) -> IODescriptors;

        /// Close the current pipe's writer (for builtin commands that write to pipe)
        void closeCurrentPipeWriter();
    };

    PipelineBuilder _currentPipelineBuilder;

    std::vector<ProcessId> _currentProcessGroupPids;
    std::vector<std::string> _pipelineCommands; ///< Commands in current pipeline for job table display
    std::optional<ProcessId> _leftPid;
    std::optional<ProcessId> _rightPid;

    int _exitCode = -1;
    std::chrono::milliseconds _lastCommandDuration { 0 }; ///< Duration of the last command
    bool _interactive = true;                             ///< Whether running in interactive mode
    ProcessId _shellPid = 0;
    ProcessId _shellPgid = 0; ///< Shell's process group ID
    int _signalFd = -1;       ///< signalfd for Linux, -1 otherwise
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
