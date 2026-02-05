// SPDX-License-Identifier: Apache-2.0
module;
#include <shell/ProcessGroup.h>

#include <crispy/assert.h>
#include <crispy/utils.h>

#include <cstdio>
#include <cstring>
#include <expected>
#include <format>
#include <iostream>
#include <map>
#include <memory>
#include <print>

#if !defined(_WIN32)
    #include <fcntl.h>
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
        _cmdBuilderArgs.clear();
        _cmdBuilderArgs.push_back(context.getString(1));
    }

    void builtinCmdArg(CoreVM::Params& context) { _cmdBuilderArgs.push_back(context.getString(1)); }

    void builtinCmdExec(CoreVM::Params& context)
    {
        if (_cmdBuilderArgs.empty())
        {
            error("No command to execute");
            context.setResult(CoreVM::CoreNumber(EXIT_FAILURE));
            return;
        }

        std::string const& program = _cmdBuilderArgs.at(0);
        auto const programPath = resolveProgram(program);

        if (!programPath.has_value())
        {
            error("{}: {}", program, toString(programPath.error()));
            context.setResult(CoreVM::CoreNumber(EXIT_FAILURE));
            return;
        }

        SpawnConfig config;
        config.program = *programPath;
        config.arguments = std::vector<std::string>(_cmdBuilderArgs.begin() + 1, _cmdBuilderArgs.end());
        config.stdinFd = _currentPipelineBuilder.defaultStdinFd;
        config.stdoutFd = _currentPipelineBuilder.defaultStdoutFd;
        config.closeExtraFds = true;

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

        _cmdBuilderArgs.clear();
        context.setResult(CoreVM::CoreNumber(_exitCode));
    }

    void builtinCmdExecPiped(CoreVM::Params& context)
    {
        bool const lastInChain = context.getBool(1);

        if (_cmdBuilderArgs.empty())
        {
            error("No command to execute");
            context.setResult(CoreVM::CoreNumber(EXIT_FAILURE));
            return;
        }

        std::string const& program = _cmdBuilderArgs.at(0);
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
        config.arguments = std::vector<std::string>(_cmdBuilderArgs.begin() + 1, _cmdBuilderArgs.end());
        config.stdinFd = stdinFd;
        config.stdoutFd = stdoutFd;
        config.processGroup = !_currentProcessGroupPids.empty()
                                  ? std::make_optional(_currentProcessGroupPids.front())
                                  : std::make_optional<ProcessId>(0);
        config.closeExtraFds = true;

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
        }

        _cmdBuilderArgs.clear();
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

    // Command builder for dynamic argument construction
    std::vector<std::string> _cmdBuilderArgs;

    // Redirect state for collecting redirects before process spawn
    RedirectState _redirectState;

    CoreVM::Runner* _runner = nullptr;
    bool _quit = false;
};
} // namespace endo
