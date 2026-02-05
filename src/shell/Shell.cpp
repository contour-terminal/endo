// SPDX-License-Identifier: Apache-2.0
module;
#include <shell/ProcessGroup.h>

#include <crispy/assert.h>
#include <crispy/utils.h>

#include <cstdio>
#include <cstring>
#include <expected>
#include <filesystem>
#include <format>
#include <iostream>
#include <map>
#include <memory>
#include <print>

#if !defined(_WIN32)
    #include <sys/wait.h>

    #include <fcntl.h>
    #include <pwd.h>
    #include <unistd.h>
#endif

#include "Error.h"
#include "LogConfig.h"
#include "Platform.h"

import TTY;
import Pipe;
import Prompt;
import Lexer;
import ASTPrinter;
import IRGenerator;
import Parser;
import Process;

import CoreVM;

export module Shell;

namespace
{
// Use function-local static to avoid C++20 module static initialization issues
auto& debugLog()
{
    static auto instance =
        logstore::category("shell.debug", "Shell debug log", endo::log::categoryState("shell.debug"));
    return instance;
}
} // namespace

namespace endo
{

using std::placeholders::_1;
using std::placeholders::_2;
using std::placeholders::_3;

std::string readLine(TTY& tty, std::string_view prompt)
{
    // Most super-native implementation, yet to be replaced by a proper line editor.
    tty.writeToStdout(std::format("{}", prompt));
    std::string line;
    while (true)
    {
        char ch {};
        ssize_t const n = read(tty.inputFd(), &ch, 1);
        if (n == 0)
            break;
        else if (n == -1)
        {
            if (errno == EINTR)
                continue;
            else
                break;
        }
        else if (ch == '\n')
            break;
        else
            line += ch;
    }
    return line;
}

class Environment
{
  public:
    virtual ~Environment() = default;

    virtual void set(std::string_view name, std::string_view value) = 0;
    [[nodiscard]] virtual std::optional<std::string_view> get(std::string_view name) const = 0;
    virtual void unset(std::string_view name) = 0;

    virtual void exportVariable(std::string_view name) = 0;

    void setAndExport(std::string_view name, std::string_view value)
    {
        set(name, value);
        exportVariable(name);
    }
};

struct PipelineBuilder
{
    struct IODescriptors
    {
        NativeHandle reader;
        NativeHandle writer;
    };

    NativeHandle defaultStdinFd = 0;  // STDIN_FILENO
    NativeHandle defaultStdoutFd = 1; // STDOUT_FILENO
    std::unique_ptr<Pipe> currentPipe = nullptr;

    auto requestShellPipe(bool lastInChain) -> IODescriptors;
};

inline auto PipelineBuilder::requestShellPipe(bool lastInChain) -> IODescriptors
{
    NativeHandle const stdinFd = !currentPipe ? defaultStdinFd : currentPipe->releaseReader();
    if (lastInChain)
        currentPipe = nullptr;
    else if (auto pipeResult = createPipe(); pipeResult.has_value())
        currentPipe = std::move(pipeResult.value());
    else
        currentPipe = nullptr; // Error case - will result in using default stdout
    NativeHandle const stdoutFd = lastInChain || !currentPipe ? defaultStdoutFd : currentPipe->writer();
    return IODescriptors { .reader = stdinFd, .writer = stdoutFd };
}

/// Stores state during command substitution capture.
///
/// When executing a command substitution like $(command), we redirect the
/// command's stdout to a pipe and capture the output.
struct SubstitutionCapture
{
    std::unique_ptr<Pipe> pipe;    ///< Pipe to capture stdout
    NativeHandle savedStdout = -1; ///< Original stdout fd saved during capture
    std::string output;            ///< Accumulated output

    void clear()
    {
        pipe.reset();
        if (savedStdout != -1)
        {
            savedStdout = -1;
        }
        output.clear();
    }
};

/// Stores redirect state during command execution.
///
/// Redirects are collected via builtins during IR execution, then applied
/// just before spawning the process.
struct RedirectState
{
    enum class Type
    {
        InputFile,  ///< < FILE
        OutputFile, ///< > FILE or >> FILE
        FdDup,      ///< N>&M
        HereDoc,    ///< << EOF ... EOF
        HereString, ///< <<< "string"
    };

    struct Entry
    {
        Type type;
        int sourceFd = -1;          ///< Source fd for output redirects
        int targetFd = -1;          ///< Target fd for input redirects or fd duplication
        std::string path;           ///< File path for file redirects
        std::string content;        ///< Content for heredoc/herestring
        bool append = false;        ///< True for >> (append mode)
        NativeHandle openedFd = -1; ///< Fd opened for this redirect (to close after spawn)
    };

    std::vector<Entry> entries;

    void clear() { entries.clear(); }

    void addInputFile(int targetFd, std::string path)
    {
        entries.push_back({ .type = Type::InputFile, .targetFd = targetFd, .path = std::move(path) });
    }

    void addOutputFile(int sourceFd, std::string path, bool append)
    {
        entries.push_back(
            { .type = Type::OutputFile, .sourceFd = sourceFd, .path = std::move(path), .append = append });
    }

    void addFdDup(int sourceFd, int targetFd)
    {
        entries.push_back({ .type = Type::FdDup, .sourceFd = sourceFd, .targetFd = targetFd });
    }

    void addHereDoc(int targetFd, std::string content)
    {
        entries.push_back({ .type = Type::HereDoc, .targetFd = targetFd, .content = std::move(content) });
    }

    void addHereString(int targetFd, std::string content)
    {
        entries.push_back({ .type = Type::HereString, .targetFd = targetFd, .content = std::move(content) });
    }
};

export class TestEnvironment: public Environment
{
  public:
    void set(std::string_view name, std::string_view value) override
    {
        _values[std::string(name)] = std::string(value);
    }

    [[nodiscard]] std::optional<std::string_view> get(std::string_view name) const override
    {
        if (auto i = _values.find(name.data()); i != _values.end())
            return i->second;
        else if (auto const* value = getenv(name.data()))
            return std::string_view { value };
        else
            return std::nullopt;
    }

    void unset(std::string_view name) override
    {
        _values.erase(std::string(name));
        unsetenv(name.data());
    }

    void exportVariable(std::string_view name) override
    {
        if (auto i = _values.find(name.data()); i != _values.end())
            setenv(name.data(), i->second.data(), 1);
    }

  private:
    std::map<std::string, std::string> _values;
};

export class SystemEnvironment: public Environment
{
  public:
    void set(std::string_view name, std::string_view value) override
    {
        _values[std::string(name)] = std::string(value);
    }

    [[nodiscard]] std::optional<std::string_view> get(std::string_view name) const override
    {
        if (auto i = _values.find(name.data()); i != _values.end())
            return i->second;
        else if (auto const* value = getenv(name.data()))
            return std::string_view { value };
        else
            return std::nullopt;
    }

    void unset(std::string_view name) override
    {
        _values.erase(std::string(name));
        unsetenv(name.data());
    }

    void exportVariable(std::string_view name) override
    {
        if (auto i = _values.find(name.data()); i != _values.end())
            setenv(name.data(), i->second.data(), 1);
    }

    static SystemEnvironment& instance()
    {
        static SystemEnvironment env;
        return env;
    }

  private:
    std::map<std::string, std::string> _values;
};

export class Shell final: public CoreVM::Runtime
{
  public:
    Shell(): Shell(RealTTY::instance(), SystemEnvironment::instance()) {}

    ~Shell() = default;

    Shell(TTY& tty, Environment& env): _env { env }, _tty { tty }
    {
        _currentPipelineBuilder.defaultStdinFd = _tty.inputFd();
        _currentPipelineBuilder.defaultStdoutFd = _tty.outputFd();

        _env.setAndExport("SHELL", "endo");

        // Capture the shell's process ID at startup
#if !defined(_WIN32)
        _shellPid = static_cast<ProcessId>(getpid());
#else
        _shellPid = static_cast<ProcessId>(GetCurrentProcessId());
#endif

        // NB: These lines could go away once we have a proper command line parser and
        //     the ability to set these options from the command line.
        registerBuiltinFunctions();

        // for (CoreVM::NativeCallback const* callback: builtins())
        //      fmt::print("builtin: {}\n", callback->signature().to_s());
    }

    [[nodiscard]] Environment& environment() noexcept { return _env; }

    [[nodiscard]] Environment const& environment() const noexcept { return _env; }

    void setOptimize(bool optimize) { _optimize = optimize; }

    int run()
    {
        while (!_quit && prompt.ready())
        {
            auto const lineBuffer = prompt.read();
            debugLog()()("input buffer: {}", lineBuffer);

            _exitCode = execute(lineBuffer);
            // _tty.writeToStdout("exit code: {}\n", _exitCode);
        }

        return _quit ? _exitCode : EXIT_SUCCESS;
    }

    int execute(std::string const& lineBuffer)
    {
        // Clear any leftover redirect state from previous commands
        _redirectState.clear();

        try
        {
            CoreVM::diagnostics::ConsoleReport report;
            auto parser = endo::Parser(*this, report, std::make_unique<endo::StringSource>(lineBuffer));
            auto const rootNode = parser.parse();

            // Check for parser errors
            if (report.containsFailures())
                return EXIT_FAILURE;

            if (!rootNode)
                return EXIT_FAILURE;

            debugLog()()("Parsed & printed: {}", endo::ast::ASTPrinter::print(*rootNode));

            auto irProgram = IRGenerator::generate(*rootNode, report, *this);

            // Check for IR generation errors
            if (report.containsFailures())
                return EXIT_FAILURE;

            if (!irProgram)
                return EXIT_FAILURE;

            if (_optimize)
            {
                CoreVM::PassManager pm;

                // clang-format off
            pm.registerPass("eliminate-empty-blocks", &CoreVM::transform::emptyBlockElimination);
            pm.registerPass("eliminate-linear-br", &CoreVM::transform::eliminateLinearBr);
            pm.registerPass("eliminate-unused-blocks", &CoreVM::transform::eliminateUnusedBlocks);
            pm.registerPass("eliminate-unused-instr", &CoreVM::transform::eliminateUnusedInstr);
            pm.registerPass("fold-constant-condbr", &CoreVM::transform::foldConstantCondBr);
            pm.registerPass("rewrite-br-to-exit", &CoreVM::transform::rewriteBrToExit);
            pm.registerPass("rewrite-cond-br-to-same-branches", &CoreVM::transform::rewriteCondBrToSameBranches);
                // clang-format on

                pm.run(irProgram.get());
            }

            debugLog()()("================================================\n");
            debugLog()()("Optimized IR program:\n");
            if (debugLog().is_enabled())
                irProgram->dump();

            _currentProgram = CoreVM::TargetCodeGenerator {}.generate(irProgram.get());
            if (!_currentProgram)
            {
                error("Failed to generate target code");
                return EXIT_FAILURE;
            }
            _currentProgram->link(this, &report);

            debugLog()()("================================================\n");
            debugLog()()("Linked target code:\n");
            if (debugLog().is_enabled())
                _currentProgram->dump();

            CoreVM::Handler* main = _currentProgram->findHandler("@main");
            assert(main != nullptr);
            auto runner =
                CoreVM::Runner(main, nullptr, &_globals, std::bind(&Shell::trace, this, _1, _2, _3));
            _runner = &runner;
            runner.run();
            return _exitCode;
        }
        catch (std::exception const& e)
        {
            error("Exception caught: {}", e.what());
            return EXIT_FAILURE;
        }

        return EXIT_SUCCESS;
    }

    Prompt prompt;
    std::vector<ProcessGroup> processGroups;

  private:
    void registerBuiltinFunctions()
    {
        // clang-format off
        registerFunction("exit")
            .param<CoreVM::CoreNumber>("code")
            .returnType(CoreVM::LiteralType::Void)
            .bind(&Shell::builtinExit, this);

        registerFunction("export")
            .param<std::string>("name")
            .returnType(CoreVM::LiteralType::Void)
            .bind(&Shell::builtinExport, this);

        registerFunction("export")
            .param<std::string>("name")
            .param<std::string>("value")
            .returnType(CoreVM::LiteralType::Void)
            .bind(&Shell::builtinSetAndExport, this);

        registerFunction("true")
            .returnType(CoreVM::LiteralType::Boolean)
            .bind(&Shell::builtinTrue, this);

        registerFunction("false")
            .returnType(CoreVM::LiteralType::Boolean)
            .bind(&Shell::builtinFalse, this);

        registerFunction("cd")
            .returnType(CoreVM::LiteralType::Boolean)
            .bind(&Shell::builtinChDirHome, this);

        registerFunction("cd")
            .param<std::string>("path")
            .returnType(CoreVM::LiteralType::Boolean)
            .bind(&Shell::builtinChDir, this);

        registerFunction("set")
            .param<std::string>("name")
            .param<std::string>("value")
            .returnType(CoreVM::LiteralType::Boolean)
            .bind(&Shell::builtinSet, this);

        registerFunction("unset")
            .param<std::string>("name")
            .returnType(CoreVM::LiteralType::Boolean)
            .bind(&Shell::builtinUnset, this);

        registerFunction("getvar")
            .param<std::string>("name")
            .returnType(CoreVM::LiteralType::String)
            .bind(&Shell::builtinGetVar, this);

        registerFunction("getvar.exitstatus")
            .returnType(CoreVM::LiteralType::String)
            .bind(&Shell::builtinGetExitStatus, this);

        registerFunction("getvar.processid")
            .returnType(CoreVM::LiteralType::String)
            .bind(&Shell::builtinGetProcessId, this);

        registerFunction("getvar.backgroundid")
            .returnType(CoreVM::LiteralType::String)
            .bind(&Shell::builtinGetBackgroundId, this);

        registerFunction("getvar.positional")
            .param<CoreVM::CoreNumber>("index")
            .returnType(CoreVM::LiteralType::String)
            .bind(&Shell::builtinGetPositional, this);

        registerFunction("callproc")
            .param<std::vector<std::string>>("args")
            //.param<std::vector<CoreVM::CoreNumber>>("redirects")
            .returnType(CoreVM::LiteralType::Number)
            .bind(&Shell::builtinCallProcess, this);

        registerFunction("callproc")
            .param<bool>("last_in_chain")
            .param<std::vector<std::string>>("args")
            //.param<std::vector<CoreVM::CoreNumber>>("redirects")
            .returnType(CoreVM::LiteralType::Number)
            .bind(&Shell::builtinCallProcessShellPiped, this);

        registerFunction("read")
            .returnType(CoreVM::LiteralType::String)
            .bind(&Shell::builtinReadDefault, this);

        registerFunction("read")
            .param<std::vector<std::string>>("args")
            .returnType(CoreVM::LiteralType::String)
            .bind(&Shell::builtinRead, this);

        // used to redirect file to stdin
        registerFunction("internal.open_read")
            .param<std::string>("path")
            .returnType(CoreVM::LiteralType::Number)
            .bind(&Shell::builtinOpenRead, this);

        // used for redirecting output to a file
        registerFunction("internal.open_write")
            .param<std::string>("path")
            .param<CoreVM::CoreNumber>("oflags")
            .returnType(CoreVM::LiteralType::Number)
            .bind(&Shell::builtinOpenWrite, this);

        // Command builder functions for dynamic argument construction
        registerFunction("internal.cmd_start")
            .param<std::string>("program")
            .returnType(CoreVM::LiteralType::Void)
            .bind(&Shell::builtinCmdStart, this);

        registerFunction("internal.cmd_arg")
            .param<std::string>("arg")
            .returnType(CoreVM::LiteralType::Void)
            .bind(&Shell::builtinCmdArg, this);

        registerFunction("internal.cmd_exec")
            .returnType(CoreVM::LiteralType::Number)
            .bind(&Shell::builtinCmdExec, this);

        registerFunction("internal.cmd_exec_piped")
            .param<bool>("last_in_chain")
            .returnType(CoreVM::LiteralType::Number)
            .bind(&Shell::builtinCmdExecPiped, this);

        // Redirect management functions
        registerFunction("internal.redirect_start")
            .returnType(CoreVM::LiteralType::Void)
            .bind(&Shell::builtinRedirectStart, this);

        registerFunction("internal.redirect_input")
            .param<CoreVM::CoreNumber>("target_fd")
            .param<std::string>("path")
            .returnType(CoreVM::LiteralType::Void)
            .bind(&Shell::builtinRedirectInput, this);

        registerFunction("internal.redirect_output")
            .param<CoreVM::CoreNumber>("source_fd")
            .param<std::string>("path")
            .param<bool>("append")
            .returnType(CoreVM::LiteralType::Void)
            .bind(&Shell::builtinRedirectOutput, this);

        registerFunction("internal.redirect_fd_dup")
            .param<CoreVM::CoreNumber>("source_fd")
            .param<CoreVM::CoreNumber>("target_fd")
            .returnType(CoreVM::LiteralType::Void)
            .bind(&Shell::builtinRedirectFdDup, this);

        registerFunction("internal.redirect_heredoc")
            .param<CoreVM::CoreNumber>("target_fd")
            .param<std::string>("content")
            .returnType(CoreVM::LiteralType::Void)
            .bind(&Shell::builtinRedirectHeredoc, this);

        registerFunction("internal.redirect_herestring")
            .param<CoreVM::CoreNumber>("target_fd")
            .param<std::string>("content")
            .returnType(CoreVM::LiteralType::Void)
            .bind(&Shell::builtinRedirectHerestring, this);

        registerFunction("internal.redirect_end")
            .returnType(CoreVM::LiteralType::Void)
            .bind(&Shell::builtinRedirectEnd, this);

        // Command substitution functions
        registerFunction("internal.subst_start")
            .returnType(CoreVM::LiteralType::Void)
            .bind(&Shell::builtinSubstStart, this);

        registerFunction("internal.subst_end")
            .returnType(CoreVM::LiteralType::String)
            .bind(&Shell::builtinSubstEnd, this);

        // Process substitution functions
        registerFunction("internal.procsubst_fork")
            .param<bool>("is_write")
            .returnType(CoreVM::LiteralType::Number)
            .bind(&Shell::builtinProcSubstFork, this);

        registerFunction("internal.procsubst_exit")
            .returnType(CoreVM::LiteralType::Void)
            .bind(&Shell::builtinProcSubstExit, this);

        registerFunction("internal.procsubst_get_path")
            .returnType(CoreVM::LiteralType::String)
            .bind(&Shell::builtinProcSubstGetPath, this);

        registerFunction("internal.procsubst_cleanup")
            .returnType(CoreVM::LiteralType::Void)
            .bind(&Shell::builtinProcSubstCleanup, this);

        // Tilde expansion functions
        registerFunction("expand.tilde")
            .param<std::string>("suffix")
            .returnType(CoreVM::LiteralType::String)
            .bind(&Shell::builtinExpandTilde, this);

        registerFunction("expand.tilde_user")
            .param<std::string>("user")
            .param<std::string>("suffix")
            .returnType(CoreVM::LiteralType::String)
            .bind(&Shell::builtinExpandTildeUser, this);

        // Glob expansion
        registerFunction("expand.glob")
            .param<std::string>("pattern")
            .returnType(CoreVM::LiteralType::Void)
            .bind(&Shell::builtinExpandGlob, this);

        // Arithmetic expansion helpers
        registerFunction("expand.arith_to_string")
            .param<CoreVM::CoreNumber>("value")
            .returnType(CoreVM::LiteralType::String)
            .bind(&Shell::builtinArithToString, this);

        registerFunction("expand.arith_getvar")
            .param<std::string>("name")
            .returnType(CoreVM::LiteralType::Number)
            .bind(&Shell::builtinArithGetVar, this);

        registerFunction("expand.arith_pow")
            .param<CoreVM::CoreNumber>("base")
            .param<CoreVM::CoreNumber>("exp")
            .returnType(CoreVM::LiteralType::Number)
            .bind(&Shell::builtinArithPow, this);

        // Parameter expansion functions
        registerFunction("expand.param_length")
            .param<std::string>("var")
            .returnType(CoreVM::LiteralType::String)
            .bind(&Shell::builtinExpandParamLength, this);

        registerFunction("expand.param_default")
            .param<std::string>("var")
            .param<std::string>("default_value")
            .returnType(CoreVM::LiteralType::String)
            .bind(&Shell::builtinExpandParamDefault, this);

        registerFunction("expand.param_alternate")
            .param<std::string>("var")
            .param<std::string>("alternate")
            .returnType(CoreVM::LiteralType::String)
            .bind(&Shell::builtinExpandParamAlternate, this);

        registerFunction("expand.param_assign")
            .param<std::string>("var")
            .param<std::string>("default_value")
            .returnType(CoreVM::LiteralType::String)
            .bind(&Shell::builtinExpandParamAssign, this);

        registerFunction("expand.param_error")
            .param<std::string>("var")
            .param<std::string>("error_msg")
            .returnType(CoreVM::LiteralType::String)
            .bind(&Shell::builtinExpandParamError, this);

        registerFunction("expand.param_remove_prefix")
            .param<std::string>("var")
            .param<std::string>("pattern")
            .param<bool>("longest")
            .returnType(CoreVM::LiteralType::String)
            .bind(&Shell::builtinExpandParamRemovePrefix, this);

        registerFunction("expand.param_remove_suffix")
            .param<std::string>("var")
            .param<std::string>("pattern")
            .param<bool>("longest")
            .returnType(CoreVM::LiteralType::String)
            .bind(&Shell::builtinExpandParamRemoveSuffix, this);

        registerFunction("expand.param_replace")
            .param<std::string>("var")
            .param<std::string>("pattern")
            .param<std::string>("replacement")
            .param<bool>("all")
            .returnType(CoreVM::LiteralType::String)
            .bind(&Shell::builtinExpandParamReplace, this);
        // clang-format on
    }

    // builtins that match to shell commands
    void builtinExit(CoreVM::Params& context)
    {
        _exitCode = static_cast<int>(context.getInt(1));
        _runner->suspend();
        _quit = true;
    }

    void builtinCallProcess(CoreVM::Params& context)
    {
        CoreVM::CoreStringArray const& args = context.getStringArray(1);
        std::string const& program = args.at(0);
        auto const programPath = resolveProgram(program);

        if (!programPath.has_value())
        {
            error("{}: {}", program, toString(programPath.error()));
            context.setResult(CoreVM::CoreNumber(EXIT_FAILURE));
            return;
        }

        SpawnConfig config;
        config.program = *programPath;
        config.arguments = std::vector<std::string>(args.begin() + 1, args.end());
        config.stdinFd = _currentPipelineBuilder.defaultStdinFd;
        config.stdoutFd = _currentPipelineBuilder.defaultStdoutFd;
        config.closeExtraFds = true;
        config.keepOpenFds = _procSubstExposedFds; // Keep process substitution fds open

        // Apply any redirects that were set up
        applyRedirects(config);

        auto const spawnResult = _processManager.spawn(config);
        if (!spawnResult.has_value())
        {
            error("Failed to spawn {}: {}", program, toString(spawnResult.error()));
            context.setResult(CoreVM::CoreNumber(EXIT_FAILURE));
            return;
        }

        auto const waitResult = _processManager.wait(spawnResult.value());
        if (!waitResult.has_value())
        {
            error("Failed to wait for {}: {}", program, toString(waitResult.error()));
            context.setResult(CoreVM::CoreNumber(EXIT_FAILURE));
            return;
        }

        _exitCode = waitResult->exitCode;
        if (waitResult->signaled)
            debugLog()()("child process exited with signal {}\n", waitResult->signal);
        else
            debugLog()()("child process exited with code {}\n", _exitCode);

        // Clean up process substitution state after command finishes
        cleanupProcSubst();

        context.setResult(CoreVM::CoreNumber(_exitCode));
    }

    void builtinCallProcessShellPiped(CoreVM::Params& context)
    {
        bool const lastInChain = context.getBool(1);
        CoreVM::CoreStringArray const& args = context.getStringArray(2);

        std::string const& program = args.at(0);
        auto const programPath = resolveProgram(program);

        if (!programPath.has_value())
        {
            error("{}: {}", program, toString(programPath.error()));
            context.setResult(CoreVM::CoreNumber(EXIT_FAILURE));
            return;
        }

        auto const [stdinFd, stdoutFd] = _currentPipelineBuilder.requestShellPipe(lastInChain);

        SpawnConfig config;
        config.program = *programPath;
        config.arguments = std::vector<std::string>(args.begin() + 1, args.end());
        config.stdinFd = stdinFd;
        config.stdoutFd = stdoutFd;
        config.processGroup = !_currentProcessGroupPids.empty()
                                  ? std::make_optional(_currentProcessGroupPids.front())
                                  : std::make_optional<ProcessId>(0);
        config.closeExtraFds = true;
        config.keepOpenFds = _procSubstExposedFds; // Keep process substitution fds open

        // Apply any redirects that were set up
        applyRedirects(config);

        auto const spawnResult = _processManager.spawn(config);
        if (!spawnResult.has_value())
        {
            error("Failed to spawn {}: {}", program, toString(spawnResult.error()));
            context.setResult(CoreVM::CoreNumber(EXIT_FAILURE));
            return;
        }

        ProcessId const pid = spawnResult.value();
        _leftPid = _rightPid;
        _rightPid = pid;
        _currentProcessGroupPids.push_back(pid);

        if (lastInChain)
        {
            // This is the last process in the chain, so we need to wait for all
            for (ProcessId const processPid: _currentProcessGroupPids)
            {
                auto const waitResult = _processManager.wait(processPid);
                if (!waitResult.has_value())
                {
                    error("Failed to wait for process {}: {}", processPid, toString(waitResult.error()));
                    continue;
                }

                _exitCode = waitResult->exitCode;
                if (waitResult->signaled)
                    debugLog()()("child process {} exited with signal {}\n", processPid, waitResult->signal);
                else
                    debugLog()()("child process {} exited with code {}\n", processPid, _exitCode);
            }
            _currentProcessGroupPids.clear();
            _leftPid = std::nullopt;
            _rightPid = std::nullopt;

            // Clean up process substitution state after pipeline finishes
            cleanupProcSubst();
        }

        context.setResult(CoreVM::CoreNumber(_exitCode));
    }

    void builtinChDir(CoreVM::Params& context)
    {
        std::string const& path = context.getString(1);

        _env.set("OLDPWD", _env.get("PWD").value_or(""));
        _env.set("PWD", path);

        auto const result = _processManager.changeDirectory(path);
        if (!result.has_value())
            error("Failed to change directory to '{}': {}", path, toString(result.error()));

        context.setResult(result.has_value());
    }

    void builtinChDirHome(CoreVM::Params& context)
    {
        auto const path = _env.get("HOME").value_or("/");
        _env.set("OLDPWD", std::filesystem::current_path().string());
        _env.set("PWD", path);

        auto const result = _processManager.changeDirectory(std::filesystem::path(path));
        if (!result.has_value())
            error("Failed to change directory to '{}': {}", path, toString(result.error()));

        context.setResult(result.has_value());
    }

    void builtinSet(CoreVM::Params& context)
    {
        _env.set(context.getString(1), context.getString(2));
        context.setResult(true);
    }

    void builtinUnset(CoreVM::Params& context)
    {
        _env.unset(context.getString(1));
        context.setResult(true);
    }

    void builtinGetVar(CoreVM::Params& context)
    {
        auto const& name = context.getString(1);
        auto const value = _env.get(name);
        context.setResult(std::string(value.value_or("")));
    }

    void builtinGetExitStatus(CoreVM::Params& context) { context.setResult(std::to_string(_exitCode)); }

    void builtinGetProcessId(CoreVM::Params& context) { context.setResult(std::to_string(_shellPid)); }

    void builtinGetBackgroundId(CoreVM::Params& context)
    {
        if (_lastBackgroundPid.has_value())
            context.setResult(std::to_string(_lastBackgroundPid.value()));
        else
            context.setResult("");
    }

    void builtinGetPositional(CoreVM::Params& context)
    {
        auto const index = static_cast<size_t>(context.getInt(1));
        if (index < _positionalParameters.size())
            context.setResult(_positionalParameters[index]);
        else
            context.setResult("");
    }

    // Command builder functions for dynamic argument construction
    void builtinCmdStart(CoreVM::Params& context)
    {
        // Push a new command builder onto the stack for nested command support
        _cmdBuilderStack.emplace_back();
        cmdBuilderArgs().push_back(context.getString(1));
    }

    void builtinCmdArg(CoreVM::Params& context) { cmdBuilderArgs().push_back(context.getString(1)); }

    void builtinCmdExec(CoreVM::Params& context)
    {
        if (cmdBuilderArgs().empty())
        {
            error("No command to execute");
            context.setResult(CoreVM::CoreNumber(EXIT_FAILURE));
            return;
        }

        std::string const& program = cmdBuilderArgs().at(0);
        auto const programPath = resolveProgram(program);

        if (!programPath.has_value())
        {
            error("{}: {}", program, toString(programPath.error()));
            context.setResult(CoreVM::CoreNumber(EXIT_FAILURE));
            return;
        }

        SpawnConfig config;
        config.program = *programPath;
        config.arguments = std::vector<std::string>(cmdBuilderArgs().begin() + 1, cmdBuilderArgs().end());
        config.stdinFd = _currentPipelineBuilder.defaultStdinFd;
        config.stdoutFd = _currentPipelineBuilder.defaultStdoutFd;
        config.closeExtraFds = true;
        config.keepOpenFds = _procSubstExposedFds; // Keep process substitution fds open

        // Apply any redirects that were set up
        applyRedirects(config);

        auto const spawnResult = _processManager.spawn(config);
        if (!spawnResult.has_value())
        {
            error("Failed to spawn {}: {}", program, toString(spawnResult.error()));
            context.setResult(CoreVM::CoreNumber(EXIT_FAILURE));
            return;
        }

        auto const waitResult = _processManager.wait(spawnResult.value());
        if (!waitResult.has_value())
        {
            error("Failed to wait for {}: {}", program, toString(waitResult.error()));
            context.setResult(CoreVM::CoreNumber(EXIT_FAILURE));
            return;
        }

        _exitCode = waitResult->exitCode;
        if (waitResult->signaled)
            debugLog()()("child process exited with signal {}\n", waitResult->signal);
        else
            debugLog()()("child process exited with code {}\n", _exitCode);

        // Clean up process substitution state after command finishes
        cleanupProcSubst();

        // Pop this command builder from the stack
        if (!_cmdBuilderStack.empty())
            _cmdBuilderStack.pop_back();
        context.setResult(CoreVM::CoreNumber(_exitCode));
    }

    void builtinCmdExecPiped(CoreVM::Params& context)
    {
        bool const lastInChain = context.getBool(1);

        if (cmdBuilderArgs().empty())
        {
            error("No command to execute");
            context.setResult(CoreVM::CoreNumber(EXIT_FAILURE));
            return;
        }

        std::string const& program = cmdBuilderArgs().at(0);
        auto const programPath = resolveProgram(program);

        if (!programPath.has_value())
        {
            error("{}: {}", program, toString(programPath.error()));
            context.setResult(CoreVM::CoreNumber(EXIT_FAILURE));
            return;
        }

        auto const [stdinFd, stdoutFd] = _currentPipelineBuilder.requestShellPipe(lastInChain);

        SpawnConfig config;
        config.program = *programPath;
        config.arguments = std::vector<std::string>(cmdBuilderArgs().begin() + 1, cmdBuilderArgs().end());
        config.stdinFd = stdinFd;
        config.stdoutFd = stdoutFd;
        config.processGroup = !_currentProcessGroupPids.empty()
                                  ? std::make_optional(_currentProcessGroupPids.front())
                                  : std::make_optional<ProcessId>(0);
        config.closeExtraFds = true;
        config.keepOpenFds = _procSubstExposedFds; // Keep process substitution fds open

        // Apply any redirects that were set up
        applyRedirects(config);

        auto const spawnResult = _processManager.spawn(config);
        if (!spawnResult.has_value())
        {
            error("Failed to spawn {}: {}", program, toString(spawnResult.error()));
            context.setResult(CoreVM::CoreNumber(EXIT_FAILURE));
            return;
        }

        ProcessId const pid = spawnResult.value();
        _leftPid = _rightPid;
        _rightPid = pid;
        _currentProcessGroupPids.push_back(pid);

        if (lastInChain)
        {
            // This is the last process in the chain, so we need to wait for all
            for (ProcessId const processPid: _currentProcessGroupPids)
            {
                auto const waitResult = _processManager.wait(processPid);
                if (!waitResult.has_value())
                {
                    error("Failed to wait for process {}: {}", processPid, toString(waitResult.error()));
                    continue;
                }

                _exitCode = waitResult->exitCode;
                if (waitResult->signaled)
                    debugLog()()("child process {} exited with signal {}\n", processPid, waitResult->signal);
                else
                    debugLog()()("child process {} exited with code {}\n", processPid, _exitCode);
            }
            _currentProcessGroupPids.clear();
            _leftPid = std::nullopt;
            _rightPid = std::nullopt;

            // Clean up process substitution state after pipeline finishes
            cleanupProcSubst();
        }

        // Pop this command builder from the stack
        if (!_cmdBuilderStack.empty())
            _cmdBuilderStack.pop_back();
        context.setResult(CoreVM::CoreNumber(_exitCode));
    }

    void builtinSetAndExport(CoreVM::Params& context)
    {
        _env.set(context.getString(1), context.getString(2));
        _env.exportVariable(context.getString(1));
    }

    void builtinExport(CoreVM::Params& context) { _env.exportVariable(context.getString(1)); }

    void builtinTrue(CoreVM::Params& context)
    {
        _exitCode = 0;
        context.setResult(true);
    }

    void builtinFalse(CoreVM::Params& context)
    {
        _exitCode = 1;
        context.setResult(false);
    }

    void builtinReadDefault(CoreVM::Params& context)
    {
        std::string const line =
            readLine(_tty, std::format("{}read{}>{} ", "\033[1;34m", "\033[37;1m", "\033[m"));
        _env.set("REPLY", line);
        context.setResult(line);
    }

    void builtinRead(CoreVM::Params& context)
    {
        CoreVM::CoreStringArray const& args = context.getStringArray(1);
        std::string const& variable = args.at(0);
        std::string const line =
            readLine(_tty, std::format("{}read{}>{} ", "\033[1;34m", "\033[37;1m", "\033[m"));
        _env.set(variable, line);
        context.setResult(line);
    }

    // helper-builtins for redirects and pipes
    void builtinOpenRead(CoreVM::Params& context)
    {
        std::string const& path = context.getString(1);
        auto const result = _processManager.openFile(path, O_RDONLY);
        if (!result.has_value())
        {
            error("Failed to open file '{}': {}", path, toString(result.error()));
            context.setResult(CoreVM::CoreNumber(-1));
            return;
        }

        context.setResult(CoreVM::CoreNumber(result.value()));
    }

    void builtinOpenWrite(CoreVM::Params& context)
    {
        std::string const& path = context.getString(1);
        int const oflags = static_cast<int>(context.getInt(2));
        auto const result = _processManager.openFile(path, oflags ? oflags : (O_WRONLY | O_CREAT | O_TRUNC));
        if (!result.has_value())
        {
            error("Failed to open file '{}': {}", path, toString(result.error()));
            context.setResult(CoreVM::CoreNumber(-1));
            return;
        }

        context.setResult(CoreVM::CoreNumber(result.value()));
    }

    // Redirect management builtins
    void builtinRedirectStart(CoreVM::Params&) { _redirectState.clear(); }

    void builtinRedirectInput(CoreVM::Params& context)
    {
        int const targetFd = static_cast<int>(context.getInt(1));
        std::string path = context.getString(2);
        _redirectState.addInputFile(targetFd, std::move(path));
    }

    void builtinRedirectOutput(CoreVM::Params& context)
    {
        int const sourceFd = static_cast<int>(context.getInt(1));
        std::string path = context.getString(2);
        bool const append = context.getBool(3);
        _redirectState.addOutputFile(sourceFd, std::move(path), append);
    }

    void builtinRedirectFdDup(CoreVM::Params& context)
    {
        int const sourceFd = static_cast<int>(context.getInt(1));
        int const targetFd = static_cast<int>(context.getInt(2));
        _redirectState.addFdDup(sourceFd, targetFd);
    }

    void builtinRedirectHeredoc(CoreVM::Params& context)
    {
        int const targetFd = static_cast<int>(context.getInt(1));
        std::string content = context.getString(2);
        _redirectState.addHereDoc(targetFd, std::move(content));
    }

    void builtinRedirectHerestring(CoreVM::Params& context)
    {
        int const targetFd = static_cast<int>(context.getInt(1));
        std::string content = context.getString(2);
        _redirectState.addHereString(targetFd, std::move(content));
    }

    void builtinRedirectEnd(CoreVM::Params&)
    {
        // Close any fds that were opened for redirects
        for (auto& entry: _redirectState.entries)
        {
            if (entry.openedFd >= 0 && entry.openedFd != STDIN_FILENO && entry.openedFd != STDOUT_FILENO
                && entry.openedFd != STDERR_FILENO)
            {
                close(entry.openedFd);
                entry.openedFd = -1;
            }
        }
        _redirectState.clear();
    }

    /// Starts command substitution capture by creating a pipe and redirecting stdout.
    void builtinSubstStart(CoreVM::Params&)
    {
        // Create a new capture state
        _substitutionCapture.emplace();

        // Create pipe for capturing output
        auto pipeResult = createPipe();
        if (!pipeResult.has_value())
        {
            error("Failed to create pipe for command substitution: {}", toString(pipeResult.error()));
            _substitutionCapture.reset();
            return;
        }

        _substitutionCapture->pipe = std::move(pipeResult.value());

        // Save the current stdout and redirect to the pipe's write end
        _substitutionCapture->savedStdout = _currentPipelineBuilder.defaultStdoutFd;
        _currentPipelineBuilder.defaultStdoutFd = _substitutionCapture->pipe->writer();
    }

    /// Ends command substitution capture, reads the captured output, and returns it.
    void builtinSubstEnd(CoreVM::Params& context)
    {
        if (!_substitutionCapture)
        {
            error("Command substitution end without matching start");
            context.setResult(std::string {});
            return;
        }

        // Restore original stdout
        _currentPipelineBuilder.defaultStdoutFd = _substitutionCapture->savedStdout;

        // Close the write end so we can read until EOF
        _substitutionCapture->pipe->closeWriter();

        // Read all output from the pipe
        std::string output;
        char buffer[4096];
        while (true)
        {
            ssize_t const bytesRead = read(_substitutionCapture->pipe->reader(), buffer, sizeof(buffer));
            if (bytesRead <= 0)
                break;
            output.append(buffer, static_cast<size_t>(bytesRead));
        }

        // Close the read end
        _substitutionCapture->pipe->closeReader();

        // Trim trailing newlines (standard shell behavior)
        while (!output.empty() && output.back() == '\n')
            output.pop_back();

        _substitutionCapture.reset();

        context.setResult(std::move(output));
    }

    /// Process substitution using fork: creates a pipe, forks, and returns 0 in child, non-zero in parent.
    /// For <(command), is_write=false: child writes to pipe, parent reads via /dev/fd/N
    /// For >(command), is_write=true: child reads from pipe, parent writes via /dev/fd/N
    void builtinProcSubstFork(CoreVM::Params& context)
    {
        bool const isWrite = context.getBool(1);

        // Create pipe for process substitution
        auto pipeResult = createPipe();
        if (!pipeResult.has_value())
        {
            error("Failed to create pipe for process substitution: {}", toString(pipeResult.error()));
            context.setResult(CoreVM::CoreNumber(-1));
            return;
        }

        auto pipe = std::move(pipeResult.value());

#if !defined(_WIN32)
        // Fork to create child process for the substituted command
        pid_t const pid = fork();

        if (pid < 0)
        {
            error("Failed to fork for process substitution: {}", strerror(errno));
            context.setResult(CoreVM::CoreNumber(-1));
            return;
        }

        if (pid == 0)
        {
            // Child process: will execute the inner command
            if (isWrite)
            {
                // >(command) - child reads from pipe's read end via stdin
                pipe->closeWriter();
                dup2(pipe->reader(), STDIN_FILENO);
                pipe->closeReader();
            }
            else
            {
                // <(command) - child writes to pipe's write end via stdout
                pipe->closeReader();
                dup2(pipe->writer(), STDOUT_FILENO);
                pipe->closeWriter();
            }

            // Return 0 to indicate child
            context.setResult(CoreVM::CoreNumber(0));
            return;
        }

        // Parent process
        _procSubstChildPids.push_back(static_cast<ProcessId>(pid));

        NativeHandle exposedFd;
        if (isWrite)
        {
            // >(command) - parent writes to the pipe's write end
            pipe->closeReader();
            exposedFd = pipe->releaseWriter();
        }
        else
        {
            // <(command) - parent reads from the pipe's read end
            pipe->closeWriter();
            exposedFd = pipe->releaseReader();
        }

        // Track the exposed fd for cleanup
        _procSubstExposedFds.push_back(exposedFd);

        // Build the /dev/fd/N or /proc/self/fd/N path
    #if defined(__linux__)
        _procSubstFdPath = std::format("/proc/self/fd/{}", exposedFd);
    #else
        _procSubstFdPath = std::format("/dev/fd/{}", exposedFd);
    #endif

        // Return non-zero to indicate parent
        context.setResult(CoreVM::CoreNumber(1));
#else
        // Windows: not yet implemented
        error("Process substitution not implemented on Windows");
        context.setResult(CoreVM::CoreNumber(-1));
#endif
    }

    /// Called by child process to exit after running the substituted command.
    void builtinProcSubstExit(CoreVM::Params&)
    {
#if !defined(_WIN32)
        _exit(0);
#endif
    }

    /// Returns the fd path for the current process substitution (called by parent).
    void builtinProcSubstGetPath(CoreVM::Params& context) { context.setResult(_procSubstFdPath); }

    /// Cleans up process substitution state: waits for children and closes fds.
    void builtinProcSubstCleanup(CoreVM::Params&) { cleanupProcSubst(); }

    /// Expands ~ to the current user's home directory, appending optional suffix.
    void builtinExpandTilde(CoreVM::Params& context)
    {
        auto const& suffix = context.getString(1);
        std::string home = std::string(_env.get("HOME").value_or(""));
        context.setResult(home + suffix);
    }

    /// Expands ~user to the specified user's home directory, appending optional suffix.
    void builtinExpandTildeUser(CoreVM::Params& context)
    {
        auto const& user = context.getString(1);
        auto const& suffix = context.getString(2);
#if !defined(_WIN32)
        if (auto* pw = getpwnam(user.c_str()); pw != nullptr)
            context.setResult(std::string(pw->pw_dir) + suffix);
        else
            context.setResult("~" + user + suffix); // Return unexpanded on failure
#else
        // Windows: not yet implemented, return unexpanded
        context.setResult("~" + user + suffix);
#endif
    }

    /// Cross-platform glob pattern matching for a single path component.
    /// Returns true if the filename matches the pattern.
    [[nodiscard]] static bool globMatchFilename(std::string_view filename, std::string_view pattern)
    {
        size_t fi = 0;
        size_t pi = 0;
        size_t starIdx = std::string_view::npos;
        size_t matchIdx = 0;

        while (fi < filename.size())
        {
            if (pi < pattern.size() && pattern[pi] == '*')
            {
                starIdx = pi;
                matchIdx = fi;
                ++pi;
            }
            else if (pi < pattern.size() && (pattern[pi] == '?' || pattern[pi] == filename[fi]))
            {
                ++fi;
                ++pi;
            }
            else if (pi < pattern.size() && pattern[pi] == '[')
            {
                // Bracket expression [abc] or [a-z]
                bool negate = false;
                bool matched = false;
                ++pi;
                if (pi < pattern.size() && (pattern[pi] == '!' || pattern[pi] == '^'))
                {
                    negate = true;
                    ++pi;
                }
                auto const bracketStart = pi;
                while (pi < pattern.size() && pattern[pi] != ']')
                {
                    if (pi + 2 < pattern.size() && pattern[pi + 1] == '-' && pattern[pi + 2] != ']')
                    {
                        // Range [a-z]
                        if (filename[fi] >= pattern[pi] && filename[fi] <= pattern[pi + 2])
                            matched = true;
                        pi += 3;
                    }
                    else
                    {
                        if (filename[fi] == pattern[pi])
                            matched = true;
                        ++pi;
                    }
                }
                if (pi < pattern.size())
                    ++pi; // Skip closing ]

                if (negate)
                    matched = !matched;
                if (!matched)
                {
                    if (starIdx != std::string_view::npos)
                    {
                        pi = starIdx + 1;
                        ++matchIdx;
                        fi = matchIdx;
                    }
                    else
                    {
                        return false;
                    }
                }
                else
                {
                    ++fi;
                }
            }
            else if (starIdx != std::string_view::npos)
            {
                pi = starIdx + 1;
                ++matchIdx;
                fi = matchIdx;
            }
            else
            {
                return false;
            }
        }

        // Consume remaining stars in pattern
        while (pi < pattern.size() && pattern[pi] == '*')
            ++pi;

        return pi == pattern.size();
    }

    /// Cross-platform glob expansion using std::filesystem.
    /// Expands patterns like *.txt, dir/*, file?.log, [abc].txt, **/*.cpp
    [[nodiscard]] static std::vector<std::string> expandGlobPattern(std::string_view pattern)
    {
        namespace fs = std::filesystem;
        std::vector<std::string> results;
        std::string patternStr(pattern);

        // Check for ** (recursive globbing)
        auto const starstarPos = patternStr.find("**");
        if (starstarPos != std::string::npos)
        {
            return expandRecursiveGlob(patternStr);
        }

        fs::path patternPath(patternStr);

        // Split into directory and filename pattern
        fs::path dirPath = patternPath.parent_path();
        std::string filePattern = patternPath.filename().string();

        // If no directory specified, use current directory
        if (dirPath.empty())
            dirPath = ".";

        // Check if the pattern contains glob characters
        bool hasGlobChars = filePattern.find_first_of("*?[") != std::string::npos;

        if (!hasGlobChars)
        {
            // No glob chars - just return the pattern as-is if it doesn't exist
            // (standard shell behavior: keep the literal)
            return {};
        }

        std::error_code ec;
        if (!fs::exists(dirPath, ec) || ec)
        {
            // Directory doesn't exist - return empty (no matches)
            return {};
        }

        // Iterate directory and match files
        for (auto const& entry: fs::directory_iterator(dirPath, ec))
        {
            if (ec)
                break;

            std::string filename = entry.path().filename().string();
            if (globMatchFilename(filename, filePattern))
            {
                if (dirPath == ".")
                    results.push_back(filename);
                else
                    results.push_back(entry.path().string());
            }
        }

        // Sort results for consistent output
        std::ranges::sort(results);

        return results;
    }

    /// Expands ** recursive glob patterns like **/*.cpp
    [[nodiscard]] static std::vector<std::string> expandRecursiveGlob(std::string_view pattern)
    {
        namespace fs = std::filesystem;
        std::vector<std::string> results;
        std::string patternStr(pattern);

        // Split pattern at **
        auto const starstarPos = patternStr.find("**");
        if (starstarPos == std::string::npos)
            return {};

        // Get the base directory (before **)
        std::string basePath = patternStr.substr(0, starstarPos);
        // Remove trailing slash if present
        while (!basePath.empty() && (basePath.back() == '/' || basePath.back() == '\\'))
            basePath.pop_back();
        if (basePath.empty())
            basePath = ".";

        // Get the suffix pattern (after **)
        std::string suffixPattern = patternStr.substr(starstarPos + 2);
        // Remove leading slash if present
        while (!suffixPattern.empty() && (suffixPattern.front() == '/' || suffixPattern.front() == '\\'))
            suffixPattern.erase(0, 1);

        std::error_code ec;
        if (!fs::exists(basePath, ec) || ec)
            return {};

        // Recursively iterate all files
        for (auto const& entry: fs::recursive_directory_iterator(basePath, ec))
        {
            if (ec)
                break;

            if (!entry.is_regular_file())
                continue;

            // Get the relative path from base
            std::string filePath = entry.path().string();
            std::string filename = entry.path().filename().string();

            // If there's a suffix pattern, match against filename
            if (!suffixPattern.empty())
            {
                if (globMatchFilename(filename, suffixPattern))
                    results.push_back(filePath);
            }
            else
            {
                results.push_back(filePath);
            }
        }

        // Sort results for consistent output
        std::ranges::sort(results);

        return results;
    }

    /// Expands glob pattern to matching files, adding them to cmdBuilderArgs.
    /// If no matches are found, the original pattern is kept (shell default behavior).
    void builtinExpandGlob(CoreVM::Params& context)
    {
        auto const& pattern = context.getString(1);

        auto matches = expandGlobPattern(pattern);
        if (matches.empty())
        {
            // No matches - keep the pattern literal (standard shell behavior)
            cmdBuilderArgs().push_back(pattern);
        }
        else
        {
            for (auto& match: matches)
                cmdBuilderArgs().push_back(std::move(match));
        }
    }

    /// Converts arithmetic result to string.
    /// Handles signed 64-bit values properly (CoreVM stores as unsigned, but we interpret as signed).
    void builtinArithToString(CoreVM::Params& context)
    {
        auto const unsignedValue = context.getInt(1);
        // Interpret as signed for proper negative number display
        auto const signedValue = static_cast<int64_t>(unsignedValue);
        context.setResult(std::to_string(signedValue));
    }

    /// Gets variable value as integer for arithmetic expansion.
    void builtinArithGetVar(CoreVM::Params& context)
    {
        auto const& name = context.getString(1);
        auto const value = _env.get(name);
        if (!value.has_value() || value->empty())
        {
            context.setResult(CoreVM::CoreNumber(0));
            return;
        }
        int64_t result = 0;
        auto [ptr, ec] = std::from_chars(value->data(), value->data() + value->size(), result);
        context.setResult(CoreVM::CoreNumber(result));
    }

    /// Power operation for arithmetic expansion.
    void builtinArithPow(CoreVM::Params& context)
    {
        auto const base = context.getInt(1);
        auto const exp = context.getInt(2);
        if (exp < 0)
        {
            context.setResult(CoreVM::CoreNumber(0)); // Integer division: negative exponent = 0
            return;
        }
        int64_t result = 1;
        for (int64_t i = 0; i < exp; ++i)
            result *= base;
        context.setResult(CoreVM::CoreNumber(result));
    }

    /// ${#VAR} - returns length of variable value
    void builtinExpandParamLength(CoreVM::Params& context)
    {
        auto const& varName = context.getString(1);
        auto const value = _env.get(varName);
        context.setResult(std::to_string(value.value_or("").size()));
    }

    /// ${VAR:-default} - returns VAR if set and non-empty, otherwise default
    void builtinExpandParamDefault(CoreVM::Params& context)
    {
        auto const& varName = context.getString(1);
        auto const& defaultValue = context.getString(2);
        auto const value = _env.get(varName);
        if (value.has_value() && !value->empty())
            context.setResult(std::string(*value));
        else
            context.setResult(defaultValue);
    }

    /// ${VAR:+alternate} - returns alternate if VAR is set and non-empty, otherwise empty
    void builtinExpandParamAlternate(CoreVM::Params& context)
    {
        auto const& varName = context.getString(1);
        auto const& alternate = context.getString(2);
        auto const value = _env.get(varName);
        if (value.has_value() && !value->empty())
            context.setResult(alternate);
        else
            context.setResult("");
    }

    /// ${VAR:=default} - assigns and returns default if VAR is unset or empty
    void builtinExpandParamAssign(CoreVM::Params& context)
    {
        auto const& varName = context.getString(1);
        auto const& defaultValue = context.getString(2);
        auto const value = _env.get(varName);
        if (value.has_value() && !value->empty())
            context.setResult(std::string(*value));
        else
        {
            _env.set(varName, defaultValue);
            context.setResult(defaultValue);
        }
    }

    /// ${VAR:?error} - returns VAR if set and non-empty, otherwise prints error and fails
    void builtinExpandParamError(CoreVM::Params& context)
    {
        auto const& varName = context.getString(1);
        auto const& errorMsg = context.getString(2);
        auto const value = _env.get(varName);
        if (value.has_value() && !value->empty())
            context.setResult(std::string(*value));
        else
        {
            std::string const msg = errorMsg.empty() ? std::format("{}: parameter null or not set", varName)
                                                     : std::format("{}: {}", varName, errorMsg);
            error("{}", msg);
            _exitCode = 1;
            context.setResult("");
        }
    }

    /// Helper for glob-style pattern matching (recursive implementation)
    /// Returns true if pattern matches the entire text
    [[nodiscard]] static bool globMatch(std::string_view text, std::string_view pattern)
    {
        size_t ti = 0;
        size_t pi = 0;
        size_t starIdx = std::string_view::npos;
        size_t matchIdx = 0;

        while (ti < text.size())
        {
            if (pi < pattern.size() && (pattern[pi] == '?' || pattern[pi] == text[ti]))
            {
                ++ti;
                ++pi;
            }
            else if (pi < pattern.size() && pattern[pi] == '*')
            {
                starIdx = pi;
                matchIdx = ti;
                ++pi;
            }
            else if (starIdx != std::string_view::npos)
            {
                pi = starIdx + 1;
                ++matchIdx;
                ti = matchIdx;
            }
            else
            {
                return false;
            }
        }

        // Consume remaining stars in pattern
        while (pi < pattern.size() && pattern[pi] == '*')
            ++pi;

        return pi == pattern.size();
    }

    /// Helper to find all possible prefix matches of a pattern in text
    [[nodiscard]] static std::vector<size_t> findPrefixMatches(std::string_view text,
                                                               std::string_view pattern)
    {
        std::vector<size_t> matches;

        // For prefix matching, we try to match pattern against each prefix of text
        for (size_t len = 0; len <= text.size(); ++len)
        {
            if (globMatch(text.substr(0, len), pattern))
                matches.push_back(len);
        }

        return matches;
    }

    /// Helper to find all possible suffix matches of a pattern in text
    [[nodiscard]] static std::vector<size_t> findSuffixMatches(std::string_view text,
                                                               std::string_view pattern)
    {
        std::vector<size_t> matches;

        // For suffix matching, we try to match pattern against each suffix of text
        for (size_t start = 0; start <= text.size(); ++start)
        {
            if (globMatch(text.substr(start), pattern))
                matches.push_back(start); // Store the start position
        }

        return matches;
    }

    /// Helper to find first pattern match and its length starting at pos
    [[nodiscard]] static std::optional<size_t> findPatternMatchLength(std::string_view text,
                                                                      std::string_view pattern)
    {
        // Try each possible match length starting from 1
        for (size_t len = 1; len <= text.size(); ++len)
        {
            if (globMatch(text.substr(0, len), pattern))
                return len;
        }
        return std::nullopt;
    }

    /// ${VAR#pattern} / ${VAR##pattern} - remove prefix matching pattern
    void builtinExpandParamRemovePrefix(CoreVM::Params& context)
    {
        auto const& varName = context.getString(1);
        auto const& pattern = context.getString(2);
        bool const longest = context.getBool(3);
        auto const value = _env.get(varName);
        std::string const val = std::string(value.value_or(""));

        if (val.empty() || pattern.empty())
        {
            context.setResult(val);
            return;
        }

        auto const matches = findPrefixMatches(val, pattern);
        if (matches.empty())
        {
            context.setResult(val);
            return;
        }

        // shortest = first match, longest = last match
        size_t const matchLen = longest ? matches.back() : matches.front();
        context.setResult(val.substr(matchLen));
    }

    /// ${VAR%pattern} / ${VAR%%pattern} - remove suffix matching pattern
    void builtinExpandParamRemoveSuffix(CoreVM::Params& context)
    {
        auto const& varName = context.getString(1);
        auto const& pattern = context.getString(2);
        bool const longest = context.getBool(3);
        auto const value = _env.get(varName);
        std::string const val = std::string(value.value_or(""));

        if (val.empty() || pattern.empty())
        {
            context.setResult(val);
            return;
        }

        auto const matches = findSuffixMatches(val, pattern);
        if (matches.empty())
        {
            context.setResult(val);
            return;
        }

        // For suffix: shortest match starts later (larger start), longest starts earlier (smaller start)
        size_t const matchStart = longest ? matches.front() : matches.back();
        context.setResult(val.substr(0, matchStart));
    }

    /// ${VAR/pattern/replacement} / ${VAR//pattern/replacement} - replace pattern
    void builtinExpandParamReplace(CoreVM::Params& context)
    {
        auto const& varName = context.getString(1);
        auto const& pattern = context.getString(2);
        auto const& replacement = context.getString(3);
        bool const replaceAll = context.getBool(4);
        auto const value = _env.get(varName);
        std::string const val = std::string(value.value_or(""));

        if (val.empty() || pattern.empty())
        {
            context.setResult(val);
            return;
        }

        std::string result;
        size_t pos = 0;

        while (pos < val.size())
        {
            // Try to match pattern at current position
            auto const matchResult = findPatternMatchLength(std::string_view(val).substr(pos), pattern);
            if (matchResult.has_value())
            {
                result += replacement;
                pos += *matchResult;
                if (!replaceAll)
                {
                    result += val.substr(pos);
                    break;
                }
            }
            else
            {
                result += val[pos];
                ++pos;
            }
        }

        context.setResult(result);
    }

    /// Helper to clean up process substitution state.
    void cleanupProcSubst()
    {
#if !defined(_WIN32)
        // Wait for all child processes
        for (ProcessId childPid: _procSubstChildPids)
        {
            int status = 0;
            waitpid(static_cast<pid_t>(childPid), &status, 0);
        }
        _procSubstChildPids.clear();

        // Close all exposed fds
        for (NativeHandle fd: _procSubstExposedFds)
        {
            if (fd >= 0)
                close(fd);
        }
        _procSubstExposedFds.clear();

        _procSubstFdPath.clear();
#endif
    }

    /// Applies the collected redirects to a SpawnConfig.
    ///
    /// Opens files and creates pipes as needed, updating the spawn config
    /// with the appropriate file descriptors.
    void applyRedirects(SpawnConfig& config)
    {
        for (auto& entry: _redirectState.entries)
        {
            switch (entry.type)
            {
                case RedirectState::Type::InputFile: {
                    auto const result = _processManager.openFile(entry.path, O_RDONLY);
                    if (!result.has_value())
                    {
                        error("Failed to open '{}' for reading: {}", entry.path, toString(result.error()));
                        continue;
                    }
                    entry.openedFd = result.value();
                    if (entry.targetFd == STDIN_FILENO)
                        config.stdinFd = entry.openedFd;
                    break;
                }
                case RedirectState::Type::OutputFile: {
                    int const oflags =
                        entry.append ? (O_WRONLY | O_CREAT | O_APPEND) : (O_WRONLY | O_CREAT | O_TRUNC);
                    auto const result = _processManager.openFile(entry.path, oflags);
                    if (!result.has_value())
                    {
                        error("Failed to open '{}' for writing: {}", entry.path, toString(result.error()));
                        continue;
                    }
                    entry.openedFd = result.value();
                    if (entry.sourceFd == STDOUT_FILENO)
                        config.stdoutFd = entry.openedFd;
                    else if (entry.sourceFd == STDERR_FILENO)
                        config.stderrFd = entry.openedFd;
                    break;
                }
                case RedirectState::Type::FdDup: {
                    // For fd duplication like 2>&1, we need to make stderr point to stdout
                    if (entry.sourceFd == STDERR_FILENO && entry.targetFd == STDOUT_FILENO)
                        config.stderrFd = config.stdoutFd;
                    else if (entry.sourceFd == STDOUT_FILENO && entry.targetFd == STDERR_FILENO)
                        config.stdoutFd = config.stderrFd;
                    break;
                }
                case RedirectState::Type::HereDoc:
                case RedirectState::Type::HereString: {
                    // Create a pipe and write content to it
                    auto pipeResult = createPipe();
                    if (!pipeResult.has_value())
                    {
                        error("Failed to create pipe for here-string: {}", toString(pipeResult.error()));
                        continue;
                    }
                    auto pipe = std::move(pipeResult.value());

                    // Write content to the pipe's write end
                    std::string const& content = entry.content;
                    ssize_t const written = write(pipe->writer(), content.data(), content.size());
                    if (written < 0)
                    {
                        error("Failed to write to here-string pipe: {}", strerror(errno));
                        continue;
                    }

                    // Add newline for here-strings if not present
                    if (entry.type == RedirectState::Type::HereString && !content.empty()
                        && content.back() != '\n')
                    {
                        write(pipe->writer(), "\n", 1);
                    }

                    // Close write end so reader gets EOF
                    pipe->closeWriter();

                    // Use reader as stdin
                    entry.openedFd = pipe->releaseReader();
                    if (entry.targetFd == STDIN_FILENO)
                        config.stdinFd = entry.openedFd;
                    break;
                }
            }
        }
    }

    /// Resolves a program name to its full path.
    ///
    /// @param program Program name or path to resolve
    /// @return Full path to the program on success, or ShellError::ProgramNotFound
    [[nodiscard]] std::expected<std::filesystem::path, ShellError> resolveProgram(
        std::string const& program) const
    {
        // Check if it's an absolute or relative path
        if (program.contains('/'))
        {
            if (std::filesystem::exists(program))
                return std::filesystem::path(program);
            return std::unexpected(ShellError::ProgramNotFound);
        }

        // Search in PATH
        auto const pathEnv = _env.get("PATH");
        if (!pathEnv.has_value())
            return std::unexpected(ShellError::VariableNotFound);

        auto const pathEnvValue = pathEnv.value();
        auto const paths = crispy::split(pathEnvValue, ':');

        for (auto const& pathStr: paths)
        {
            auto const programPath = std::filesystem::path(pathStr) / program;
            if (std::filesystem::exists(programPath))
            {
                debugLog()()("Found program: {}", programPath.string());
                return programPath;
            }
        }

        return std::unexpected(ShellError::ProgramNotFound);
    }

    void trace(CoreVM::Instruction instr, size_t ip, size_t sp)
    {
        debugLog()()("trace: {}\n", CoreVM::disassemble(instr, ip, sp, &_currentProgram->constants()));
    }

    template <typename... Args>
    void error(std::format_string<Args...> const& message, Args&&... args)
    {
        std::println(std::cerr, "{}", std::format(message, std::forward<Args>(args)...));
    }

  private:
    Environment& _env;

    TTY& _tty;

    ProcessManager& _processManager = PosixProcessManager::instance();

    std::unique_ptr<CoreVM::Program> _currentProgram;
    CoreVM::Runner::Globals _globals;

    bool _optimize = false;

    PipelineBuilder _currentPipelineBuilder;

    // This stores the PIDs of all processes in the pipeline's process group.
    std::vector<ProcessId> _currentProcessGroupPids;
    std::optional<ProcessId> _leftPid;
    std::optional<ProcessId> _rightPid;

    // This stores the exit code of the last process in the pipeline.
    // TODO: remember exit codes from all processes in the pipeline's process group
    int _exitCode = -1;

    // Shell's process ID for $$ variable
    ProcessId _shellPid = 0;

    // Last background process ID for $! variable
    std::optional<ProcessId> _lastBackgroundPid;

    // Positional parameters ($0, $1, $2, etc.)
    std::vector<std::string> _positionalParameters;

    // Command builder stack for dynamic argument construction
    // Uses a stack to support nested command substitutions
    std::vector<std::vector<std::string>> _cmdBuilderStack;

    /// Gets the current command builder args (top of stack).
    std::vector<std::string>& cmdBuilderArgs()
    {
        if (_cmdBuilderStack.empty())
            _cmdBuilderStack.emplace_back();
        return _cmdBuilderStack.back();
    }

    // Redirect state for collecting redirects before process spawn
    RedirectState _redirectState;

    // Command substitution capture state
    std::optional<SubstitutionCapture> _substitutionCapture;

    // Process substitution state
    std::vector<std::unique_ptr<Pipe>> _processSubstitutionPipes;
    std::string _procSubstFdPath;                   ///< Fd path for current process substitution
    std::vector<ProcessId> _procSubstChildPids;     ///< Child pids for process substitution cleanup
    std::vector<NativeHandle> _procSubstExposedFds; ///< Exposed fds to close after command

    CoreVM::Runner* _runner = nullptr;
    bool _quit = false;
};
} // namespace endo
