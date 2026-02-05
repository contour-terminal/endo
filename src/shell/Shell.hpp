// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <shell/ProcessGroup.hpp>

#include <CoreVM/CoreVM.hpp>

#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <print>
#include <set>
#include <string>
#include <vector>

#include "Job.hpp"
#include "Pipe.hpp"
#include "Process.hpp"
#include "Prompt.hpp"
#include "SignalHandler.hpp"
#include "TTY.hpp"

namespace endo
{

std::string readLine(TTY& tty, std::string_view prompt);

class Environment
{
  public:
    virtual ~Environment() = default;

    virtual void set(std::string_view name, std::string_view value) = 0;
    [[nodiscard]] virtual std::optional<std::string_view> get(std::string_view name) const = 0;
    virtual void unset(std::string_view name) = 0;

    virtual void exportVariable(std::string_view name) = 0;

    void setAndExport(std::string_view name, std::string_view value);
};

class TestEnvironment: public Environment
{
  public:
    void set(std::string_view name, std::string_view value) override;
    [[nodiscard]] std::optional<std::string_view> get(std::string_view name) const override;
    void unset(std::string_view name) override;
    void exportVariable(std::string_view name) override;

  private:
    std::map<std::string, std::string> _values;
};

class SystemEnvironment: public Environment
{
  public:
    void set(std::string_view name, std::string_view value) override;
    [[nodiscard]] std::optional<std::string_view> get(std::string_view name) const override;
    void unset(std::string_view name) override;
    void exportVariable(std::string_view name) override;

    static SystemEnvironment& instance();

  private:
    std::map<std::string, std::string> _values;
};

class Shell final: public CoreVM::Runtime
{
  public:
    Shell();
    ~Shell();

    Shell(TTY& tty, Environment& env);

    [[nodiscard]] Environment& environment() noexcept;
    [[nodiscard]] Environment const& environment() const noexcept;

    void setOptimize(bool optimize);

    int run();
    int execute(std::string const& lineBuffer);

    /// Called when SIGCHLD is received to reap child processes.
    void onSigchld();

    /// Reports status of completed/stopped background jobs to the user.
    void reportJobStatus();

    Prompt prompt;
    std::vector<ProcessGroup> processGroups;
    JobTable jobTable; ///< Table of background jobs

  private:
    void registerBuiltinFunctions();

    // Builtin functions
    void builtinExit(CoreVM::Params& context);
    void builtinCallProcess(CoreVM::Params& context);
    void builtinCallProcessShellPiped(CoreVM::Params& context);
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
    void builtinCmdStart(CoreVM::Params& context);
    void builtinCmdArg(CoreVM::Params& context);
    void builtinCmdExec(CoreVM::Params& context);
    void builtinCmdExecPiped(CoreVM::Params& context);
    void builtinSetAndExport(CoreVM::Params& context);
    void builtinExport(CoreVM::Params& context);
    void builtinTrue(CoreVM::Params& context);
    void builtinFalse(CoreVM::Params& context);
    void builtinReadDefault(CoreVM::Params& context);
    void builtinRead(CoreVM::Params& context);
    void builtinOpenRead(CoreVM::Params& context);
    void builtinOpenWrite(CoreVM::Params& context);
    void builtinRedirectStart(CoreVM::Params& context);
    void builtinRedirectInput(CoreVM::Params& context);
    void builtinRedirectOutput(CoreVM::Params& context);
    void builtinRedirectFdDup(CoreVM::Params& context);
    void builtinRedirectHeredoc(CoreVM::Params& context);
    void builtinRedirectHerestring(CoreVM::Params& context);
    void builtinRedirectEnd(CoreVM::Params& context);
    void builtinSubstStart(CoreVM::Params& context);
    void builtinSubstEnd(CoreVM::Params& context);
    void builtinProcSubstFork(CoreVM::Params& context);
    void builtinProcSubstExit(CoreVM::Params& context);
    void builtinProcSubstGetPath(CoreVM::Params& context);
    void builtinProcSubstCleanup(CoreVM::Params& context);
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
    void builtinForInit(CoreVM::Params& context);
    void builtinForAddItem(CoreVM::Params& context);
    void builtinForHasMore(CoreVM::Params& context);
    void builtinForNext(CoreVM::Params& context);
    void builtinForCleanup(CoreVM::Params& context);
    void builtinCaseMatch(CoreVM::Params& context);
    void builtinFunctionRegister(CoreVM::Params& context);
    void builtinFunctionCall(CoreVM::Params& context);
    void builtinJobs(CoreVM::Params& context);
    void builtinFg(CoreVM::Params& context);
    void builtinBg(CoreVM::Params& context);
    void builtinWait(CoreVM::Params& context);
    void builtinCmdExecPipedBackground(CoreVM::Params& context);

    // Helper functions
    void cleanupProcSubst();
    void applyRedirects(SpawnConfig& config);

    [[nodiscard]] std::expected<std::filesystem::path, ShellError> resolveProgram(
        std::string const& program) const;

    void trace(CoreVM::Instruction instr, size_t ip, size_t sp);

    template <typename... Args>
    void error(std::format_string<Args...> const& message, Args&&... args)
    {
        std::println(std::cerr, "{}", std::format(message, std::forward<Args>(args)...));
    }

    [[nodiscard]] static bool globMatchFilename(std::string_view filename, std::string_view pattern);
    [[nodiscard]] static std::vector<std::string> expandGlobPattern(std::string_view pattern);
    [[nodiscard]] static std::vector<std::string> expandRecursiveGlob(std::string_view pattern);
    [[nodiscard]] static bool globMatch(std::string_view text, std::string_view pattern);
    [[nodiscard]] static std::vector<size_t> findPrefixMatches(std::string_view text,
                                                               std::string_view pattern);
    [[nodiscard]] static std::vector<size_t> findSuffixMatches(std::string_view text,
                                                               std::string_view pattern);
    [[nodiscard]] static std::optional<size_t> findPatternMatchLength(std::string_view text,
                                                                      std::string_view pattern);

    std::vector<std::string>& cmdBuilderArgs();

    Environment& _env;
    TTY& _tty;

    ProcessManager& _processManager;

    std::unique_ptr<CoreVM::Program> _currentProgram;
    CoreVM::Runner::Globals _globals;

    bool _optimize = false;

    struct PipelineBuilder
    {
        struct IODescriptors
        {
            NativeHandle reader;
            NativeHandle writer;
        };

        NativeHandle defaultStdinFd = 0;
        NativeHandle defaultStdoutFd = 1;
        std::unique_ptr<Pipe> currentPipe = nullptr;

        auto requestShellPipe(bool lastInChain) -> IODescriptors;
    };

    PipelineBuilder _currentPipelineBuilder;

    std::vector<ProcessId> _currentProcessGroupPids;
    std::optional<ProcessId> _leftPid;
    std::optional<ProcessId> _rightPid;

    int _exitCode = -1;
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
            NativeHandle openedFd = -1;
        };

        std::vector<Entry> entries;

        void clear();
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
        NativeHandle savedStdout = -1;
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
